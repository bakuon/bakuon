#include "FocusContextWatcher.h"

#include <QEvent>

namespace bakuon::examples {

FocusContextWatcher::FocusContextWatcher(QObject* parent)
    : QObject(parent)
{
}

bool FocusContextWatcher::eventFilter(QObject* watched, QEvent* event)
{
    if (event->type() != QEvent::FocusIn) {
        return QObject::eventFilter(watched, event);
    }

    // Prevent changing the context object just because the menu or a menu item is activated
    // if (qobject_cast<QMenuBar*>(watched) || qobject_cast<QMenu*>(watched))
    //     return QObject::eventFilter(watched, event);

    auto* widget = qobject_cast<QWidget*>(watched);

    // !!! 这里过滤掉的是一类真实存在的噪声：Qt 会向一些根本不该真正持有键盘焦点、
    // 或者根本不是 QWidget 的 QObject（典型如内部样式对象 QStyle、承载顶层窗口的
    // QWidgetWindow，以及 QMainWindow 这类默认 focusPolicy() 就是 Qt::NoFocus 的
    // 部件）补发 FocusIn 事件用于内部簿记，这并不代表"用户的逻辑焦点真的换到了
    // 这里"。必须同时拒绝这两类：不是 QWidget（qobject_cast 失败，widget 为空）
    // 和是 QWidget 但 focusPolicy 为 NoFocus——只挡后者是不够的，前者同样会导致
    // 后面的向上查找拿到 nullptr、被误判成"焦点离开了所有已标记部件"，把此刻
    // 正确持有的上下文错误地 pop 掉。这正是本类曾经出现过的一个真实 bug。
    if (!widget || widget->focusPolicy() == Qt::NoFocus) {
        return QObject::eventFilter(watched, event);
    }

    // 从获得焦点的部件开始沿父级链向上查找，直到找到第一个标记了上下文属性的部件。
    // 用 property() 判断是否“已标记”，不再依赖 dynamic_cast 与接口继承。
    QWidget* providerWidget = nullptr;
    gui::ContextId foundContext;
    while (widget) {
        gui::ContextId ctx = widgetContext(widget);
        if (ctx.isValid()) {
            providerWidget = widget;
            foundContext   = ctx;
            break;
        }
        widget = widget->parentWidget();
    }

    if (providerWidget == m_focusedProviderWidget.data()) {
        return QObject::eventFilter(watched, event); // 焦点仍在同一个已标记部件内部转移，无需变更
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
    QWidget* const oldWidget        = m_focusedProviderWidget.data();
    const gui::ContextId oldContext = m_focusedContext;

    if (providerWidget) {
        gui::CommandSystem::pushContext(foundContext, providerWidget);
    }
    if (oldWidget) {
        gui::CommandSystem::popContext(oldContext, oldWidget);
    }

    m_focusedProviderWidget = providerWidget; // 为 nullptr 表示焦点完全离开了所有已标记部件
    m_focusedContext        = providerWidget ? foundContext : gui::ContextId{};

    return QObject::eventFilter(watched, event);
}

} // namespace bakuon::examples
