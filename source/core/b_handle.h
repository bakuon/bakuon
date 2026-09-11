#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <type_traits>

#include <entt/entity/entity.hpp>

namespace bakuon::core {

/**
 * @brief bakuon::core 的实体句柄 —— 对 entt::entity 的一层强类型薄封装。
 *
 * @details 直接把 entt::entity 暴露给消费者（gui/plugin/sandbox 等模块）意味着
 * 一旦未来更换底层 ECS 实现，所有消费者代码都要跟着改；用一个薄的强类型包装，
 * 消费者只认识 EntityId，看不到（也不需要看到）entt 本身——与 gui::Id<Tag> 的
 * 设计动机完全一致（见 source/gui/b_id.h 头部注释），只是这里区分的不是"字符串
 * 语义"而是"底层 ECS 库的具体类型"。
 *
 * @note Handle 本身是 entt::entity 的直接封装，可平凡拷贝，可安全地作为值到处
 * 传递/存储；"这个 id 指向的实体是否还存活"要靠 Registry::valid() 查询，
 * Handle 自己不持有任何生命周期信息（和 entt::entity 的语义完全一致）。
 */
class Handle
{
public:
    constexpr Handle() noexcept = default;

    /// 仅供 Registry 内部在 entt::entity <-> Handle 之间转换使用。
    explicit constexpr Handle(entt::entity raw) noexcept
        : m_raw(raw)
    {
    }

    /// entt::null 是"空实体"的哨兵值；默认构造的 Handle 恒等于它。
    [[nodiscard]] constexpr bool isValid() const noexcept { return m_raw != entt::null; }

    /// 底层 entt::entity，仅供 Registry 内部使用；不建议消费者代码直接依赖这个类型。
    [[nodiscard]] constexpr entt::entity native() const noexcept { return m_raw; }

    friend constexpr bool operator==(Handle lhs, Handle rhs) noexcept
    {
        return lhs.m_raw == rhs.m_raw;
    }
    friend constexpr bool operator!=(Handle lhs, Handle rhs) noexcept { return !(lhs == rhs); }

private:
    entt::entity m_raw{entt::null};
};

} // namespace bakuon::core

namespace std {
template<>
struct hash<bakuon::core::Handle>
{
    size_t operator()(const bakuon::core::Handle& id) const noexcept
    {
        using Underlying = std::underlying_type_t<entt::entity>;
        return std::hash<Underlying>{}(static_cast<Underlying>(id.native()));
    }
};
} // namespace std
