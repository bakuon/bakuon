#pragma once

#include <cstdlib>
#include <memory>
#include <new>
#include <stdexcept>
#include <type_traits>

#ifdef _MSC_VER
#include <malloc.h> // For _aligned_malloc and _aligned_free on Windows
#endif

#include "gui/b_plugin.h"

namespace bakuon::gui {

/**
 * @brief 插件包装类的控制块（Control Block）指针
 * PluginBlock 与 Plugin（不是 IPlugin：Plugin 是内部的加载器/生命周期包装类，
 * IPlugin 才是插件开发者实现的接口，见 include/bakuon/gui/IPlugin.h）组合存储在
 * 同一块内存里，PluginBlock 在前，Plugin 紧随其后：
 *
 * +-----------------+------------------+
 * |     block        |    plugin (T)    |
 * +-----------------+------------------+
 * |      data        |       data       |
 * +-----------------+------------------+
 *
 * 外部存储表只需要整数的原子自增 id 作为 key，PluginBlock 共享指针作为值存储，
 * 同时也支持插件接口 IPlugin::id() 的字符串作为 key 的另一个存储容器（字符串查询极少用到但也需要支持）。
 * 通过 PluginBlock 地址偏移可以找到 Plugin 对象。
 *
 * ## 生命周期管理
 * 只维护一份引用计数，挂在 std::shared_ptr<PluginBlock> 上（由 create() 统一创建，
 * 携带自定义删除器：先析构 Plugin 子对象，再析构 PluginBlock 自身，最后释放整块对齐内存）。
 * 需要 std::shared_ptr<Plugin> 时，用 shared_ptr 的 aliasing 构造函数从
 * shared_ptr<PluginBlock> 借用同一份引用计数（见 pluginOf()），不额外引入 Plugin 自身的
 * enable_shared_from_this ——组合分配 + 自定义删除器的场景下，enable_shared_from_this
 * 需要额外的手工 wiring 才能正确工作，容易踩坑，不如直接用 aliasing 更简单可靠。
 */
class PluginBlock
{
public:
    // 定义为插件块及其插件（管理的）对象分配的内存区域的对齐方式。
    // 用 constexpr 常量代替宏，避免 CACHE_LINE_SIZE 这种通用名字污染全局宏命名空间、
    // 和其他头文件（包括 Qt、第三方库）撞名。
    static constexpr size_t cache_line_size = 64;
    static constexpr size_t allocation_size = cache_line_size;
    static constexpr size_t alignment_size  = cache_line_size;

    PluginBlock(const PluginBlock&)            = delete;
    PluginBlock& operator=(const PluginBlock&) = delete;

    /**
     * @brief 组合分配一个 PluginBlock + Plugin，返回统一管理生命周期的 shared_ptr。
     * @tparam Args 转发给 Plugin 构造函数的参数（例如 (size_t id, QString filepath)
     *              或 (size_t id, std::shared_ptr<IPlugin> builtin)，对应 Plugin 的两个构造函数）。
     */
    template<class... Args>
    static std::shared_ptr<PluginBlock> create(size_t id, Args&&... args)
    {
        void* mem = allocate<Plugin>();

        PluginBlock* block = nullptr;
        try {
            block = constructBlock(mem, id);
        } catch (...) {
            alignedFree(mem);
            throw;
        }

        try {
            constructManaged<Plugin>(mem, id, std::forward<Args>(args)...);
        } catch (...) {
            block->~PluginBlock();
            alignedFree(mem);
            throw;
        }

        // 自定义删除器：捕获 mem（组合内存的起始地址，即 block 本身），
        // 按“先构造的后析构”的顺序：先析构 Plugin 子对象，再析构 PluginBlock，最后释放整块内存。
        // 该 lambda 定义在成员函数内部，对 PluginBlock 的私有成员（alignedFree）拥有和外层函数一致的访问权限。
        auto deleter = [mem](PluginBlock* b) noexcept {
            toPlugin(b)->~Plugin();
            b->~PluginBlock();
            alignedFree(mem);
        };

        return std::shared_ptr<PluginBlock>(block, deleter);
    }

    /**
     * @brief 从 shared_ptr<PluginBlock> 借用同一份引用计数，得到一个 shared_ptr<Plugin>。
     * @note 这是本类唯一推荐的“拿到 shared_ptr<Plugin>”的方式；get() 只返回裸指针，
     *       仅供已经确保 PluginBlock 存活期间的内部短生命周期访问使用。
     */
    static std::shared_ptr<Plugin> pluginOf(const std::shared_ptr<PluginBlock>& block)
    {
        if (!block) {
            return nullptr;
        }
        return {block, block->plugin()};
    }

    /// 返回一个指向插件对象指针所对应的插件块的指针
    static PluginBlock* fromPlugin(const Plugin* ptr) noexcept
    {
        return reinterpret_cast<PluginBlock*>(internalId(ptr) - PluginBlock::allocation_size);
    }

    /// 从插件块指针返回指向插件对象的指针
    static Plugin* toPlugin(const PluginBlock* ptr) noexcept
    {
        return reinterpret_cast<Plugin*>(internalId(ptr) + PluginBlock::allocation_size);
    }

    std::size_t id() const noexcept { return m_id; }

    /**
     * @brief 非拥有的裸指针访问；生命周期由调用方持有的 shared_ptr<PluginBlock> 保证。
     * @note 将 get() 替换成 plugin 以与 std::shared_ptr 的 get 区分，免得混淆且语义更明确。
     */
    Plugin* plugin() const noexcept { return toPlugin(this); }

    size_t hash() const noexcept { return static_cast<size_t>(m_id); }

private:
    explicit PluginBlock(size_t id)
        : m_id(id)
    {
    }

    ~PluginBlock() = default; // 只能通过 create() 返回的 shared_ptr 的自定义删除器析构

    template<class Managed>
    static void* allocate()
    {
        static_assert(std::is_base_of_v<Plugin, Managed>);
        constexpr size_t alloc_size = PluginBlock::allocation_size + sizeof(Managed);
        return alignedAlloc(PluginBlock::alignment_size, alloc_size);
    }

    template<class... Args>
    static PluginBlock* constructBlock(void* mem, Args&&... args)
    {
        auto* ptr = new (mem) PluginBlock(std::forward<Args>(args)...);
        if (internalId(ptr) != internalId(mem)) {
            throw std::runtime_error("PluginBlock: constructed plugin block at misaligned address");
        }
        return ptr;
    }

    template<class Managed, class... Args>
    static Managed* constructManaged(void* block, Args&&... args)
    {
        static_assert(std::is_base_of_v<Plugin, Managed>);
        auto* mem = reinterpret_cast<std::byte*>(block) + PluginBlock::allocation_size;
        auto* ptr = new (mem) Managed(std::forward<Args>(args)...);
        if (internalId(ptr) != internalId(mem)) {
            throw std::runtime_error("PluginBlock: constructed plugin block at misaligned address");
        }
        if constexpr (!std::is_same_v<Managed, Plugin>) {
            const Plugin* base_ptr = ptr;
            if (internalId(base_ptr) != internalId(ptr)) {
                throw std::runtime_error(
                    "PluginBlock: incompatible memory layout between base and derived class");
            }
        }
        return ptr;
    }

    static uintptr_t internalId(const void* ptr) noexcept
    {
        return reinterpret_cast<uintptr_t>(ptr);
    }

    static void* alignedAlloc(size_t alignment, size_t size)
    {
        void* ptr = nullptr;
#ifdef _MSC_VER
        // Windows (MSVC): _aligned_malloc(size, alignment)
        // Note: _aligned_malloc 并不要求大小必须是对齐值的倍数
        ptr = _aligned_malloc(size, alignment);
#else
        // Note: size MUST be a multiple of alignment
        // 如果使用标准对齐分配，确保大小是对齐方式的倍数以保证可移植性
        if (size % alignment != 0) {
            // 调整大小，使其为对齐的倍数，以满足标准要求
            size = (size + alignment - 1) & ~(alignment - 1);
        }
        ptr = std::aligned_alloc(alignment, size);
#endif
        if (!ptr) {
            throw std::bad_alloc();
        }
        return ptr;
    }

    static void alignedFree(void* ptr) noexcept
    {
        if (ptr) {
#ifdef _MSC_VER
            _aligned_free(ptr);
#else
            std::free(ptr);
#endif
        }
    }

private:
    std::size_t m_id; // IPlugin 内部的整数 id
};

inline bool operator==(const std::shared_ptr<PluginBlock>& lhs, const Plugin* rhs) noexcept
{
    if (!rhs) {
        return !lhs;
    }
    return lhs.get() == PluginBlock::fromPlugin(rhs);
}

inline bool operator==(const Plugin* lhs, const std::shared_ptr<PluginBlock>& rhs) noexcept
{
    return rhs == lhs;
}

inline bool operator!=(const std::shared_ptr<PluginBlock>& x, const Plugin* y) noexcept
{
    return !(x == y);
}

inline bool operator!=(const Plugin* x, const std::shared_ptr<PluginBlock>& y) noexcept
{
    return !(x == y);
}

} // namespace bakuon::gui

namespace std {

template<>
struct hash<std::shared_ptr<bakuon::gui::PluginBlock>>
{
    size_t operator()(const std::shared_ptr<bakuon::gui::PluginBlock>& ptr) const noexcept
    {
        return ptr ? ptr->hash() : 0;
    }
};

template<>
struct hash<std::weak_ptr<bakuon::gui::PluginBlock>>
{
    size_t operator()(const std::weak_ptr<bakuon::gui::PluginBlock>& ptr) const noexcept
    {
        if (auto locked = ptr.lock()) {
            return locked->hash();
        }
        return 0;
    }
};

} // namespace std
