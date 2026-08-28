#include "gui/b_contextarbiter.h"

#include <limits>

#include <QtCore/QDebug>

#include "gui/b_command.h"
#include "gui/b_commandmanager.h"

namespace bakuon::gui {

ContextArbiter::ContextArbiter(QObject* parent)
    : QObject(parent)
{
}

std::shared_ptr<ContextState> ContextArbiter::registerContext(const ContextId& id,
                                                              const QString& owner,
                                                              const QString& description,
                                                              int priority)
{
    if (!id.isValid()) {
        qWarning() << "ContextArbiter::registerContext: refuse to register an invalid (empty) "
                      "ContextId, owner:"
                   << owner;
        return nullptr;
    }

    auto regIt = m_registry.find(id);
    if (regIt != m_registry.end() && regIt->second.owner != owner) {
        // 两个不相关的调用方争用了同一个上下文字符串——真实的命名冲突，拒绝注册，
        // 不创建/不修改任何 ContextState。
        qWarning() << "ContextArbiter::registerContext: naming collision on" << id.name()
                   << "-- already owned by" << regIt->second.owner
                   << ", rejected registration attempt from" << owner;
        return nullptr;
    }

    // 新登记，或同一 owner 的幂等更新（刷新 description）。
    m_registry[id] = ContextInfo{owner, description};

    // 如果之前已经因为 pushContext() 自动创建过匿名条目，这里会复用同一个对象，
    // 不会丢失已有的激活引用计数/动作注册。
    auto state = ensureContext(id);
    state->setPriority(priority);
    return state;
}

void ContextArbiter::unregisterContext(const ContextId& id)
{
    m_registry.erase(id);
    if (m_contexts.erase(id) > 0) {
        refreshCommandStates(); // 移除的上下文里可能有某些命令当前的权威源，必须重新仲裁
    }
}

std::shared_ptr<ContextState> ContextArbiter::context(const ContextId& id) const
{
    auto it = m_contexts.find(id);
    return it != m_contexts.end() ? it->second : nullptr;
}

std::optional<ContextArbiter::ContextInfo> ContextArbiter::contextInfo(const ContextId& id) const
{
    auto it = m_registry.find(id);
    if (it == m_registry.end()) {
        return std::nullopt;
    }
    return it->second;
}

std::vector<ContextId> ContextArbiter::registeredContexts() const
{
    std::vector<ContextId> result;
    result.reserve(m_registry.size());
    for (const auto& [id, _] : m_registry) {
        result.push_back(id);
    }
    return result;
}

void ContextArbiter::pushContext(const ContextId& ctxId, const void* source, ContextTier tier)
{
    Q_ASSERT(source != nullptr);

    if (!m_contexts.contains(ctxId)) {
        qWarning() << "ContextArbiter::pushContext: 上下文" << ctxId.name()
                   << "从未 registerContext()，自动创建一个匿名条目（owner 未知），"
                      "建议改用 registerContext()/CommandSystem::declareContext()";
    }
    auto state = ensureContext(ctxId);
    if (state->retain(source, tier)) {
        // Background 不推进 Foreground 的全局时钟（见 ContextTier 注释），
        // 使用独立计数保证同层后台上下文之间仍可按激活先后比较。
        if (tier == ContextTier::Background) {
            state->setActivationOrder(++m_backgroundClock);
        } else {
            state->setActivationOrder(++m_activationClock);
        }
    }
    refreshCommandStates();
}

void ContextArbiter::popContext(const ContextId& ctxId, const void* source, ContextTier tier)
{
    Q_ASSERT(source != nullptr);

    auto it = m_contexts.find(ctxId);
    if (it == m_contexts.end()) {
        qWarning("ContextArbiter::popContext: mismatched pop (context=%s, source=%p)",
                 qPrintable(ctxId.name()),
                 source);
        return;
    }
    it->second->release(source, tier);
    refreshCommandStates();
}

void ContextArbiter::releaseContext(const void* source)
{
    if (!source) {
        return;
    }
    for (auto& [id, state] : m_contexts) {
        state->releaseAll(source);
    }
    refreshCommandStates();
}

std::unordered_set<ContextId> ContextArbiter::activeContexts() const
{
    std::unordered_set<ContextId> result;
    for (const auto& [id, state] : m_contexts) {
        if (state->isActive()) {
            result.insert(id);
        }
    }
    return result;
}

bool ContextArbiter::isActiveContext(const ContextId& ctxId) const noexcept
{
    auto it = m_contexts.find(ctxId);
    return it != m_contexts.end() && it->second->isActive();
}

ContextTier ContextArbiter::effectiveTier(const ContextId& ctxId) const noexcept
{
    auto it = m_contexts.find(ctxId);
    return it != m_contexts.end() ? it->second->effectiveTier() : ContextTier::Foreground;
}

uint64_t ContextArbiter::activationOrder(const ContextId& ctxId) const noexcept
{
    auto it = m_contexts.find(ctxId);
    return it != m_contexts.end() ? it->second->activationOrder() : 0;
}

std::vector<ContextId> ContextArbiter::contextsForCommand(const CommandId& cmdId) const
{
    std::vector<ContextId> result;
    for (const auto& [id, state] : m_contexts) {
        if (state->hasAction(cmdId)) {
            result.push_back(id);
        }
    }
    return result;
}

QAction* ContextArbiter::findActiveAction(const CommandId& cmdId) const
{
    ContextTier bestTier = ContextTier::Foreground; // 会被第一个候选无条件覆盖，初值不重要
    QAction* bestAction  = nullptr;
    int bestPriority     = std::numeric_limits<int>::min();
    uint64_t bestOrder   = 0;

    for (const auto& [id, state] : m_contexts) {
        if (!state->isActive()) {
            continue;
        }
        QAction* action = state->action(cmdId);
        if (!action) {
            continue;
        }

        const ContextTier tier = state->effectiveTier();
        const int priority     = state->priority();
        const uint64_t order   = state->activationOrder();

        const bool betterTier            = tier > bestTier;
        const bool sameTierBetterPrio    = (tier == bestTier) && (priority > bestPriority);
        const bool sameTierSamePrioNewer = (tier == bestTier) && (priority == bestPriority)
                                           && (order > bestOrder);

        if (!bestAction || betterTier || sameTierBetterPrio || sameTierSamePrioNewer) {
            bestAction   = action;
            bestTier     = tier;
            bestPriority = priority;
            bestOrder    = order;
        }
    }
    return bestAction;
}

std::shared_ptr<ContextState> ContextArbiter::ensureContext(const ContextId& id)
{
    if (auto it = m_contexts.find(id); it != m_contexts.end()) {
        return it->second;
    }
    auto state = std::make_shared<ContextState>(id);
    m_contexts.emplace(id, state);
    connect(state.get(), &ContextState::actionsChanged, this, &ContextArbiter::refreshCommandStates);
    return state;
}

void ContextArbiter::refreshCommandStates()
{
    if (!m_commandManager) {
        static uint8_t times = 0;
        if (times < 1) {
            ++times;
            // 如果 UI 没有同步更新，提醒调用者是否忘记设置了 CommandManager
            qWarning() << "Did you forget to set up the CommandManager with "
                          "ContextArbiter::setCommandManager()";
        }
        return;
    }
    for (Command* cmd : m_commandManager->allCommands()) {
        cmd->setRealAction(findActiveAction(cmd->id()));
    }
}

} // namespace bakuon::gui
