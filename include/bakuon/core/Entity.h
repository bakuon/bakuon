#pragma once

// ============================================================================
// bakuon::core 门面头文件（facade）
//
// 参见 include/bakuon/gui/IPlugin.h 顶部关于门面 / 内部实现分层的说明：
// source/core/ 下的 b_ 前缀头文件是内部实现，include/bakuon/core/ 是面向消费者
// （gui/plugin/sandbox，以及未来第三方插件）的稳定转发层。
//
// 与 gui 门面的一个关键差异：core 目前完全由模板/内联组成（EnTT 本身也是
// header-only 模板库），没有跨动态库边界的符号导出问题（core 仍然是 STATIC 库），
// 因此这里的"门面"就是直接 #include 对应的内部头文件，不需要另外包一层适配代码。
// 保留这一层转发是为了让消费者始终写：
//   #include "bakuon/core/Entity.h"
// 而不是：
//   #include "core/b_entity.h"
// 一旦将来 core 的内部实现分层策略发生变化（例如引入非模板的 PIMPL 部分），
// 只需要调整这一层转发，消费者的 #include 路径不受影响。
// ============================================================================

#include "core/b_entity.h" // IWYU pragma: export

// #include <cstddef>
// #include <cstdint>
// #include <functional>
// #include <type_traits>

// namespace bakuon::core {

/**
 * @brief bakuon::core 的实体句柄 —— 对 entt::entity 的一层强类型薄封装。
 *
 * @details 直接把 entt::entity 暴露给消费者（gui/plugin/sandbox 等模块）意味着
 * 一旦未来更换底层 ECS 实现，所有消费者代码都要跟着改；用一个薄的强类型包装，
 * 消费者只认识 Handle，看不到（也不需要看到）entt 本身——与 gui::Id<Tag> 的
 * 设计动机完全一致（见 source/gui/b_id.h 头部注释），只是这里区分的不是"字符串
 * 语义"而是"底层 ECS 库的具体类型"。
 *
 * @note Entity 本身是 entt::entity 的直接封装，可平凡拷贝，可安全地作为值到处
 * 传递/存储；"这个 id 指向的实体是否还存活"要靠 Registry::valid() 查询，
 * Entity 自己不持有任何生命周期信息（和 entt::entity 的语义完全一致）。
 *
 * 外部类型安全封装（Type-Safe Wrapper）：
 * - 定义领域特定的实体句柄（Opaque Handles）： WidgetHandle/FileHandle/SceneHandle
 * - 领域管理器（封装 Registry）
 */
/*
class Entity final
{
public:
    using entity_type = std::uint32_t;
    static constexpr entity_type null = std::numeric_limits<entity_type>::max(); // eq ~entity_type{0};

    constexpr Entity(entity_type raw = null) noexcept
        : m_raw(raw)
    {
    }

    ~Entity() = default;

    constexpr Entity(const Entity &)                = default;
    constexpr Entity(Entity &&) noexcept            = default;
    constexpr Entity &operator=(const Entity &)     = default;
    constexpr Entity &operator=(Entity &&) noexcept = default;

    // EnTT 需要隐式转换
    constexpr operator entity_type() const noexcept { return m_raw; }

    friend constexpr bool operator==(Entity lhs, Entity rhs) noexcept
    {
        return lhs.m_raw == rhs.m_raw;
    }
    friend constexpr bool operator!=(Entity lhs, Entity rhs) noexcept { return !(lhs == rhs); }

private:
    entity_type m_raw{null};
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

*/
