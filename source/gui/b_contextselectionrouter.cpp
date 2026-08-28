#include "gui/b_contextselectionrouter.h"

#include <QtCore/QItemSelectionModel>

#include "gui/b_commandsystem.h"

namespace bakuon::gui {

ContextSelectionRouter::ContextSelectionRouter(const ContextId& context, QObject* parent,
                                               ContextTier tier)
    : QObject(parent)
    , m_context(context)
    , m_tier(tier)
{
}

ContextSelectionRouter::~ContextSelectionRouter()
{
    // 析构时如果仍处于"已选中"状态，主动 pop 掉——不能依赖调用方在对象销毁前
    // 记得调用一次 setSelected(false)，否则会遗留一份永远不会再被 pop 的引用。
    if (m_selected) {
        CommandSystem::popContext(m_context, this, m_tier);
        m_selected = false;
    }
}

void ContextSelectionRouter::setSelected(bool selected)
{
    if (selected == m_selected) {
        return; // 状态未变化，保持幂等，避免把重复调用误当成一次新的 push/pop
    }
    if (selected) {
        CommandSystem::pushContext(m_context, this, m_tier);
    } else {
        CommandSystem::popContext(m_context, this, m_tier);
    }
    m_selected = selected;
}

void ContextSelectionRouter::route(QItemSelectionModel* selectionModel)
{
    QObject::disconnect(m_selectionConnection);

    if (!selectionModel) {
        setSelected(false);
        return;
    }

    m_selectionConnection = connect(selectionModel,
                                    &QItemSelectionModel::selectionChanged,
                                    this,
                                    [this, selectionModel] {
                                        setSelected(!selectionModel->selectedIndexes().isEmpty());
                                    });
    // 选择模型被销毁时视为取消选中，避免遗留激活引用。
    connect(selectionModel, &QObject::destroyed, this, [this]() {
        m_selectionConnection = {};
        setSelected(false);
    });

    setSelected(!selectionModel->selectedIndexes().isEmpty());
}

} // namespace bakuon::gui
