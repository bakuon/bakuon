#include "gui/b_documentsession.h"

#include "gui/b_commandsystem.h"

namespace bakuon::gui {

DocumentSession::DocumentSession(ContextId selectionContext, QObject* parent)
    : QObject(parent)
    , m_registry(std::make_unique<core::Registry>())
    , m_selection(std::make_unique<core::selection::Selection>(*m_registry))
    , m_bridge(std::make_unique<RegistryBridge>(*m_registry, this))
    , m_selectionContext(std::move(selectionContext))
{
    // 标准组件观察 + 选中集合级信号
    m_bridge->watchSelection();
    m_bridge->watch<core::components::Name>(QStringLiteral("Name"));
    m_bridge->watch<core::hierarchy::Hierarchy>(QStringLiteral("Hierarchy"));

    m_selectionConn = m_selection->onChanged([this](const core::selection::Selection&) {
        onSelectionChanged();
    });

    // 确保上下文已登记（幂等：owner 冲突仅 warning）
    CommandSystem::declareContext(m_selectionContext.toString(),
                                  QStringLiteral("bakuon.gui"),
                                  QStringLiteral("Document selection is non-empty"));
}

DocumentSession::~DocumentSession()
{
    // 先 pop 上下文，再释放 Selection/Registry，避免遗留激活引用
    if (m_selectionCtxActive) {
        CommandSystem::popContext(m_selectionContext, this);
        m_selectionCtxActive = false;
    }
    m_selectionConn.disconnect();
    // unique_ptr 析构顺序：先 bridge（仍引用 registry），再 selection，再 registry
    m_bridge.reset();
    m_selection.reset();
    m_registry.reset();
}

void DocumentSession::onSelectionChanged()
{
    syncSelectionContext();
    Q_EMIT selectionChanged();
}

void DocumentSession::syncSelectionContext()
{
    const bool shouldActive = !m_selection->empty();
    if (shouldActive == m_selectionCtxActive) {
        return;
    }
    if (shouldActive) {
        CommandSystem::pushContext(m_selectionContext, this);
    } else {
        CommandSystem::popContext(m_selectionContext, this);
    }
    m_selectionCtxActive = shouldActive;
}

} // namespace bakuon::gui
