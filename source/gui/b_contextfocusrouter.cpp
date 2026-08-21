#include "gui/b_contextfocusrouter.h"

namespace bakuon::gui {

ContextFocusRouter::ContextFocusRouter(QObject* parent)
    : QObject(parent)
    , m_lastProvider(nullptr)
{
    // TODO: 在外部调用过滤器
    // 注册为全局事件过滤器，监控整个应用程序的焦点变化
    qApp->installEventFilter(this);

    // 或者连接到应用的焦点变化信号
    // if (qApp) {
    //     connect(qApp, &QApplication::focusChanged, this, &ContextFocusRouter::updateProvider);
    // }
}

void ContextFocusRouter::addProviderWidget(QObject* widget, const ContextId& context)
{
    if (!widget || !context.isValid()) {
        qWarning() << "ContextFocusRouter: Invalid widget or context ID";
        return;
    }

    m_providers[widget] = context;
}

void ContextFocusRouter::removeProviderWidget(QObject* widget)
{
    auto it = m_providers.find(widget);
    if (it != m_providers.end()) {
        m_providers.erase(it);
    }

    if (m_lastProvider == widget) {
        m_lastProvider = nullptr;
    }
}

void ContextFocusRouter::clear()
{
    m_lastContext  = {};
    m_lastProvider = nullptr;
    m_providers.clear();
}

bool ContextFocusRouter::eventFilter(QObject* watched, QEvent* event)
{
    if (event->type() != QEvent::FocusIn) {
        return QObject::eventFilter(watched, event);
    }

    auto* gainer = qobject_cast<QWidget*>(watched);

    // !!! 这里过滤掉的是一类真实存在的噪声：Qt 会向一些根本不该真正持有键盘焦点、
    // 或者根本不是 QWidget 的 QObject（典型如内部样式对象 QStyle、承载顶层窗口的
    // QWidgetWindow，以及 QMainWindow 这类默认 focusPolicy() 就是 Qt::NoFocus 的
    // 部件）补发 FocusIn 事件用于内部簿记，这并不代表"用户的逻辑焦点真的换到了
    // 这里"。必须同时拒绝这两类：不是 QWidget（qobject_cast 失败，widget 为空）
    // 和是 QWidget 但 focusPolicy 为 NoFocus——只挡后者是不够的，前者同样会导致
    // 后面的向上查找拿到 nullptr、被误判成"焦点离开了所有已标记部件"，把此刻
    // 正确持有的上下文错误地 pop 掉。这正是本类曾经出现过的一个真实 bug。
    if (!gainer || gainer->focusPolicy() == Qt::NoFocus) {
        return QObject::eventFilter(watched, event);
    }

    handleFocusProvider(gainer);

    return QObject::eventFilter(watched, event);
}

void ContextFocusRouter::handleFocusProvider(QObject* focusedWidget)
{
    if (!focusedWidget) {
        return;
    }

    QObject* current         = focusedWidget;
    QObject* matchedProvider = nullptr;
    ContextId foundContext;

    while (current) {
        if (auto it = m_providers.find(current); it != m_providers.end()) {
            matchedProvider = current;
            foundContext    = it->second;
            break;
        }
        current = current->parent(); // 沿着对象树向上冒泡
    }

    if (matchedProvider == m_lastProvider) {
        return;
    }

    // !!! 顺序很重要：先 push 新上下文、再 pop 旧上下文，不要反过来。
    // ContextManager 是"引用计数集合"而不是栈顶唯一，如果先 pop 旧的再 push 新的，
    // 中间会出现一个短暂的过渡态——旧上下文已经失活、新上下文还未激活，这期间会
    // 广播一次 contextSetChanged()，让全部 Command 都做一次多余的重新仲裁；对于
    // 那些同时绑定了新旧两个上下文的 Command（比如图像/3D 编辑器共用的"删除"命令），
    // 还会让它们在这个过渡态里短暂掉进"无权威源"分支。先 push 后 pop 则不会有这个
    // 问题：任何同时绑定新旧上下文的 Command，会在旧上下文失活之前就已经因为新
    // 上下文有更大的 activationOrder 而立刻切换过去；极端情况下如果新旧上下文恰好
    // 是同一个字符串，引用计数全程不会掉到 0，完全不会触发多余的全局重新仲裁。
    if (matchedProvider) {
        CommandSystem::pushContext(foundContext, matchedProvider);
    }
    if (m_lastProvider) {
        gui::CommandSystem::popContext(m_lastContext, m_lastProvider);
    }

    m_lastProvider = matchedProvider; // 为 nullptr 表示焦点完全离开了所有已标记部件
    m_lastContext  = foundContext;
}

QObject* ContextFocusRouter::findContextProvider(QObject* widget) const
{
    if (!widget) {
        return nullptr;
    }
    QObject* current = widget;
    while (current) {
        if (auto it = m_providers.find(current); it != m_providers.end()) {
            return current;
        }
        current = current->parent();
    }
    return nullptr;
}

void ContextFocusRouter::updateProvider(QWidget* old, QWidget* now)
{
    Q_UNUSED(old)
    Q_UNUSED(now)
}

} // namespace bakuon::gui
