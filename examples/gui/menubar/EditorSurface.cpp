#include "EditorSurface.h"
#include "Constants.h"

#include <QtWidgets/QBoxLayout>
#include <QtWidgets/QMessageBox>
#include <QtWidgets/QStatusBar>

using bakuon::gui::Command;
using bakuon::gui::CommandId;

namespace bakuon::examples {

EditorSurface::EditorSurface(const QString& label, const ContextId& focusContext,
                             const ContextId& selectedContext, QWidget* parent)
    : QWidget(parent)
    , m_selectedContext(selectedContext)
    , m_focusContext(focusContext)
{
    setFocusPolicy(Qt::StrongFocus);
    setContextMenuPolicy(Qt::DefaultContextMenu);
    gui::CommandSystem::setProviderContext(this, m_focusContext);

    auto* layout = new QVBoxLayout(this);
    m_hint = new QLabel(label
                            + QStringLiteral("\n（点击选中/取消选中一个对象，右键查看上下文菜单）"),
                        this);
    m_hint->setAlignment(Qt::AlignCenter);
    layout->addWidget(m_hint);

    setupRealActions(label);
}

Context EditorSurface::context() const
{
    return Context{m_selectedContext, m_focusContext};
}

void EditorSurface::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        setFocus(Qt::MouseFocusReason);
        m_selected = !m_selected;
        if (m_selected) {
            gui::CommandSystem::pushContext(m_selectedContext, this);
        } else {
            gui::CommandSystem::popContext(m_selectedContext, this);
        }
        m_deleteAction->setEnabled(m_selected);
        m_duplicateAction->setEnabled(m_selected);
        m_hint->setText(QStringLiteral("对象状态：%1")
                            .arg(m_selected ? QStringLiteral("已选中") : QStringLiteral("未选中")));
    }

    QWidget::mousePressEvent(event);
}

void EditorSurface::contextMenuEvent(QContextMenuEvent* event)
{
    QMenu menu(this);
    menu.addAction(gui::CommandSystem::command(kCmdDelete)->action());
    menu.addAction(gui::CommandSystem::command(kCmdDuplicate)->action());
    menu.exec(event->globalPos());
}

void EditorSurface::setupRealActions(const QString& label)
{
    m_deleteAction = new QAction(QStringLiteral("删除"), this);
    m_deleteAction->setEnabled(false);
    connect(m_deleteAction, &QAction::triggered, this, [this, label]() {
        QMessageBox::information(this,
                                 QStringLiteral("删除"),
                                 QStringLiteral("已删除%1中选中的对象").arg(label));
    });

    m_duplicateAction = new QAction(QStringLiteral("复制"), this);
    m_duplicateAction->setEnabled(false);
    connect(m_duplicateAction, &QAction::triggered, this, [this, label]() {
        if (auto* mw = qobject_cast<QMainWindow*>(window())) {
            mw->statusBar()->showMessage(QStringLiteral("已复制%1对象").arg(label), 2000);
        }
    });

    gui::CommandSystem::command(kCmdDelete)->addContextAction(m_deleteAction, m_selectedContext);
    gui::CommandSystem::command(kCmdDuplicate)
        ->addContextAction(m_duplicateAction, m_selectedContext);
    gui::CommandSystem::command(kCmdDuplicate)->addContextAction(m_duplicateAction, m_focusContext);
}

} // namespace bakuon::examples
