#include "gui/b_contextarbiter.h"

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
        qWarning() << "ContextArbiter::registerContext: naming collision on" << id
                   << "-- already owned by" << regIt->second.owner
                   << ", rejected registration attempt from" << owner;
        return nullptr;
    }

    m_registry[id] = ContextInfo{owner, description};

    auto state = ensureContext(id);
    state->setPriority(priority);
    return state;
}

void ContextArbiter::unregisterContext(const ContextId& id)
{
    m_registry.erase(id);
    if (m_contexts.erase(id) > 0) {
        refreshCommandStates();
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
        qWarning() << "ContextArbiter::pushContext: 上下文" << ctxId
                   << "从未 registerContext()，自动创建一个匿名条目（owner 未知），"
                      "建议改用 registerContext()/CommandSystem::declareContext()";
    }
    auto state = ensureContext(ctxId);
    if (state->retain(source, tier)) {
        state->setActivationOrder(m_clock.next(tier));
    }
    refreshCommandStates();
}

void ContextArbiter::popContext(const ContextId& ctxId, const void* source, ContextTier tier)
{
    Q_ASSERT(source != nullptr);

    auto it = m_contexts.find(ctxId);
    if (it == m_contexts.end()) {
        qWarning("ContextArbiter::popContext: mismatched pop (context=%s, source=%p)",
                 qPrintable(ctxId.toString()),
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
    core::ContextArbitrationKey best{};
    QAction* bestAction = nullptr;
    bool hasBest        = false;

    for (const auto& [id, state] : m_contexts) {
        if (!state->isActive()) {
            continue;
        }
        QAction* action = state->action(cmdId);
        if (!action) {
            continue;
        }

        const core::ContextArbitrationKey key{state->effectiveTier(),
                                              state->priority(),
                                              state->activationOrder()};
        if (core::beats(key, best, hasBest)) {
            bestAction = action;
            best       = key;
            hasBest    = true;
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
