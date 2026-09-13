#include "gui/b_contextstatus.h"

#include <QtGui/QAction>

namespace bakuon::gui {

ContextState::ContextState(const ContextId& id, int priority, QObject* parent)
    : QObject(parent)
    , m_id(id)
    , m_priority(priority)
{
}

void ContextState::addAction(const CommandId& cmdId, QAction* action)
{
    if (!action) {
        return;
    }

    if (auto it = m_actions.find(cmdId); it != m_actions.end() && it->second) {
        QObject::disconnect(it->second, &QObject::destroyed, this, nullptr);
    }

    m_actions[cmdId] = action;
    connect(action, &QObject::destroyed, this, [this, cmdId]() {
        m_actions.erase(cmdId);
        Q_EMIT actionsChanged();
    });

    Q_EMIT actionsChanged();
}

void ContextState::removeAction(const CommandId& cmdId)
{
    auto it = m_actions.find(cmdId);
    if (it == m_actions.end()) {
        return;
    }
    if (it->second) {
        QObject::disconnect(it->second, &QObject::destroyed, this, nullptr);
    }
    m_actions.erase(it);
    Q_EMIT actionsChanged();
}

QAction* ContextState::action(const CommandId& cmdId) const
{
    auto it = m_actions.find(cmdId);
    return it != m_actions.end() ? it->second.data() : nullptr;
}

bool ContextState::hasAction(const CommandId& cmdId) const noexcept
{
    auto it = m_actions.find(cmdId);
    return it != m_actions.end() && !it->second.isNull();
}

std::vector<CommandId> ContextState::commandIds() const
{
    std::vector<CommandId> result;
    result.reserve(m_actions.size());
    for (const auto& [cmdId, action] : m_actions) {
        if (!action.isNull()) {
            result.push_back(cmdId);
        }
    }
    return result;
}

} // namespace bakuon::gui
