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
    auto it = m_contexts.find(id);
    if (it == m_contexts.end()) {
        return;
    }

    // 先记下该上下文上的命令，再拆倒排、再擦状态，最后只刷这些命令
    const std::vector<CommandId> affected = unindexContext(id);
    m_contexts.erase(it);
    refreshCommands(affected);
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

    const ActivationSnapshot before = snapshotOf(*state);
    if (state->retain(source, tier)) {
        // 从未激活 → 激活：分配本层单调序
        state->setActivationOrder(m_clock.next(tier));
    }
    const ActivationSnapshot after = snapshotOf(*state);

    // 仅当激活态 / 有效层级 / 序发生变化时需要重仲裁
    // （重复 retain 同 (source,tier)、或已激活时再挂同层且 tier 不变 → 跳过）
    if (before != after) {
        refreshCommands(state->commandIds());
    }
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

    ContextState& state             = *it->second;
    const ActivationSnapshot before = snapshotOf(state);
    state.release(source, tier);
    const ActivationSnapshot after = snapshotOf(state);

    if (before != after) {
        refreshCommands(state.commandIds());
    }
}

void ContextArbiter::releaseContext(const void* source)
{
    if (!source) {
        return;
    }

    std::unordered_set<CommandId> dirty;
    for (auto& [id, state] : m_contexts) {
        const ActivationSnapshot before = snapshotOf(*state);
        state->releaseAll(source);
        if (before != snapshotOf(*state)) {
            for (const CommandId& cmdId : state->commandIds()) {
                dirty.insert(cmdId);
            }
        }
    }
    refreshCommands(dirty);
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
    auto it = m_commandContexts.find(cmdId);
    if (it == m_commandContexts.end()) {
        return result;
    }
    result.reserve(it->second.size());
    for (const ContextId& id : it->second) {
        result.push_back(id);
    }
    return result;
}

QAction* ContextArbiter::findActiveAction(const CommandId& cmdId) const
{
    core::ContextArbitrationKey best{};
    QAction* bestAction = nullptr;
    bool hasBest        = false;

    auto indexIt = m_commandContexts.find(cmdId);
    if (indexIt == m_commandContexts.end()) {
        return nullptr;
    }

    for (const ContextId& id : indexIt->second) {
        auto ctxIt = m_contexts.find(id);
        if (ctxIt == m_contexts.end()) {
            continue;
        }
        const ContextState& state = *ctxIt->second;
        if (!state.isActive()) {
            continue;
        }
        QAction* action = state.action(cmdId);
        if (!action) {
            continue;
        }

        const core::ContextArbitrationKey key{state.effectiveTier(),
                                              state.priority(),
                                              state.activationOrder()};
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
    // 动表变化 → 增量维护倒排并只刷相关命令（不再全表刷新）
    connect(state.get(), &ContextState::actionsChanged, this, [this, id]() {
        onActionsChanged(id);
    });
    return state;
}

void ContextArbiter::onActionsChanged(const ContextId& ctxId)
{
    auto it = m_contexts.find(ctxId);
    if (it == m_contexts.end()) {
        return;
    }

    const std::vector<CommandId> newIds = it->second->commandIds();

    // dirty = 旧集合 ∪ 新集合（移除的命令也要重仲裁，可能改走其它上下文）
    std::unordered_set<CommandId> dirty;
    if (auto oldIt = m_contextCommands.find(ctxId); oldIt != m_contextCommands.end()) {
        dirty.insert(oldIt->second.begin(), oldIt->second.end());
    }
    dirty.insert(newIds.begin(), newIds.end());

    unindexContext(ctxId);
    indexContext(ctxId, newIds);
    refreshCommands(dirty);
}

std::vector<CommandId> ContextArbiter::unindexContext(const ContextId& ctxId)
{
    std::vector<CommandId> removed;
    auto ctxCmdIt = m_contextCommands.find(ctxId);
    if (ctxCmdIt == m_contextCommands.end()) {
        return removed;
    }

    removed.reserve(ctxCmdIt->second.size());
    for (const CommandId& cmdId : ctxCmdIt->second) {
        removed.push_back(cmdId);
        auto cmdIt = m_commandContexts.find(cmdId);
        if (cmdIt == m_commandContexts.end()) {
            continue;
        }
        cmdIt->second.erase(ctxId);
        if (cmdIt->second.empty()) {
            m_commandContexts.erase(cmdIt);
        }
    }
    m_contextCommands.erase(ctxCmdIt);
    return removed;
}

void ContextArbiter::indexContext(const ContextId& ctxId, const std::vector<CommandId>& cmdIds)
{
    auto& set = m_contextCommands[ctxId];
    set.clear();
    for (const CommandId& cmdId : cmdIds) {
        set.insert(cmdId);
        m_commandContexts[cmdId].insert(ctxId);
    }
    if (set.empty()) {
        m_contextCommands.erase(ctxId);
    }
}

void ContextArbiter::refreshCommands(const std::vector<CommandId>& cmdIds)
{
    if (cmdIds.empty()) {
        return;
    }
    if (!m_commandManager) {
        static uint8_t times = 0;
        if (times < 1) {
            ++times;
            qWarning() << "Did you forget to set up the CommandManager with "
                          "ContextArbiter::setCommandManager()";
        }
        return;
    }
    for (const CommandId& cmdId : cmdIds) {
        if (Command* cmd = m_commandManager->command(cmdId)) {
            cmd->setRealAction(findActiveAction(cmdId));
        }
    }
}

void ContextArbiter::refreshCommands(const std::unordered_set<CommandId>& cmdIds)
{
    if (cmdIds.empty()) {
        return;
    }
    if (!m_commandManager) {
        static uint8_t times = 0;
        if (times < 1) {
            ++times;
            qWarning() << "Did you forget to set up the CommandManager with "
                          "ContextArbiter::setCommandManager()";
        }
        return;
    }
    for (const CommandId& cmdId : cmdIds) {
        if (Command* cmd = m_commandManager->command(cmdId)) {
            cmd->setRealAction(findActiveAction(cmdId));
        }
    }
}

void ContextArbiter::refreshAllCommandStates()
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
