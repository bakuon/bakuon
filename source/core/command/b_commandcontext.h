#pragma once

#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "core/b_contextactivation.h"
#include "core/b_entity.h"
#include "core/command/b_command.h"

namespace bakuon::core::command {

// ============================================================================
// 命令上下文的纯数据表示 + 仲裁。
//
// 这一层是从旧 gui::ContextArbiter / gui::ContextState 里"去 Qt 化"抽出来的：
// 激活引用计数/层级/仲裁序完全委托给已经是纯 C++ 的 core::ContextActivation /
// core::ActivationClock / core::beats()（b_contextactivation.h）——本文件不
// 重新发明这部分数学，只是把"上下文是一个 Entity""上下文登记了哪些命令"
// 这两件事补上，让仲裁结果能落到具体的 Command Entity 上。
//
// GUI 层（gui::ContextArbiter 的后继）保留的职责：
//  - 给每个 (context, commandId) 组合挂一个只有 gui 层认识的 RealAction 组件
//    （QPointer<QAction>），本类完全不知道 QAction 的存在；
//  - 订阅 Container::onUpdate<ActiveContext>()，读出新的权威 context，
//    去查那个 gui-only 的 RealAction 表，调用 Command::setRealAction()。
// ============================================================================

struct CommandContext
{
    std::string id;
    std::string owner;
    std::string description;
};

struct ContextPriority
{
    int value = 0;
};

/// 激活状态：直接复用 core::ContextActivation，不重复定义引用计数/层级语义。
struct ContextActivationState
{
    ContextActivation activation;
};

/// 该上下文登记了哪些命令 id——供 GUI 侧反查"这个上下文影响哪些命令"，
/// 也是仲裁时"命令 -> 候选上下文"倒排索引的正向数据来源。
struct ContextCommands
{
    std::unordered_set<std::string> commandIds;
};

/**
 * @brief Context 的 id <-> Entity 索引 + push/pop 激活 + 仲裁。
 * @details gui::ContextArbiter like
 *
 * 依赖一个已经存在的 CommandRegistry（同一个 Registry 上）——仲裁结果最终要
 * 写回某个 Command Entity 的 ActiveContext 组件，本类需要能按 commandId 查到
 * 那个 Command Entity。
 *
 * 不可拷贝/不可移动，理由与 CommandRegistry 一致。
 */
class ContextRegistry
{
public:
    ContextRegistry(Registry& registry, CommandRegistry& commands);
    ~ContextRegistry();

    ContextRegistry(const ContextRegistry&)            = delete;
    ContextRegistry& operator=(const ContextRegistry&) = delete;
    ContextRegistry(ContextRegistry&&)                 = delete;
    ContextRegistry& operator=(ContextRegistry&&)      = delete;

    /// 幂等注册；owner 冲突检测（同一 id 被不同 owner 声明）留给调用方按需处理——
    /// 本类只负责数据结构本身，"冲突要不要拒绝、要不要打日志"是策略决定，
    /// 见旧 gui::ContextArbiter::registerContext() 的 qWarning，那部分应留在 gui。
    Entity registerContext(std::string_view id, std::string owner, std::string description,
                           int priority = 0);

    [[nodiscard]] Entity find(std::string_view id) const noexcept;
    [[nodiscard]] bool contains(std::string_view id) const noexcept;
    [[nodiscard]] std::vector<Entity> all() const;

    /// 把命令 id 关联进某个上下文；重复调用是幂等的。
    void bindCommand(Entity context, std::string_view commandId);
    void unbindCommand(Entity context, std::string_view commandId);

    void pushContext(Entity context, const void* source, ContextTier tier = ContextTier::Foreground);
    void popContext(Entity context, const void* source, ContextTier tier = ContextTier::Foreground);
    /// 释放某个 source 在所有上下文上持有的全部引用（对应旧 releaseContext()）。
    void releaseSource(const void* source);

    [[nodiscard]] bool isActive(Entity context) const noexcept;
    [[nodiscard]] ContextTier effectiveTier(Entity context) const noexcept;
    [[nodiscard]] std::uint64_t activationOrder(Entity context) const noexcept;

    /**
     * @brief 对给定命令 id 重新仲裁：在登记了该命令、且当前激活的上下文里，
     *        按 (tier, priority, activationOrder) 选出权威上下文，写回对应
     *        Command Entity 的 ActiveContext 组件（emplace_or_replace，
     *        因此总是触发 onConstruct 或 onUpdate），并返回该上下文
     *        （无权威上下文时返回 nullentity）。
     */
    Entity arbitrate(std::string_view commandId);

    /// 找出当前对给定命令具备权威的 context（不写回 ActiveContext，纯查询）。
    [[nodiscard]] Entity findActiveContext(std::string_view commandId) const;

private:
    void refreshCommands(const std::unordered_set<std::string>& commandIds);

private:
    Registry& m_registry;
    CommandRegistry& m_commands;
    std::unordered_map<std::string, Entity> m_idToEntity;
    /// commandId -> 登记了它的 context 集合（仲裁倒排索引）。
    std::unordered_map<std::string, std::unordered_set<Entity>> m_commandToContexts;
    ActivationClock m_clock;
};

} // namespace bakuon::core::command
