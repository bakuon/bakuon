#pragma once

#include <optional>
#include <unordered_map>
#include <unordered_set>

#include <QtCore/QObject>

#include "gui/b_contextstatus.h"
#include "gui/b_gui_export.h"

namespace bakuon::gui {

class CommandManager;

/**
 * @brief 上下文注册表 + 激活仲裁 + 命令路由。
 *
 * 取代原来的 ContextTracker。核心变化：原来 ContextTracker 只管"哪些 context id
 * 当前激活"，对 QAction/Command 一无所知，Command 自己拿仲裁结果去查
 * m_bindings；现在 ContextArbiter 直接持有每个上下文的 ContextState（其中就有
 * CommandId -> QAction 的注册表），仲裁出结果后直接调用 Command::setRealAction()
 * 把答案"推"给它——Command 不再需要认识 ContextArbiter/ContextState 这些类型，
 * 也不需要主动查询任何东西。
 *
 * ## 谁拥有 ContextState
 * registerContext()/context() 返回 shared_ptr<ContextState>，调用方（通常是
 * 声明了某个上下文的模块/插件）应该保留这个指针，后续直接对它调用
 * addAction()/removeAction() 来注册实际的 QAction，不需要每次都回来问
 * ContextArbiter 要。激活状态变更（push/pop）仍然要通过 ContextArbiter，
 * 因为 activationOrder 是跨全部上下文共享的全局时钟。
 *
 * ## 与 CommandManager 的关联是可选的
 * 不调用 setCommandManager() 时，ContextArbiter 完全可以独立工作（比如只用来测试
 * "上下文激活/仲裁"这部分逻辑，不需要构造任何 Command）；调用之后，push/pop/
 * 注册动作等任何可能改变路由结果的操作都会自动对 CommandManager::allCommands()
 * 里的每一个 Command 调用 setRealAction()。
 */
class BAKUON_GUI_EXPORT ContextArbiter : public QObject
{
    Q_OBJECT
public:
    explicit ContextArbiter(QObject* parent = nullptr);
    ~ContextArbiter() override = default;

    // 一个上下文 id 的元信息：由谁登记、用于人类阅读的描述。语义和之前 ContextTracker 里
    // 的 ContextInfo 完全一致（含冲突判定规则），只是现在挂在 ContextArbiter 名下。
    struct ContextInfo
    {
        QString owner;
        QString description;
    };

    /**
     * 显式登记一个上下文 id 并返回对应的运行期 ContextState（拿到后可以直接
     * ->addAction() 注册动作）。冲突判定规则与之前 ContextTracker::registerContext()
     * 完全一致：
     *   - id 尚未登记                 -> 正常登记，返回有效指针
     *   - id 已登记，owner 相同        -> 视为幂等更新（刷新 description/priority），返回有效指针
     *   - id 已登记，owner 不同        -> 真实的命名冲突，拒绝注册，qWarning 报出双方 owner，
     *                                     返回 nullptr（不修改已有登记信息）
     */
    std::shared_ptr<ContextState> registerContext(const ContextId& ctxId, const QString& owner,
                                                  const QString& description, int priority = 0);
    // 连带释放该上下文持有的全部动作注册和激活引用计数；不做"先逐个 release 再删"的
    // 复杂语义，调用方如果需要干净收尾应该自己先处理还持有引用的 source。
    void unregisterContext(const ContextId& ctxId);

    std::shared_ptr<ContextState> context(const ContextId& ctxId) const;

    std::optional<ContextInfo> contextInfo(const ContextId& ctxId) const;
    std::vector<ContextId> registeredContexts() const;

    /**
     * 维护"激活上下文集合"。未登记过的 id 会自动创建一个匿名 ContextState（owner 未知）
     * 并 qWarning 提示——保持行为宽松，不能因为漏调 registerContext() 就让 push 静默失效，
     * 这一点与之前 ContextTracker 的宽松策略一致。
     */
    void pushContext(const ContextId& ctxId, const void* source,
                     ContextTier tier = ContextTier::Foreground);
    void popContext(const ContextId& ctxId, const void* source,
                    ContextTier tier = ContextTier::Foreground);
    // 某个 source 生命周期结束时（如控件析构），一次性释放其在全部上下文里持有的引用
    void releaseContext(const void* source);

    bool isActiveContext(const ContextId& ctxId) const noexcept;
    std::unordered_set<ContextId> activeContexts() const;
    ContextTier effectiveTier(const ContextId& ctxId) const noexcept;
    uint64_t activationOrder(const ContextId& ctxId) const noexcept;

    // 某条命令当前在哪些上下文里注册了动作（不代表这些上下文都激活），
    // 用于 UI 展示，例如 CommandModel 的"上下文"列。
    std::vector<ContextId> contextsForCommand(const CommandId& cmdId) const;

    /**
     * @brief 核心路由：在所有当前激活的上下文里，找出这条命令应该使用的 QAction。
     * 规则与之前 Command::findAuthoritativeIndex() 完全一致：层级 > 优先级 > 激活时序，
     * 层级比较严格占先。没有任何激活上下文为该命令注册动作时返回 nullptr。
     */
    [[nodiscard]] QAction* findActiveAction(const CommandId& cmdId) const;

    void setCommandManager(CommandManager* manager) { m_commandManager = manager; }
    CommandManager* commandManager() const noexcept { return m_commandManager; }

private:
    std::shared_ptr<ContextState> ensureContext(const ContextId& ctxId);
    // 任何可能改变某条命令路由结果的操作之后调用；未关联 CommandManager 时是 no-op。
    void refreshCommandStates();

private:
    std::unordered_map<ContextId, ContextInfo> m_registry;
    std::unordered_map<ContextId, std::shared_ptr<ContextState>> m_contexts;
    uint64_t m_activationClock       = 0;
    uint64_t m_backgroundClock       = 0; // 两套独立计数，Background 跨层仍按 tier 仲裁
    CommandManager* m_commandManager = nullptr;
};

} // namespace bakuon::gui
