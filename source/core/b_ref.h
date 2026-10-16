#pragma once

#include <concepts>
#include <type_traits>
#include <utility>

#include <bakuon/core/Handle.h>

#include "core/b_entity.h"

// ============================================================================
// 句柄分层（所有 System 共用）
//
//   插件侧（对外，entt 不可见）   BasicHandle<Tag>        4 字节，可跨动态库、可存储、可比较
//        │  只在 System 的 public 接口边界做一次转换：resolve<Ref>(registry, handle)
//        ▼
//   System 内部（"Impl"）         RefBase<Traits, Registry>  = entt::basic_handle +
//   该种类的校验/领域方法
//        │                        短命对象：registry 指针 + 实体，16
//        字节，只放栈上、不进组件、不进容器 ▼
//   组件里存放的跨实体引用         Entity（4 字节）；用到时 resolve<Ref>(registry, entity) 再升格
//
// 为什么不直接让 BasicHandle 携带 registry 指针、把方法放在句柄上：
//  - 句柄会变成 16 字节，且持有裸指针：领域（Domain）销毁后，插件手里的旧句柄变成悬空指针，
//    而不是"被识别为失效"；
//  - 句柄的方法体需要 entt/Qt 的具体类型，跨动态库的 ABI 边界会被迫暴露。
// 所以：对外永远是 BasicHandle + 接口（IXxxSystem），Ref 只存在于 System 内部。
// ============================================================================

namespace bakuon::core {

/// 句柄种类的描述。tag 对应 BasicHandle<tag>；required 是 entt::type_list<...>，
/// 列出该种类的实体必须携带的组件——它同时承担"这个实体确实是这一种类"的类型校验，
/// 所以把别的种类的句柄误传进来（或句柄版本号碰巧撞上回收的实体）会被当成无效句柄拒绝。
template<typename T>
concept RefTraits = requires {
    typename T::tag;
    typename T::required;
};

/**
 * @brief 带种类校验的实体引用：对 entt::basic_handle 的一层薄封装。
 *
 * ## "存活"的定义
 * alive() == 实体有效 && 携带 Traits::required 里的全部组件。operator bool 就是 alive()，
 * 每次调用都重新判定（而不是只在 resolve() 时判定一次），所以持有 Ref 期间组件被摘掉也能被发现。
 * 默认构造的 Ref 是"空引用"（alive() 为 false）。
 *
 * ## 用法：继承它来扩展领域方法
 * @code
 *   struct CommandTraits {
 *       using tag      = CommandHandleTag;
 *       using required = entt::type_list<CommandProxy>;
 *   };
 *   template<typename Reg>
 *   class BasicCommandRef : public core::RefBase<CommandTraits, Reg> {
 *       using Base = core::RefBase<CommandTraits, Reg>;
 *   public:
 *       using Base::Base;
 *       BasicCommandRef() = default;
 *       QAction *proxy() const { return this->template get<CommandProxy>().action.get(); }
 *   };
 *   using CommandRef      = BasicCommandRef<core::Registry>;        // 可读写
 *   using ConstCommandRef = BasicCommandRef<const core::Registry>;  // 只读：写操作无法通过编译
 * @endcode
 * Registry 的 const 性由 entt::basic_handle 在类型层面保证：ConstRef 上调用 emplace()/patch()
 * 等写操作是编译错误，而不是运行时约定。
 *
 * ## 约束
 *  - 生命周期与 Registry 引用相同：不要跨过"可能销毁 Registry"的边界持有。
 *  - 不要把 Ref 放进组件或长寿命容器；存 Entity 或 BasicHandle 。
 */
template<RefTraits Traits, typename Reg>
class RefBase
{
public:
    using traits_type   = Traits;
    using registry_type = Reg;
    using handle_type   = entt::basic_handle<Reg>;
    using opaque_type   = BasicHandle<typename Traits::tag>;

    static constexpr bool is_const = std::is_const_v<Reg>;

    RefBase() = default;
    /// 不做种类校验的包装（监听器里组件可能正处于构造/销毁途中时用）；校验由 alive() 负责。
    explicit RefBase(handle_type handle) noexcept
        : m_handle(handle)
    {
    }

    [[nodiscard]] bool alive() const noexcept
    {
        return static_cast<bool>(m_handle) && hasRequired(typename Traits::required{});
    }
    [[nodiscard]] explicit operator bool() const noexcept { return alive(); }

    [[nodiscard]] Entity entity() const noexcept { return m_handle.entity(); }
    [[nodiscard]] registry_type &registry() const noexcept { return *m_handle.registry(); }
    [[nodiscard]] handle_type handle() const noexcept { return m_handle; }
    /// 回到插件侧的不透明句柄（边界出口）；空引用得到无效句柄。
    [[nodiscard]] opaque_type opaque() const noexcept
    {
        return handleOf<opaque_type>(m_handle.entity());
    }

    // ---- 对 entt::basic_handle 的转发（返回类型随 Reg 的 const 性变化）----

    template<typename... C>
    [[nodiscard]] bool has() const
    {
        return m_handle.template all_of<C...>();
    }
    template<typename... C>
    [[nodiscard]] bool hasAny() const
    {
        return m_handle.template any_of<C...>();
    }
    template<typename C>
    [[nodiscard]] decltype(auto) get() const
    {
        return m_handle.template get<C>();
    }
    template<typename C>
    [[nodiscard]] auto tryGet() const
    {
        return m_handle.template try_get<C>();
    }
    template<typename C, typename Func>
    void patch(Func &&func) const
    {
        m_handle.template patch<C>(std::forward<Func>(func));
    }
    template<typename C, typename... Args>
    decltype(auto) emplace(Args &&...args) const
    {
        return m_handle.template emplace<C>(std::forward<Args>(args)...);
    }
    template<typename C, typename... Args>
    decltype(auto) getOrEmplace(Args &&...args) const
    {
        return m_handle.template get_or_emplace<C>(std::forward<Args>(args)...);
    }
    template<typename C>
    void remove() const
    {
        m_handle.template remove<C>();
    }
    /// 销毁实体及其全部组件。entt::basic_handle::destroy() 不是 const 成员，这里拷贝一份 16 字节的句柄调用。
    void destroy() const
    {
        handle_type copy = m_handle;
        copy.destroy();
    }

    friend bool operator==(const RefBase &lhs, const RefBase &rhs) noexcept
    {
        return lhs.m_handle == rhs.m_handle;
    }

private:
    template<typename... C>
    [[nodiscard]] bool hasRequired(entt::type_list<C...>) const
    {
        return m_handle.template all_of<C...>(); // 空 type_list 恒为 true
    }

    handle_type m_handle;
};

// ---------------------------------------------------------------------------
// 解析：边界上的唯一入口
// ---------------------------------------------------------------------------

/// 不校验的包装。用于 entt 监听器 (Registry&, Entity)：此刻组件可能还没挂全/正在销毁。
template<typename Ref>
[[nodiscard]] Ref wrap(typename Ref::registry_type &registry, Entity entity) noexcept
{
    return Ref{typename Ref::handle_type{registry, entity}};
}

/// 校验后的解析：实体有效且携带 Traits::required，否则返回空引用。
template<typename Ref>
[[nodiscard]] Ref resolve(typename Ref::registry_type &registry, Entity entity) noexcept
{
    Ref ref = wrap<Ref>(registry, entity);
    return ref ? ref : Ref{};
}

/// 插件侧句柄 → 内部引用（种类由 Ref 的 Traits::tag 在编译期绑定：传错种类的句柄无法通过编译）。
template<typename Ref>
[[nodiscard]] Ref resolve(typename Ref::registry_type &registry,
                          typename Ref::opaque_type handle) noexcept
{
    return resolve<Ref>(registry, entityOf(handle));
}

/**
 * @brief 给 System 用的 mixin：把 registry 绑定进来，使用点写成 `resolve<CommandRef>(handle)`。
 *
 * const 方法里 registry 被视为只读：只能解析 Const...Ref，试图解析可写 Ref 会在编译期报错。
 * @code
 *   class CommandSystem : ..., private core::RefResolver {
 *       CommandSystem(Registry &r) : core::RefResolver(r) {}
 *       QAction *action(CommandHandle h) const {
 *           auto cmd = resolve<ConstCommandRef>(h);     // 校验 + 解码，一行
 *           return cmd ? cmd.proxy() : nullptr;
 *       }
 *   };
 * @endcode
 */
class RefResolver
{
protected:
    explicit RefResolver(Registry &registry) noexcept
        : m_resolverRegistry(&registry)
    {
    }

    template<typename Ref, typename Key>
    [[nodiscard]] Ref resolve(Key key)
    {
        return core::resolve<Ref>(*m_resolverRegistry, key);
    }
    template<typename Ref, typename Key>
    [[nodiscard]] Ref resolve(Key key) const
    {
        static_assert(Ref::is_const, "const 成员函数里只能解析只读引用（ConstXxxRef）");
        return core::resolve<Ref>(std::as_const(*m_resolverRegistry), key);
    }

private:
    Registry *m_resolverRegistry;
};

} // namespace bakuon::core
