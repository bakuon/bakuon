#include "gui/b_contextfocusrouter.h"
#include "gui/b_commandsystem.h"

namespace bakuon::gui {

ContextFocusRouter::ContextFocusRouter(QObject* parent)
    : QObject(parent)
    , m_currentProvider(nullptr)
{
}

ContextFocusRouter::~ContextFocusRouter()
{
    uninstall();
}

void ContextFocusRouter::addProviderWidget(QObject* widget, const Context& context)
{
    if (!widget || !context.empty()) {
        qWarning() << "ContextFocusRouter: Invalid widget or empty context";
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

    if (m_currentProvider == widget) {
        m_currentProvider = nullptr;
    }
}

void ContextFocusRouter::clearProviderWidget()
{
    m_currentContext  = {};
    m_currentProvider = nullptr;
    m_providers.clear();
}

void ContextFocusRouter::install()
{
    if (m_installed)
        return;
    if (qApp) {
        qApp->installEventFilter(this);
        // 或者连接到应用的焦点变化信号
        // connect(qApp, &QApplication::focusChanged, this, &ContextFocusRouter::updateProvider);
        m_installed = true;
    }
}

void ContextFocusRouter::uninstall()
{
    if (!m_installed)
        return;

    // ContextFocusRouter 通常以 qApp 为 parent 构造，这意味着
    // QApplication 自身析构时会走 QObjectPrivate::deleteChildren()
    // 自动把本对象也析构掉，从而调用到这里。但此时 QApplication 正处
    // 于自己的析构过程中，其内部状态（包括事件过滤器列表）可能已经被部
    // 分拆除；此时仍然调用 qApp->removeEventFilter(this) 会踩到已析
    // 构的内部数据而段错误。QCoreApplication::closingDown() 正是 Qt
    // 提供的、专门用来判断"应用是否正在关闭"的信号，此时应跳过任何对 qApp
    // 的进一步操作————反正应用本身即将退出，事件过滤器列表也无所谓是否移除了。
    if (qApp && !QCoreApplication::closingDown()) {
        qApp->removeEventFilter(this);
    }
    m_installed = false;

    // 主动释放当前持有的引用，避免"卸载路由器"之后遗留一份永远不会再被
    // 更新、也不会再被任何人 pop 掉的激活上下文。
    if (m_currentProvider) {
        for (const ContextId& ctx : m_currentContext) {
            CommandSystem::popContext(ctx, m_currentProvider.data());
        }
    }
    m_currentProvider = nullptr;
    m_currentContext  = {};
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

    handleFocusIn(gainer);

    return QObject::eventFilter(watched, event);
}

void ContextFocusRouter::handleFocusIn(QObject* gainer)
{
    QObject* provider = gainer;
    Context foundContext;

    while (provider) {
        if (auto it = m_providers.find(provider); it != m_providers.end()) {
            foundContext = it->second;
            break;
        }
        /**
        // 或者使用属性标签的形式
        if (Context ctx = providerContext(provider); !ctx.empty()) {
            foundContext = ctx;
            break;
        }
        */
        provider = provider->parent(); // 沿着对象树向上冒泡
    }

    if (!provider) {
        foundContext.clear();
    }

    if (provider == m_currentProvider.data()) {
        return;
    }

    auto* const oldProvider = m_currentProvider.data();
    const auto oldContext   = m_currentContext;

    // !!! 顺序很重要：先 push 新上下文、再 pop 旧上下文，不要反过来。
    // CommandManager 是"引用计数集合"而不是栈顶唯一，如果先 pop 旧的再 push 新的，
    // 中间会出现一个短暂的过渡态——旧上下文已经失活、新上下文还未激活，这期间会
    // 广播一次 contextChanged()，让全部 Command 都做一次多余的重新仲裁；对于
    // 那些同时绑定了新旧两个上下文的 Command（比如图像/3D 编辑器共用的"删除"命令），
    // 还会让它们在这个过渡态里短暂掉进"无权威源"分支。先 push 后 pop 则不会有这个
    // 问题：任何同时绑定新旧上下文的 Command，会在旧上下文失活之前就已经因为新
    // 上下文有更大的 activationOrder 而立刻切换过去；极端情况下如果新旧上下文恰好
    // 是同一个字符串，引用计数全程不会掉到 0，完全不会触发多余的全局重新仲裁。
    if (provider) {
        for (const ContextId& ctx : foundContext) {
            CommandSystem::pushContext(ctx, provider);
        }
    }
    if (oldProvider) {
        for (const ContextId& ctx : oldContext) {
            CommandSystem::popContext(ctx, oldProvider);
        }
    }

    m_currentProvider = provider; // 为 nullptr 表示焦点完全离开了所有已标记部件
    m_currentContext  = foundContext;
}

QObject* ContextFocusRouter::findContextProvider(QObject* object) const
{
    QObject* provider = object;
    while (provider) {
        if (auto it = m_providers.find(provider); it != m_providers.end()) {
            return provider;
        }
        provider = provider->parent(); // 沿着对象树向上冒泡
    }
    return nullptr;
}

} // namespace bakuon::gui
