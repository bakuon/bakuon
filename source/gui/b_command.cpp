#include "gui/b_command.h"

namespace bakuon::gui {

Command::Command(const CommandId& id, QString defaultText, QObject* parent)
    : QObject(parent)
    , m_id(id)
    , m_defaultText(std::move(defaultText))
    , m_defaultCheckable(false)
    , m_active(false)
    , m_hasShortcuts(false)
    , m_proxyAction(new QAction(m_defaultText, this))
{
    // 默认策略：全部 Update* 属性开启（全量镜像），HideWhenIdle 关闭
    // （无权威源时仅禁用代理、保留最后一次镜像的文案，不直接从菜单/工具栏消失）。
    m_attributes = Attribute::UpdateText | Attribute::UpdateIcon | Attribute::UpdateToolTip
                   | Attribute::UpdateChecked | Attribute::UpdateEnabled;
}

QAction* Command::action()
{
    return m_proxyAction;
}

void Command::setDefaultText(const QString& text)
{
    m_defaultText = text;
    // 仅在无权威源时写回代理，避免覆盖正在镜像的 realAction 文案
    if (m_proxyAction && !m_realAction) {
        m_proxyAction->setText(m_defaultText);
    }
}

void Command::setDefaultIcon(const QIcon& icon)
{
    m_defaultIcon = icon;
    if (m_proxyAction && !m_realAction) {
        m_proxyAction->setIcon(m_defaultIcon);
    }
}

void Command::setDefaultCheckable(bool checkable)
{
    m_defaultCheckable = checkable;
    if (m_proxyAction && !m_realAction) {
        m_proxyAction->setCheckable(m_defaultCheckable);
    }
}

void Command::setDefaultShortcuts(const QList<QKeySequence>& shortcuts)
{
    // 快捷键只挂在 m_proxyAction 上：realAction 不应重复设置同一快捷键，
    // 否则一旦出现"多个上下文同时激活"的边缘场景，Qt 会判定为歧义快捷键，双方都不触发。
    m_defaultShortcuts = shortcuts;
    if (!m_hasShortcuts) {
        setShortcuts(shortcuts);
    }
}

void Command::setDefaultShortcut(const QKeySequence& shortcut)
{
    setDefaultShortcuts({shortcut});
}

QList<QKeySequence> Command::shortcuts() const
{
    return m_proxyAction ? m_proxyAction->shortcuts() : QList<QKeySequence>{};
}

QKeySequence Command::shortcut() const
{
    return m_proxyAction ? m_proxyAction->shortcut() : QKeySequence{};
}

void Command::setShortcuts(const QList<QKeySequence>& shortcuts)
{
    if (m_proxyAction) {
        m_hasShortcuts = true;
        m_proxyAction->setShortcuts(shortcuts);
    }
}

void Command::setShortcut(const QKeySequence& shortcut)
{
    setShortcuts({shortcut});
}

void Command::setActive(bool active)
{
    if (m_active == active) {
        return;
    }
    m_active = active;
    Q_EMIT activeChanged(m_active);
}

void Command::setAttributes(Attributes attributes)
{
    m_attributes = attributes;
    syncCurrentState(); // 策略变了，哪怕权威源身份没变，也要按新策略重新同步一次
}

void Command::setAttribute(Attributes attribute, bool on)
{
    if (on) {
        m_attributes |= attribute;
    } else {
        m_attributes &= ~Attributes(attribute);
    }
    syncCurrentState();
}

void Command::setRealAction(QAction* activeAction)
{
    if (m_realAction.data() == activeAction) {
        return; // 身份没变；状态是否与当前 Attributes 一致由其它入口（setAttributes 等）保证
    }
    m_realAction = activeAction;
    syncCurrentState();
}

void Command::syncCurrentState()
{
    QObject::disconnect(m_changedConn);
    QObject::disconnect(m_triggeredConn);
    // QObject::disconnect(m_toggledConn);

    if (!m_realAction) {
        setActive(false);
        // 无权威源：按 HideWhenIdle 策略决定是隐藏还是仅禁用。
        // 同时回落到默认展示（text/icon/checkable），避免长期停留在上一个权威源的镜像残影；
        // 若业务希望“保留最后一次镜像文案”，可关闭对应 Update* 属性后自行通过 action() 摆布。
        if (m_attributes.testFlag(Attribute::UpdateText)) {
            m_proxyAction->setText(m_defaultText);
        }
        if (m_attributes.testFlag(Attribute::UpdateIcon)) {
            m_proxyAction->setIcon(m_defaultIcon);
        }
        if (m_attributes.testFlag(Attribute::UpdateChecked)) {
            m_proxyAction->setCheckable(m_defaultCheckable);
            if (m_defaultCheckable) {
                const bool wasBlocked = m_proxyAction->blockSignals(true);
                m_proxyAction->setChecked(false);
                m_proxyAction->blockSignals(wasBlocked);
            }
        }
        m_proxyAction->setVisible(!m_attributes.testFlag(Attribute::HideWhenIdle));
        if (m_attributes.testFlag(Attribute::UpdateEnabled)) {
            m_proxyAction->setEnabled(false);
        }
        return;
    }

    QAction* real = m_realAction;
    // 先整体同步一次，避免残留旧权威源的状态导致闪烁/不一致
    mirrorProperties(real, m_proxyAction); // 先整体同步一次，避免残留旧权威源状态导致闪烁

    bool shouldBeActive = real && m_proxyAction->isEnabled() && m_proxyAction->isVisible()
                          && !m_proxyAction->isSeparator();
    setActive(shouldBeActive);

    m_changedConn   = connect(real, &QAction::changed, this, [this, real]() {
        if (m_realAction.data() == real) {
            mirrorProperties(real, m_proxyAction);
        }
    });
    // 只转发 triggered：checkable 的 QAction 在点击时会先 toggle 再 emit triggered，
    // 若再连 toggled -> real->toggle()，real 会被切换两次，等于没切。
    m_triggeredConn = connect(m_proxyAction, &QAction::triggered, real, [real](bool) {
        real->trigger();
    });

    // 注意：这里不需要 Command 自己再连一次 real 的 destroyed() ——ContextState 已经监听了
    // 它注册的每个 action 的 destroyed()，销毁时会 emit actionsChanged()，ContextArbiter
    // 监听到之后会重新仲裁并调用 setRealAction(新权威源或 nullptr)。m_realAction 是
    // QPointer，即便这中间有极短暂的窗口，读取它也不会是悬空指针。
}

void Command::mirrorProperties(const QAction* from, QAction* to)
{
    if (!from || !to || (from == to)) {
        return;
    }

    if (m_attributes.testFlag(Attribute::UpdateText)) {
        to->setText(from->text());
        to->setIconText(from->iconText());
        to->setStatusTip(from->statusTip());
        to->setWhatsThis(from->whatsThis());
    }
    if (m_attributes.testFlag(Attribute::UpdateIcon) && !from->icon().isNull()) {
        to->setIcon(from->icon());
        to->setIconVisibleInMenu(from->isIconVisibleInMenu());
    }
    if (m_attributes.testFlag(Attribute::UpdateToolTip)) {
        to->setToolTip(from->toolTip());
    }

    if (m_attributes.testFlag(Attribute::UpdateChecked)) {
        to->setCheckable(from->isCheckable());
        if (from->isCheckable()) {
            // 屏蔽信号以避免递归切换 Block signals to avoid recursive toggled
            const bool wasBlocked = to->blockSignals(true);
            to->setChecked(from->isChecked());
            to->blockSignals(wasBlocked);
        }
    }

    if (m_attributes.testFlag(Attribute::UpdateEnabled)) {
        to->setEnabled(from->isEnabled());
    }
    to->setVisible(from->isVisible());

    // 快捷键刻意不镜像：全局快捷键统一固定在 proxyAction 上，见 setShortcut() 注释。
}

} // namespace bakuon::gui
