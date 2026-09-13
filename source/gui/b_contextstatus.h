#pragma once

#include <memory>
#include <unordered_map>
#include <vector>

#include <QtCore/QObject>
#include <QtCore/QPointer>

#include "core/b_contextactivation.h"
#include "gui/b_gui_export.h"
#include "gui/b_types.h"

QT_BEGIN_NAMESPACE
class QAction;
QT_END_NAMESPACE

namespace bakuon::gui {

/**
 * @brief 一个具名上下文的运行期状态：动作注册表 + 激活引用计数。
 *
 * 激活/层级/序语义委托给 core::ContextActivation（纯 C++）；本类额外持有
 * CommandId → QAction* 与 Qt 信号，供 ContextArbiter 路由。
 *
 * retain / release / releaseAll / setActivationOrder 仅 friend ContextArbiter
 * 可调，避免外部破坏全局时钟语义。
 */
class BAKUON_GUI_EXPORT ContextState : public QObject
{
    Q_OBJECT
public:
    explicit ContextState(const ContextId& id, int priority = 0, QObject* parent = nullptr);
    ~ContextState() override = default;

    ContextId id() const noexcept { return m_id; }

    int priority() const noexcept { return m_priority; }
    void setPriority(int priority) noexcept { m_priority = priority; }

    void addAction(const CommandId& cmdId, QAction* action);
    void removeAction(const CommandId& cmdId);
    QAction* action(const CommandId& cmdId) const;
    bool hasAction(const CommandId& cmdId) const noexcept;
    std::vector<CommandId> commandIds() const;

    bool isActive() const noexcept { return m_activation.isActive(); }
    ContextTier effectiveTier() const noexcept { return m_activation.effectiveTier(); }
    uint64_t activationOrder() const noexcept { return m_activation.activationOrder(); }

    const core::ContextActivation& activation() const noexcept { return m_activation; }

Q_SIGNALS:
    void actionsChanged();

private:
    friend class ContextArbiter;

    bool retain(const void* source, ContextTier tier) { return m_activation.retain(source, tier); }
    bool release(const void* source, ContextTier tier)
    {
        return m_activation.release(source, tier);
    }
    void releaseAll(const void* source) { m_activation.releaseAll(source); }
    void setActivationOrder(uint64_t order) noexcept { m_activation.setActivationOrder(order); }

    ContextId m_id;
    int m_priority = 0;
    core::ContextActivation m_activation;
    std::unordered_map<CommandId, QPointer<QAction>> m_actions;
};

} // namespace bakuon::gui
