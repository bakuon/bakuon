#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>

namespace bakuon::core {

/**
 * @brief bakuon::core 的实体句柄 —— 对 entt::entity 不透明的句柄标识(Opaque Handle)。
 *
 * @details 直接把 entt::entity 暴露给消费者（gui/plugin/sandbox 等模块）意味着
 * 一旦未来更换底层 ECS 实现，所有消费者代码都要跟着改；用一个薄的强类型包装，
 * 消费者只认识 Handle ，看不到（也不需要看到）entt 本身——与 gui::Id<Tag> 的
 * 设计动机完全一致（见 source/gui/b_id.h 头部注释），只是这里区分的不是"字符串
 * 语义"而是"底层 ECS 库的具体类型"。
 *
 * @note Handle 本身是 entt::entity 的直接封装，可平凡拷贝，可安全地作为值到处
 * 传递/存储；"这个 id 指向的实体是否还存活"要靠 Registry::valid() 查询，
 * Handle 自己不持有任何生命周期信息（和 entt::entity 的语义完全一致）。
 *
 * 外部类型安全封装（Type-Safe Wrapper）：
 * - 定义领域特定的实体句柄（Opaque Handles）： WidgetHandle/FileHandle/SceneHandle
 * - 领域管理器（封装 Registry）
 */
template<typename Tag>
class BasicHandle final
{
public:
    using value_type                 = std::uint32_t;                          // entt::entity
    static constexpr value_type null = std::numeric_limits<value_type>::max(); // eq ~value_type{0};

    constexpr BasicHandle() noexcept = default;

    /// 仅供 Registry 内部在 entt::entity <-> Handle 之间转换使用。
    explicit constexpr BasicHandle(value_type raw) noexcept
        : m_raw(raw)
    {
    }

    /// null == entt::null 是"空实体"的哨兵值；默认构造的 Handle 恒等于它。
    [[nodiscard]] constexpr bool isNull() const noexcept { return m_raw != null; }

    /// 底层 entt::entity，仅供 Registry 内部使用；不建议消费者代码直接依赖这个类型。
    [[nodiscard]] constexpr value_type native() const noexcept { return m_raw; }

    constexpr explicit operator bool() const noexcept { return isNull(); }

    friend constexpr bool operator==(BasicHandle lhs, BasicHandle rhs) noexcept
    {
        return lhs.m_raw == rhs.m_raw;
    }
    friend constexpr bool operator!=(BasicHandle lhs, BasicHandle rhs) noexcept
    {
        return !(lhs == rhs);
    }

private:
    value_type m_raw{null};
    value_type m_generation{0};
};

} // namespace bakuon::core

// 为 std::unordered_map / std::unordered_set 提供 std::hash 特化，
// 使 Handle 可以直接作为这些容器的 key 使用（与 gui::Id<Tag> 的既有惯例一致）。
namespace std {
template<typename Tag>
struct hash<bakuon::core::BasicHandle<Tag>>
{
    size_t operator()(const bakuon::core::BasicHandle<Tag>& h) const noexcept
    {
        using underlying = bakuon::core::BasicHandle<Tag>::value_type;
        return std::hash<underlying>{}(static_cast<underlying>(h.native()));
    }
};
} // namespace std
