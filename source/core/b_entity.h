#pragma once

#include <entt/entt.hpp>

#include <bakuon/core/Handle.h>

/*
namespace bakuon::core {

/// 自定义的 entt 实体 —— 替换默认的 entt::entity。
class Entity final
{
public:
    /// 与 entt::entity 的 std::uint32_t 类型的枚举一致
    using value_type = std::uint32_t;

    /// 必须有这个嵌套类型, 让 EnTT 的 SFINAE 路径能识别（requires { typename Type::entity_type; }）
    using entity_type = value_type;

    /// 使用的 max 与 EnTT 默认的 entt::null 行为兼容(全 1 的 entity 位)
    static constexpr entity_type null = std::numeric_limits<entity_type>::max();

    /// 仅供内部 / traits 使用
    constexpr Entity(entity_type raw = null) noexcept
        : m_raw(raw)
    {
    }

    ~Entity() = default;

    constexpr Entity(const Entity &)                = default;
    constexpr Entity(Entity &&) noexcept            = default;
    constexpr Entity &operator=(const Entity &)     = default;
    constexpr Entity &operator=(Entity &&) noexcept = default;

    [[nodiscard]] constexpr value_type native() const noexcept { return m_raw; }

    /// EnTT 需要隐式转换：
    /// basic_entt_traits::to_integral 会调用 static_cast<entity_type>(handle)，因此 operator
    /// value_type() 必须存在
    constexpr operator value_type() const noexcept { return m_raw; }

    friend constexpr bool operator==(Entity lhs, Entity rhs) noexcept
    {
        return lhs.m_raw == rhs.m_raw;
    }
    friend constexpr bool operator!=(Entity lhs, Entity rhs) noexcept { return !(lhs == rhs); }

private:
    entity_type m_raw{null};
};

// ---------- 关键：为 Entity 提供 entt_traits ----------
struct EntityTraits
{
    using value_type   = Entity;        // 真正的 entity 类型
    using entity_type  = std::uint32_t; // 底层数值类型
    using version_type = std::uint16_t; // 版本类型（默认 12 位足够）

    // 默认 EnTT 布局（与 entt::entity 完全一致）：
    // 低 20 位 entity id，高 12 位 version
    static constexpr entity_type entity_mask  = 0xFFFFF; // 20 bits
    static constexpr entity_type version_mask = 0xFFF;   // 12 bits
};

} // namespace bakuon::core

template<>
struct std::hash<bakuon::core::Entity>
{
    [[nodiscard]] std::size_t operator()(bakuon::core::Entity entity) const noexcept
    {
        using Underlying = bakuon::core::Entity::entity_type;
        return std::hash<Underlying>{}(static_cast<Underlying>(entity));
    }
};

/// EnTT 特性特化：告知 EnTT 如何使用 Entity
/// 帮助实现 to_integral、to_entity、to_version、construct、combine 等所有必要函数
template<>
struct entt::entt_traits<bakuon::core::Entity> : entt::basic_entt_traits<bakuon::core::EntityTraits>
{
    // 自定义稀疏集合页大小
    static constexpr std::size_t page_size = 4096;
};
*/

namespace bakuon::core {

// EnTT 原生接口别名，核心实现不要直接使用 entt:: 命名空间。

[[maybe_unused]] inline constexpr auto NullEntity = entt::null;

using Entity            = entt::entity;
using Registry          = entt::basic_registry<Entity>;
using Snapshot          = entt::basic_snapshot<Registry>;
using SnapshotLoader    = entt::basic_snapshot_loader<Registry>;
using ContinuousLoader  = entt::basic_continuous_loader<Registry>;
using Dispatcher        = entt::dispatcher;
using SinkConnection    = entt::connection;
using ScopedConnection  = entt::scoped_connection;
using EntityHandle      = entt::basic_handle<Registry>;
using ConstEntityHandle = entt::basic_handle<const Registry>;

template<typename Type>
using Sink = entt::sink<Type>;

template<typename Type>
using TypeHash = entt::type_hash<Type>;

template<typename Type>
constexpr std::uint32_t typeHash() noexcept
{
    return entt::type_hash<Type>().value();
}

template<typename Type>
using TypeName = entt::type_name<Type>;

template<typename Type>
constexpr std::string_view typeName() noexcept
{
    return entt::type_name<Type>().value();
}

template<typename Type>
using TypeList = entt::type_list<Type>;

/// Handle -> Entity（内部实现用；无效句柄得到 entt::null）。
/// toEntity
template<typename Handle>
[[nodiscard]] Entity entityOf(Handle handle) noexcept
{
    return Entity{handle.native()};
}

/// Entity -> Handle（entt::null 得到无效句柄）。
/// fromEntity
template<typename Handle>
[[nodiscard]] Handle handleOf(Entity entity) noexcept
{
    // 用 entt::to_integral 最稳妥（兼容不同版本 / 自定义 traits）
    return Handle{entt::to_integral(entity)};
}

} // namespace bakuon::core
