#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include <bakuon/core/Bakuon.h>
#include <bakuon/core/Handle.h>

#include "gui/b_gui_export.h"

// ============================================================================
// 扩展系统的"插件侧"稳定门面：纯接口 + 类型安全的薄视图，不暴露任何 entt 类型。
//
// 说明：
//  - 没有 instance() 单例。扩展系统是 ECS 领域（core::Domain）里的一个 System 服务，
//    由 Workspace/Application 创建并持有，插件通过 PluginContext 拿到*它所在领域*的
//    IExtensionSystem*（见 b_extensionsystem.h 的使用说明）。
//  - 扩展点、扩展本身都是 registry 里的实体；这里的 Handle 只是不透明的实体标识，
//    插件无需（也无法）接触 entt。
// ============================================================================

/**
 *
 * 扩展点（Extension Point）是插件架构中的反向依赖注入机制：
 *  - 传统依赖：主程序 → 具体实现（硬编码）
 *  - 扩展点：  主程序 → 抽象接口 ← 插件实现（动态注册）
 *
 * 核心思想：主程序定义"能力槽位"（扩展点），插件填充具体实现；
 * 主程序对修改关闭、对扩展开放。
 *
 * 设计要点（开闭原则 OCP）：
 *  - 对扩展开放：
 *      1) 任意接口 T 都可通过 ExtensionSystem::registerPoint 注册一个扩展点；
 *      2) 插件只需实现接口 T 并调用 add 即可，框架对新增接口零修改；
 *  - 对修改关闭：
 *      ExtensionPoint<T> 抽象层稳定，扩展系统 ExtensionSystem 只依赖抽象接口。
 *
 * 若项目使用 Qt，可使用 qobject_interface_iid<T*>() 作为 id，
 * 也可通过 BAKUON_DECLARE_EXTENSION_IID 宏声明纯 C++ IID。
 */

namespace bakuon::gui {

/// 通用类型定义迁移到 Bakuon.h

template<typename T>
struct extension_iid
{
    static constexpr std::string_view value() noexcept { return {}; }
};

/// 在全局作用域为接口声明默认 id：BAKUON_DECLARE_EXTENSION_IID(::myapp::IEditor, "com.myapp.IEditor")
#define BAKUON_DECLARE_EXTENSION_IID(Type, IID) \
    template<> \
    struct bakuon::gui::extension_iid<Type> \
    { \
        static constexpr std::string_view value() noexcept { return IID; } \
    };

namespace extension_detail {

/// 编译期类型名：由编译器给出的函数签名（里面含有 T 的完整限定名）。
/// 只依赖标准 C++ 与编译器内建宏，不依赖任何第三方库。
template<typename T>
[[nodiscard]] constexpr std::string_view type_name() noexcept
{
#if defined(_MSC_VER)
    return __FUNCSIG__;
#else
    return __PRETTY_FUNCTION__;
#endif
}

} // namespace extension_detail

/// 接口 T 的类型键：扩展点创建时记录，之后用它校验 point<T>() 的类型是否匹配。
/// 基于类型名的内容哈希，所以宿主与各插件动态库算出的值一致，不依赖 RTTI 或地址。
/// @note 宿主和插件必须使用同一种编译器（签名格式因编译器而异）——这与它们本来就必须
///       ABI 兼容的前提一致。
template<typename T>
[[nodiscard]] constexpr std::uint64_t extension_type() noexcept
{
    return core::stringFnv1a(extension_detail::type_name<std::remove_cvref_t<T>>());
}

struct ExtensionPointTag;
struct ExtensionTag;
using ExtensionPointHandle = core::BasicHandle<ExtensionPointTag>;
using ExtensionHandle      = core::BasicHandle<ExtensionTag>;

/// 未使用，当前使用 bool 暂替
/// 懒加载扩展的实例化策略。
enum class FactoryPolicy {
    Singleton, ///< 首次解析时构造一次，之后缓存
    Transient, ///< 每次解析都调用工厂
};

template<typename T>
class ExtensionPoint;

class BAKUON_GUI_EXPORT IExtensionSystem
{
public:
    virtual ~IExtensionSystem() = default;

    /* ---------- 扩展点注册 / 注销 ---------- */

    [[nodiscard]] virtual ExtensionPointHandle registerPoint(std::string_view id,
                                                             std::string_view description,
                                                             std::uint64_t type) = 0;

    [[nodiscard]] virtual ExtensionPointHandle ensurePoint(std::string_view id,
                                                           std::string_view description,
                                                           std::uint64_t type) = 0;

    virtual bool unregisterPoint(std::string_view id) = 0;

    [[nodiscard]] virtual bool hasPoint(std::string_view id) const                  = 0;
    [[nodiscard]] virtual ExtensionPointHandle findPoint(std::string_view id) const = 0;
    [[nodiscard]] virtual std::string pointId(ExtensionPointHandle point) const     = 0;
    [[nodiscard]] virtual std::uint64_t pointType(ExtensionPointHandle point) const = 0;
    [[nodiscard]] virtual std::vector<std::string> pointIds() const                 = 0;

    // ---- 扩展 ----

    /// 注册一个已构造好的扩展实例。priority 越大越靠前，同优先级按注册先后。
    /// 同一个实例重复注册到同一扩展点 → AlreadyExists。
    [[nodiscard]] virtual ExtensionHandle add(ExtensionPointHandle point,
                                              std::shared_ptr<void> object, int priority) = 0;
    /// 注册工厂（懒实例化）。
    [[nodiscard]] virtual ExtensionHandle add(ExtensionPointHandle point,
                                              std::function<std::shared_ptr<void>()> factory,
                                              int priority, bool singleton)               = 0;

    virtual bool remove(ExtensionHandle extension)                    = 0;
    virtual bool setPriority(ExtensionHandle extension, int priority) = 0;

    [[nodiscard]] virtual std::size_t count(ExtensionPointHandle point) const                 = 0;
    /// 按优先级排好序的快照；遍历期间增删扩展是安全的。
    [[nodiscard]] virtual std::vector<ExtensionHandle> list(ExtensionPointHandle point) const = 0;
    /// 取得扩展实例（工厂扩展在此时才真正构造）；句柄失效返回空。
    [[nodiscard]] virtual std::shared_ptr<void> resolve(ExtensionHandle extension)            = 0;

    // ---- 类型安全的便捷入口 ----

    /// 取已注册的扩展点；不存在或类型不匹配返回"空视图"（explicit bool 为 false）。
    template<typename T>
    [[nodiscard]] ExtensionPoint<T> point(std::string_view id = {});

    /// 取或建类型为 T 的扩展点；id 缺省用 extension_iid<T> / qobject_interface_iid<T>。
    template<typename T>
    [[nodiscard]] ExtensionPoint<T> ensure(std::string_view id          = {},
                                           std::string_view description = {});

    /**
     * @brief 获取接口 T 的 IID，要求使用 BAKUON_DECLARE_EXTENSION_IID 声明接口
     * @tparam T 接口类型
     * @return extension_iid<T>::value() / qobject_interface_iid<T>()，若未特化返回空串
     */
    template<typename T>
    static std::string IID()
    {
        // 要求 BAKUON_DECLARE_EXTENSION_IID(...)
        return std::string(extension_iid<T>::value());
    }

    // template<typename T>
    // static std::string qt_IID()
    // {
    //     // 要求 Q_DECLARE_INTERFACE(...)
    //     return std::string(qobject_interface_iid<T*>());
    // }
};

/**
 * @brief 扩展点的强类型视图：只是 (IExtensionSystem*, 句柄) 的轻量组合，可随意拷贝。
 * @note 视图不拥有扩展点；扩展点被注销后视图里的句柄自动失效，所有查询返回空/0。
 */
template<typename T>
class ExtensionPoint
{
public:
    using ExtensionPtr = std::shared_ptr<T>;

    ExtensionPoint() = default;
    ExtensionPoint(IExtensionSystem& system, ExtensionPointHandle handle) noexcept
        : m_system(&system)
        , m_handle(handle)
    {
    }

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return m_system != nullptr && !m_handle.isNull();
    }
    [[nodiscard]] ExtensionPointHandle handle() const noexcept { return m_handle; }

    [[nodiscard]] ExtensionHandle add(ExtensionPtr extension, int priority = 0) const
    {
        if (!*this) {
            return {};
        }
        return m_system->add(m_handle, std::shared_ptr<void>(std::move(extension)), priority);
    }

    // singleton / transient(scoped)
    [[nodiscard]] ExtensionHandle add(std::function<ExtensionPtr()> factory, int priority = 0,
                                      bool singleton = true) const
    {
        if (!*this) {
            return {};
        }
        return m_system->add(
            m_handle,
            [f = std::move(factory)]() { return std::shared_ptr<void>(f()); },
            priority,
            singleton);
    }

    [[nodiscard]] std::size_t count() const { return *this ? m_system->count(m_handle) : 0; }

    /// 全部扩展（按优先级），会实例化所有懒加载扩展。
    [[nodiscard]] std::vector<ExtensionPtr> extensions() const
    {
        std::vector<ExtensionPtr> result;
        if (!*this) {
            return result;
        }
        for (const ExtensionHandle extension : m_system->list(m_handle)) {
            if (auto object = std::static_pointer_cast<T>(m_system->resolve(extension))) {
                result.push_back(std::move(object));
            }
        }
        return result;
    }

    /**
     * @brief 责任链：依次尝试，返回第一个 != R{} 的结果，否则 fallback。
     * @note 按需实例化：命中后，后面的懒加载扩展完全不会被构造。
     */
    template<typename R, typename Handler>
    [[nodiscard]] R extension(Handler&& filter, R fallback = {}) const
    {
        if (!*this) {
            return fallback;
        }
        for (const ExtensionHandle extension : m_system->list(m_handle)) {
            const auto object = std::static_pointer_cast<T>(m_system->resolve(extension));
            if (!object) {
                continue;
            }
            R r = std::invoke(filter, object);
            if (r != R{}) {
                return r;
            }
        }
        return fallback;
    }

private:
    IExtensionSystem* m_system{nullptr};
    ExtensionPointHandle m_handle{};
};

template<typename T>
ExtensionPoint<T> IExtensionSystem::point(std::string_view id)
{
    if (id.empty()) {
        id = IID<T>();
    }
    const ExtensionPointHandle handle = findPoint(id);
    if (!handle || pointType(handle) != extension_type<T>()) {
        return {};
    }
    return ExtensionPoint<T>(*this, handle);
}

template<typename T>
ExtensionPoint<T> IExtensionSystem::ensure(std::string_view id, std::string_view description)
{
    if (id.empty()) {
        id = IID<T>();
    }
    if (id.empty()) {
        return {};
    }
    const auto ensured = ensurePoint(id, description, extension_type<T>());
    return ExtensionPoint<T>(*this, ensured);
}

} // namespace bakuon::gui
