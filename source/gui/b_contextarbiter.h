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
    void refreshCommandStates();

private:
    std::unordered_map<ContextId, ContextInfo> m_registry;
    std::unordered_map<ContextId, std::shared_ptr<ContextState>> m_contexts;
    core::ActivationClock m_clock;
    CommandManager* m_commandManager = nullptr;
};

} // namespace bakuon::gui
