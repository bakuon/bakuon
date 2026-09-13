#pragma once

#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <QtCore/QObject>

#include "core/b_contextactivation.h"
#include "gui/b_contextstatus.h"
#include "gui/b_gui_export.h"

namespace bakuon::gui {

class CommandManager;

/**
 * @brief 上下文注册表 + 激活仲裁 + 命令路由。
 *
 * 激活序时钟使用 core::ActivationClock；候选比较使用 core::beats()。
 * QAction 路由与 CommandManager 推送仍留在本类。
 *
 * 刷新策略（增量优先，全量为兜底）：
 *  - push/pop：仅当 isActive / effectiveTier / activationOrder 变化时，
 *    刷新该上下文上的命令；
 *  - actionsChanged：维护 CommandId→contexts 倒排后，仅刷新受影响命令；
 *  - releaseContext：对实际发生变化的上下文收集命令并集后增量刷新；
 *  - refreshAllCommandStates()：全量兜底。
 */
class BAKUON_GUI_EXPORT ContextArbiter : public QObject
{
    Q_OBJECT
public:
    explicit ContextArbiter(QObject* parent = nullptr);
    ~ContextArbiter() override = default;

    struct ContextInfo
    {
        QString owner;
        QString description;
    };

    std::shared_ptr<ContextState> registerContext(const ContextId& ctxId, const QString& owner,
                                                  const QString& description, int priority = 0);
    void unregisterContext(const ContextId& ctxId);

    std::shared_ptr<ContextState> context(const ContextId& ctxId) const;

    std::optional<ContextInfo> contextInfo(const ContextId& ctxId) const;
    std::vector<ContextId> registeredContexts() const;

    void pushContext(const ContextId& ctxId, const void* source,
                     ContextTier tier = ContextTier::Foreground);
    void popContext(const ContextId& ctxId, const void* source,
                    ContextTier tier = ContextTier::Foreground);
    void releaseContext(const void* source);

    bool isActiveContext(const ContextId& ctxId) const noexcept;
    std::unordered_set<ContextId> activeContexts() const;
    ContextTier effectiveTier(const ContextId& ctxId) const noexcept;
    uint64_t activationOrder(const ContextId& ctxId) const noexcept;

    std::vector<ContextId> contextsForCommand(const CommandId& cmdId) const;

    [[nodiscard]] QAction* findActiveAction(const CommandId& cmdId) const;

    void setCommandManager(CommandManager* manager) { m_commandManager = manager; }
    CommandManager* commandManager() const noexcept { return m_commandManager; }

private:
    std::shared_ptr<ContextState> ensureContext(const ContextId& ctxId);

    /** 全量：所有已注册 Command 重新仲裁（兜底）。 */
    void refreshAllCommandStates();

    /** 增量：仅对给定命令集合重新仲裁并 setRealAction。 */
    void refreshCommands(const std::vector<CommandId>& cmdIds);
    void refreshCommands(const std::unordered_set<CommandId>& cmdIds);

    /** 该上下文上命令集合变化时：重建倒排并增量刷新。 */
    void onActionsChanged(const ContextId& ctxId);

    /** 从倒排中移除某上下文，返回其曾关联的全部 CommandId。 */
    std::vector<CommandId> unindexContext(const ContextId& ctxId);

    /** 用 ContextState::commandIds() 重建该上下文的倒排条目。 */
    void indexContext(const ContextId& ctxId, const std::vector<CommandId>& cmdIds);

    /** 仲裁相关快照：用于判断 push/pop/release 后是否需要刷新。 */
    struct ActivationSnapshot
    {
        bool active         = false;
        ContextTier tier    = ContextTier::Foreground;
        std::uint64_t order = 0;

        bool operator==(const ActivationSnapshot&) const = default;
    };

    static ActivationSnapshot snapshotOf(const ContextState& state) noexcept
    {
        return {state.isActive(), state.effectiveTier(), state.activationOrder()};
    }

private:
    std::unordered_map<ContextId, ContextInfo> m_registry;
    std::unordered_map<ContextId, std::shared_ptr<ContextState>> m_contexts;

    /** CommandId → 注册了该命令的上下文集合（倒排，加速 findActiveAction）。 */
    std::unordered_map<CommandId, std::unordered_set<ContextId>> m_commandContexts;

    /** ContextId → 当前倒排中的命令集合（与 ContextState 镜像，便于 diff）。 */
    std::unordered_map<ContextId, std::unordered_set<CommandId>> m_contextCommands;

    core::ActivationClock m_clock;
    CommandManager* m_commandManager = nullptr;
};

} // namespace bakuon::gui
