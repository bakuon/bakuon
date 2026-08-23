#pragma once

#include <cstdlib>
#include <memory>
#include <new> // for std::bad_alloc if needed, though aligned_alloc returns nullptr

#ifdef _MSC_VER
#include <malloc.h> // For _aligned_malloc and _aligned_free on Windows
#endif

#include "gui/b_plugin.h"

namespace bakuon::gui {

#define CACHE_LINE_SIZE 64

// class Plugin;

/**
 * PluginBlock 与 Plugin (不是 IPlugin，命名是否需要调整以防认错？)对齐存储，
 * 相当于是 Plugin 的引用指针 std::shared_ptr。通过 PluginBlock 可以访问 Plugin。
 * 
 * +-----------------+------------------+
 * |     block       |    plugin (T)    |
 * +-----------------+------------------+
 * |      data       |       data       |
 * +-----------------+------------------+
 *
 * 外部存储表只需要整数的原子自增 id 作为 key，PluginBlock 共享指针作为值为存储，
 * 同时也支持插件接口 IPlugin::id 的字符串作为 key 的另一个存储容器，字符串查询极少用到但也需要支持。
 * 通过 PluginBlock 地址偏移可以找到 Plugin 对象，如果 Plugin 对象为 nullptr，说明插件动态库已经卸载。
 *
 * @note 当前只是粗略实现，命名和实现逻辑需要优化。
 */
class PluginBlock
{
public:
    using managed_type = Plugin;

    // 定义为插件块及其插件（管理的）对象分配的内存区域的对齐方式。
    static constexpr size_t allocation_size = CACHE_LINE_SIZE;
    static constexpr size_t alignment_size  = CACHE_LINE_SIZE;

    ~PluginBlock() = default;

    PluginBlock(const PluginBlock&)            = delete;
    PluginBlock& operator=(const PluginBlock&) = delete;

    std::size_t id() const noexcept { return m_id; }

    managed_type* get() const noexcept { return toPlugin(this); }

    /// Returns a pointer to the plugin block from a plugin object pointer.
    static PluginBlock* fromPlugin(const managed_type* ptr) noexcept
    {
        return reinterpret_cast<PluginBlock*>(internalId(ptr) - PluginBlock::allocation_size);
    }

    /// Returns a pointer to the plugin object from a plugin block pointer.
    static managed_type* toPlugin(const PluginBlock* ptr) noexcept
    {
        return reinterpret_cast<managed_type*>(internalId(ptr) + PluginBlock::allocation_size);
    }

    template<class Managed>
    static void* allocate()
    {
        static_assert(std::is_base_of_v<managed_type, Managed>);
        constexpr size_t alloc_size = PluginBlock::allocation_size + sizeof(Managed);
        auto* mem                   = alignedAlloc(PluginBlock::alignment_size, alloc_size);
        if (mem == nullptr) {
            throw std::bad_alloc(); // "failed to allocate aligned memory"
        }
        return mem;
    }

    template<class... Args>
    static PluginBlock* construct_block(void* mem, Args&&... args)
    {
        auto* ptr = new (mem) PluginBlock(std::forward<Args>(args)...);
        if (internalId(ptr) != internalId(mem)) {
            throw std::exception("constructed plugin block at misaligned address");
        }
        return ptr;
    }

    template<class Managed, class... Args>
    static Managed* construct_managed(void* block, Args&&... args)
    {
        static_assert(std::is_base_of_v<managed_type, Managed>);
        auto* mem = reinterpret_cast<std::byte*>(block) + PluginBlock::allocation_size;
        auto* ptr = new (mem) Managed(std::forward<Args>(args)...);
        if (internalId(ptr) != internalId(mem)) {
            throw std::exception("constructed plugin block at misaligned address");
        }
        if constexpr (!std::is_same_v<Managed, managed_type>) {
            const managed_type* base_ptr = ptr;
            if (internalId(base_ptr) != internalId(ptr)) {
                throw std::exception("incompatible memory layout between base and derived class");
            }
        }
        return ptr;
    }

    size_t hash() const noexcept
    {
        if constexpr (sizeof(size_t) == sizeof(std::size_t)) {
            return static_cast<size_t>(m_id);
        } else {
            std::hash<std::size_t> hasher;
            return hasher(m_id);
        }
    }

private:
    explicit PluginBlock(size_t id)
        : m_id(id)
    {
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
            throw std::bad_alloc(); // Optional: Convert nullptr to exception if desired
        }
        return ptr;
    }

    static void alignedFree(void* ptr)
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
    std::weak_ptr<Plugin> m_plugin;
};

inline bool operator==(const std::shared_ptr<PluginBlock>& lhs, const Plugin* rhs) noexcept
{
    return lhs.get() == PluginBlock::fromPlugin(rhs);
}

inline bool operator==(const Plugin* lhs, const std::shared_ptr<PluginBlock>& rhs) noexcept
{
    return PluginBlock::fromPlugin(lhs) == rhs.get();
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
        return ptr.lock()->hash();
    }
};

} // namespace std
