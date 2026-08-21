#include "gui/b_command.h"
#include "gui/b_commandmanager.h"

namespace bakuon::gui {

Command::Command(CommandId id, QString defaultText, CommandManager& mgr)
    : QObject(nullptr) // note: it is a std::unique_ptr<Command>
    , m_mgr(mgr)
    , m_id(std::move(id))
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

    // 上下文集合一变化就重新仲裁权威绑定；每个 Command 独立订阅，
    // 不需要 CommandSystem 集中遍历全部命令做批量刷新。
    connect(&m_mgr, &CommandManager::contextChanged, this, &Command::resyncAuthoritativeBinding);
}

QAction* Command::action()
{
    // 不要惰性创建，否则注册时设置的默认快捷键无法起作用
    // if (!m_proxyAction) {
    //     m_proxyAction = new QAction(m_defaultText, this);
    //     if (!m_defaultIcon.isNull()) {
    //         m_proxyAction->setIcon(m_defaultIcon);
    //     }
    //     if (!m_defaultShortcuts.isEmpty()) {
    //         m_proxyAction->setShortcuts(m_defaultShortcuts);
    //     }
    //     m_proxyAction->setCheckable(m_defaultCheckable);
    //     if (m_attributes.testFlag(Attribute::UpdateEnabled)) {
    //         // 初始无权威绑定，禁用；一旦有已激活上下文的绑定会立即刷新
    //         m_proxyAction->setEnabled(false);
    //     }
    //     resyncAuthoritativeBinding();
    // }
    return m_proxyAction;
}

void Command::setDefaultText(const QString& text)
{
    m_defaultText = text;
    if (m_proxyAction && m_authoritativeIndex < 0) {
        m_proxyAction->setText(m_defaultText); // 只有在没有权威源接管时，默认值才直接生效
    }
}

void Command::setDefaultIcon(const QIcon& icon)
{
    m_defaultIcon = icon;
    if (m_proxyAction && m_authoritativeIndex < 0) {
        m_proxyAction->setIcon(m_defaultIcon);
    }
}

void Command::setDefaultCheckable(bool checkable)
{
    m_defaultCheckable = checkable;
    if (m_proxyAction && m_authoritativeIndex < 0) {
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
    return m_proxyAction ? m_proxyAction->shortcuts() : QList<QKeySequence>();
}

QKeySequence Command::shortcut() const
{
    return m_proxyAction ? m_proxyAction->shortcut() : QKeySequence();
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

void Command::setAttributes(Attributes attributes)
{
    m_attributes = attributes;
    resyncAuthoritativeBinding(); // 策略变化可能影响当前代理的可见性/禁用表现，立即按新策略校正一次
}

void Command::setAttribute(Attributes attribute, bool on)
{
    if (on) {
        m_attributes |= attribute;
    } else {
        m_attributes &= ~Attributes(attribute);
    }
    resyncAuthoritativeBinding();
}

void Command::addContextAction(QAction* realAction, const ContextId& context, int priority)
{
    Q_ASSERT_X(realAction != nullptr, "Command::addContextAction", "realAction is null");

    if (realAction == m_proxyAction) {
        return;
    }

    // changed()/destroyed() 两条连接的建立逻辑复用；lambda 里都是按下标查
    // m_bindings[m_authoritativeIndex]，不直接捕获 binding 引用，所以 vector
    // 扩容/重新分配也不会让这些连接失效，可以安全地在"替换"和"新增"两条路径间共享。
    auto wireBinding = [this](ContextBinding& binding) {
        QAction* action       = binding.realAction;
        binding.changedConn   = connect(action, &QAction::changed, this, [this]() {
            if (m_authoritativeIndex < 0 || !m_proxyAction) {
                return;
            }
            QObject* changedSender = sender();
            if (m_bindings[m_authoritativeIndex].realAction.data() != changedSender) {
                return; // 非当前权威源的属性变化，忽略
            }
            mirrorProperties(qobject_cast<QAction*>(changedSender), m_proxyAction);
        });
        binding.destroyedConn = connect(action, &QObject::destroyed, this, [this]() {
            m_authoritativeIndex = -1;
            resyncAuthoritativeBinding();
        });
    };

    // 若该上下文已有旧绑定，原地替换而不是先摘除、重新插入——避免中间产生一次
    // "该上下文暂时没有绑定"的过渡态，导致 resyncAuthoritativeBinding 被多余地
    // 触发两次（旧绑定摘除时一次、新绑定插入时又一次），也省掉一次不必要的
    // disconnect+reconnect 转发连接。
    auto it = std::find_if(m_bindings.begin(),
                           m_bindings.end(),
                           [&context](const ContextBinding& b) { return b.context == context; });
    if (it != m_bindings.end()) {
        QObject::disconnect(it->changedConn);
        QObject::disconnect(it->destroyedConn);
        it->realAction = realAction;
        it->priority   = priority;
        wireBinding(*it);
    } else {
        ContextBinding binding;
        binding.context    = context;
        binding.realAction = realAction;
        binding.priority   = priority;
        wireBinding(binding);
        m_bindings.push_back(std::move(binding));
    }

    m_authoritativeIndex = -1; // 绑定关系发生了变化，下标必须重新计算，不能沿用旧值
    resyncAuthoritativeBinding();
}

void Command::removeContextAction(const ContextId& context)
{
    auto it = std::find_if(m_bindings.begin(),
                           m_bindings.end(),
                           [&context](const ContextBinding& b) { return b.context == context; });
    if (it == m_bindings.end()) {
        return;
    }
    QObject::disconnect(it->changedConn);
    QObject::disconnect(it->destroyedConn);
    m_bindings.erase(it);
    m_authoritativeIndex = -1; // erase 会导致下标整体偏移，统一失效后重新计算最稳妥
    resyncAuthoritativeBinding();
}

bool Command::hasContextAction(const ContextId& context) const noexcept
{
    return std::any_of(m_bindings.begin(), m_bindings.end(), [&context](const ContextBinding& b) {
        return b.context == context;
    });
}

std::vector<ContextId> Command::contexts() const
{
    std::vector<ContextId> result;
    const auto size = m_bindings.size();
    result.reserve(size);
    for (size_t i = 0; i < size; ++i) {
        const ContextBinding& binding = m_bindings[i];
        result.push_back(binding.context);
    }
    return result;
}

int Command::findAuthoritativeIndex() const
{
    ContextTier bestTier = ContextTier::Foreground; // 会被第一个候选无条件覆盖，初值不重要
    int bestIndex        = -1;
    int bestPriority     = std::numeric_limits<int>::min();
    uint64_t bestOrder   = 0;

    for (int i = 0; i < static_cast<int>(m_bindings.size()); ++i) {
        const ContextBinding& binding = m_bindings[i];
        if (binding.realAction.isNull() || !m_mgr.isActiveContext(binding.context)) {
            continue; // 只在"已注册且其上下文当前激活"的绑定里挑选
        }

        const ContextTier tier = m_mgr.effectiveTier(binding.context);
        const uint64_t order   = m_mgr.activationOrder(binding.context);

        // 仲裁优先级：层级 > 优先级 > 时序，层级比较严格占先——
        // 即便某个候选的 priority/order 再高，只要层级更低就不可能胜出，
        // 这正是防止"后台任务反复刷新时序、压过真正的交互上下文"的关键。
        if (bestIndex < 0 || tier > bestTier
            || (tier == bestTier && binding.priority > bestPriority)
            || (tier == bestTier && binding.priority == bestPriority && order > bestOrder)) {
            bestIndex    = i;
            bestTier     = tier;
            bestPriority = binding.priority;
            bestOrder    = order;
        }
    }
    return bestIndex;
}

void Command::resyncAuthoritativeBinding()
{
    // 清理已失效（realAction 已销毁）的绑定，避免野下标/悬空引用参与后续仲裁
    std::erase_if(m_bindings, [](const ContextBinding& b) { return b.realAction.isNull(); });

    const int newIndex = findAuthoritativeIndex();

    if (!m_proxyAction) {
        return; // 尚未有人调用过 action()，无需同步任何 UI 状态
    }

    // 无需任何更新
    if (m_authoritativeIndex >= 0 && newIndex == m_authoritativeIndex) {
        return;
    }

    // !!! 这里曾经有一个真实的 bug：早期实现用「本次算出的权威下标是否等于上次的值」
    // 来决定要不要重新连接 proxyAction::triggered -> real->trigger()，试图省掉一次
    // disconnect+connect。但「下标没变」不代表「转发连接已经建立过」——如果上一次
    // resync 发生在 m_proxyAction 还不存在的时候（先 registerContextAction、后调用
    // proxyAction() 是很常见的时序，比如某条命令一直没被拖进任何菜单/工具栏，直到
    // "自定义菜单"对话框第一次读取它的显示文本才触发 proxyAction() 创建），
    // 权威下标当时就已经算好了、且从未变化过，但转发连接其实从来没建立过，
    // 于是这条命令的代理点了没反应。命令数量在几十到百的量级，无条件重连的开销
    // 完全可以忽略，用一点点确定性换掉这一整类"看似没变化其实没接线"的 bug 完全值得，
    // 因此这里不再做这个优化，每次都老老实实重新断开、按需重新连接。
    QObject::disconnect(m_triggeredConn);
    QObject::disconnect(m_toggledConn);
    m_authoritativeIndex = newIndex;

    if (m_authoritativeIndex < 0) {
        // 无权威源：按 HideWhenIdle 策略决定是隐藏还是仅禁用（保留最后一次镜像的文案）
        m_proxyAction->setVisible(!m_attributes.testFlag(Attribute::HideWhenIdle));
        if (m_attributes.testFlag(Attribute::UpdateEnabled)) {
            m_proxyAction->setEnabled(false);
        }
        return;
    }

    QAction* real = m_bindings[static_cast<size_t>(m_authoritativeIndex)].realAction;
    // 先整体同步一次，避免残留旧权威源的状态导致闪烁/不一致
    mirrorProperties(real, m_proxyAction); // 按当前 Attributes 策略整体同步一次，避免闪烁/不一致

    bool shouldBeActive = real && m_proxyAction->isEnabled() && m_proxyAction->isVisible()
                          && !m_proxyAction->isSeparator();
    setActive(shouldBeActive);

    m_triggeredConn = connect(m_proxyAction, &QAction::triggered, real, [real](bool) {
        real->trigger();
    });
    m_toggledConn   = connect(m_proxyAction, &QAction::toggled, real, [real](bool) {
        real->toggle();
    });
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
