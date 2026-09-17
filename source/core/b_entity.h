#pragma once

#include <entt/entt.hpp>

// #include <bakuon/core/Entity.h>

// enum class entity : id_type {};

/// EnTT 特性特化：告知 EnTT 如何使用 Entity
// template<>
// struct entt::entt_traits<bakuon::core::Entity>
//     : entt::entt_traits<bakuon::core::Entity::entity_type>
// {
//     using value_type = bakuon::core::Entity;

//     /// 若后续遇到 page_size / mask 相关问题，可显式补全：
//     // static constexpr std::size_t page_size = 4096;
// };

namespace bakuon::core {

// using Registry         = entt::basic_registry<entt::entity>;
// using Snapshot         = entt::basic_snapshot<Registry>;
// using SnapshotLoader   = entt::basic_snapshot_loader<Registry>;
// using ContinuousLoader = entt::basic_continuous_loader<Registry>;

// EnTT 原生接口别名，核心实现不要直接使用 entt:: 命名空间。

[[maybe_unused]] static constexpr auto nullentity = entt::null;

using Entity           = entt::entity;
using Registry         = entt::registry;
using Dispatcher       = entt::dispatcher;
using Snapshot         = entt::snapshot;
using SnapshotLoader   = entt::snapshot_loader;
using SinkConnection   = entt::connection;
using ScopedConnection = entt::scoped_connection;

template<typename Type>
using Sink = entt::sink<Type>;

using IdType = entt::id_type;

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

template<typename Entity>
[[nodiscard]] constexpr typename entt::entt_traits<Entity>::entity_type toEntity(
    const Entity value) noexcept
{
    return entt::entt_traits<Entity>::to_entity(value);
}

} // namespace bakuon::core
