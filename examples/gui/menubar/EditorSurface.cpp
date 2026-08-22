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
    , m_selectionRouter(m_selectedContext, this)
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
        m_selectionRouter.setSelected(m_selected);

        // 不要主动控制 action 的状态，让上下文去控制命令状态。
        // m_deleteAction->setEnabled(m_selected);
        // m_duplicateAction->setEnabled(m_selected);

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
    menu.addAction(gui::CommandSystem::command(kCmdPaste)->action());
    menu.exec(event->globalPos());
}

void EditorSurface::setupRealActions(const QString& label)
{
    m_deleteAction = new QAction(QStringLiteral("删除"), this);
    connect(m_deleteAction, &QAction::triggered, this, [this, label]() {
        QMessageBox::information(this,
                                 QStringLiteral("删除"),
                                 QStringLiteral("已删除%1中选中的对象").arg(label));
    });

    m_duplicateAction = new QAction(QStringLiteral("复制"), this);
    connect(m_duplicateAction, &QAction::triggered, this, [this, label]() {
        if (auto* mw = qobject_cast<QMainWindow*>(window())) {
            mw->statusBar()->showMessage(QStringLiteral("已复制%1对象").arg(label), 2000);
        }
    });

    m_pasteAction = new QAction(QStringLiteral("粘贴"), this);
    connect(m_pasteAction, &QAction::triggered, this, [this, label]() {
        if (auto* mw = qobject_cast<QMainWindow*>(window())) {
            mw->statusBar()->showMessage(QStringLiteral("已粘贴%1复制的对象").arg(label), 2000);
        }
    });

    // kCmdDelete 只受 ContextSelectionRouter 选中对象路由影响，完全不受 ContextFocusRouter 焦点路由影响。
    gui::CommandSystem::command(kCmdDelete)->addContextAction(m_deleteAction, m_selectedContext);

    // kCmdPaste 只受 ContextFocusRouter 焦点路由影响，完全不受 ContextSelectionRouter 选中对象路由影响（实际应用受剪贴板数据上下文影响）。
    gui::CommandSystem::command(kCmdPaste)->addContextAction(m_pasteAction, m_focusContext);

    // test focus 1: 当 m_selectedContext 激活时 kCmdDuplicate 命令无论当前部件是否有焦点都可用，
    // 因为 m_selectedContext 未受 ContextFocusRouter 焦点路由控制。
    gui::CommandSystem::command(kCmdDuplicate)
        ->addContextAction(m_duplicateAction, m_selectedContext);
    // test focus 2: 当 m_selectedContext 未激活时，ContextFocusRouter 才能路由 m_focusContext 焦点控制 kCmdDuplicate 命令的状态。
    gui::CommandSystem::command(kCmdDuplicate)->addContextAction(m_duplicateAction, m_focusContext);

    // test focus 结论：
    // S用户---需要焦点外，还需要一堆各种各样的条件上下文才能用，或者无视焦点但需要一堆条件上下文（复杂型）。
    // A用户---需要焦点作为总开关，没有焦点必须禁止使用，有焦点还要看选没选中对象（双重型）：
    //   - 1.部件无焦点时，m_selectedContext 激活和未激活时 kCmdDuplicate 命令都不可用；
    //   - 2.部件有焦点时，m_selectedContext 激活时 kCmdDuplicate 命令可用， m_selectedContext 未激活时 kCmdDuplicate 命令不可用。
    // B用户---只看选中对象行事，选中能用，没选中不能用，无视焦点（唯一型I）：
    //   - 部件无论有无焦点，m_selectedContext 激活时 kCmdDuplicate 命令可用，m_selectedContext 未激活时 kCmdDuplicate 命令不可用；
    // C用户---只看焦点行事，有焦点能用，没焦点不能用，无视选中（唯一型II）：
    //   - m_selectedContext 激活与没激活无关，他不需要使用 m_selectedContext 做选择。
    // D用户---焦点和选择对象都不需要，永远可用（永生型）：
    //   - 仅使用全局上下文 kCtxGlobal。
    // E用户---永远不可用（墓碑型）：
    //   - 不注册任何上下文，等待轮回重生（版本迭代用），极少出现的情况。
    // F用户---与界面操作和焦点无关，空闲时不可用（懒汉型/临时工）
    //   - 因为与界面无关，后台创建临时上下文后启用，完成后丢掉（当心多个任务共用一个上下文）。字面上看和B用户类似，但这是背地行事。
    // 综上所述，如果出现需要上下文栈的情况（如S和A用户），则应该就目前已有的 priority 优先级控制法，使上下文的 push/pop 统一入口的路由控制，
    // 而不是现在的 kCmdDuplicate 命令分别使用 ContextFocusRouter 和 ContextSelectionRouter 两种互不相干的路由控制。
    // 需要现写一些 examples 去完成这些需求，以求证这套框架的可扩展性是可圈可点的、全面的、可复用的...。
}

} // namespace bakuon::examples
