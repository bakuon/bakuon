#pragma once

#include <QtCore/QFlags>
#include <QtCore/QPointer>
#include <QtCore/QString>
#include <QtGui/QAction>
#include <QtGui/QIcon>
#include <QtGui/QKeySequence>

#include "gui/b_types.h"

namespace bakuon::gui {

// C++20 concepts：约束"可作为命令执行体"的可调用对象——必须无参可调用且返回 void
template<typename F>
concept ExecuteCallable = std::invocable<F> && std::same_as<std::invoke_result_t<F>, void>;

// 约束："可作为可执行性判定"的可调用对象——无参可调用且返回值可转换为 bool
template<typename F>
concept PredicateCallable = std::invocable<F> && std::convertible_to<std::invoke_result_t<F>, bool>;

// CommandSlot: 某个 Command 在某个 ContextId 下的具体响应行为绑定。
// 三个字段互相独立，使同一个 execute 逻辑可以搭配不同的可执行性判断与撤销逻辑复用。
struct CommandSlot
{
    std::function<void()> execute;    // 必需：命令被触发时执行的动作
    std::function<bool()> canExecute; // 可选：为空则视为恒可执行
    std::function<void()> undo;       // 可选：为空表示该 handler 不支持撤销

    [[nodiscard]] bool isValid() const noexcept { return static_cast<bool>(execute); }
    [[nodiscard]] bool supportsUndo() const noexcept { return static_cast<bool>(undo); }

    // 便捷工厂函数：约束参数类型必须满足对应 concept，编译期即可发现类型不匹配的绑定错误
    template<ExecuteCallable Exec>
    static CommandSlot make(Exec&& exec)
    {
        return CommandSlot{.execute = std::forward<Exec>(exec), .canExecute = {}, .undo = {}};
    }

    template<ExecuteCallable Exec, PredicateCallable Pred>
    static CommandSlot make(Exec&& exec, Pred&& pred)
    {
        return CommandSlot{.execute    = std::forward<Exec>(exec),
                           .canExecute = std::forward<Pred>(pred),
                           .undo       = {}};
    }
};

class CommandManager;

/**
 * @brief Command：一个“命令”的逻辑身份（例如“删除”“旋转”“另存为”）。
 *
 * 关键设计：一个 Command 可以在多个上下文（ContextId）中分别绑定不同的
 * CommandSlot（响应行为），并且可以在多处 UI（菜单 / 工具栏 / 右键菜单）
 * 各自创建独立的 QAction 实例——这些 QAction 共享同一份命令语义（文本、图标、快捷键），
 * 但每个实例的 enabled 状态由其所属上下文当前是否激活、以及该上下文对应
 * CommandSlot::canExecute() 共同决定，由 ContextManager 的状态变化驱动自动刷新。
 */
class Command : public QObject
{
    Q_OBJECT
public:
    // Attribute：声明式描述"代理 QAction 应如何随权威 realAction 变化"。
    // 分两类语义：
    //   - Update* 系列：有权威 realAction 时，对应属性是否参与镜像同步（关闭则代理该属性
    //     完全由外部通过 action() 自行摆布，Command 不再触碰）；
    //   - HideWhenIdle：没有任何已注册上下文处于激活状态时，代理是"仅禁用、保留最后一次
    //     镜像的展示内容"（默认），还是"直接从菜单/工具栏隐藏"。
    // todo 改成 Flag
    enum class Attribute : quint8 {
        None          = 0,
        UpdateText    = 1u << 0, // 镜像 realAction 的 text()
        UpdateIcon    = 1u << 1, // 镜像 realAction 的 icon()
        UpdateToolTip = 1u << 2, // 镜像 realAction 的 toolTip()
        UpdateChecked = 1u << 3, // 镜像 realAction 的 checkable/checked 联动状态
        UpdateEnabled = 1u << 4, // 镜像 realAction 的 enabled；无权威源时也用它控制是否禁用代理
        HideWhenIdle  = 1u << 5, // 无权威源时隐藏代理（setVisible(false)），而非仅禁用
    };
    Q_DECLARE_FLAGS(Attributes, Attribute)

    Command(const CommandId& id, QString defaultText, CommandManager& mgr);

    const CommandId& id() const noexcept { return m_id; }

    // 获取代理 QAction；同一 Command 只有一个代理实例，可反复添加到
    // 任意数量的 QMenu / QToolBar 中——这是相较"每上下文一个 QAction"方案的核心收益。
    QAction* action();

    // 以下三个 text/icon/checkable 设置的是"代理 QAction 在没有任何权威 realAction 时"的默认展示，
    // 一旦有权威 realAction 接管，对应的 Update* 属性若开启会覆盖这里设置的值。
    QString defaultText() const { return m_defaultText; }
    void setDefaultText(const QString& text);

    QIcon defaultIcon() const { return m_defaultIcon; }
    void setDefaultIcon(const QIcon& icon);

    bool isDefaultCheckable() const { return m_defaultCheckable; }
    void setDefaultCheckable(bool checkable);

    QList<QKeySequence> defaultShortcuts() const { return m_defaultShortcuts; }
    void setDefaultShortcuts(const QList<QKeySequence>& shortcuts);
    void setDefaultShortcut(const QKeySequence& shortcut);

    // 快捷键只作用于代理，不受 Attributes 影响，也永不被镜像逻辑覆盖。
    QList<QKeySequence> shortcuts() const;
    QKeySequence shortcut() const;
    void setShortcuts(const QList<QKeySequence>& shortcuts);
    void setShortcut(const QKeySequence& shortcut);

    bool isActive() const { return m_active; }
    void setActive(bool active) { m_active = active; }

    // 声明式配置镜像策略；默认值为全部 Update* 开启、HideWhenIdle 关闭
    // （即：全量镜像 realAction 的展示属性，无权威源时仅禁用、保留最后的文案）。
    Attributes attributes() const noexcept { return m_attributes; }
    void setAttributes(Attributes attributes);
    void setAttribute(Attributes attribute, bool on = true);

    // 为该命令注册某个上下文下的真实 QAction。realAction 的生命周期由调用方（编辑器）管理，
    // Command 只持有 QPointer 弱引用；realAction 销毁时会自动从 Command 中摘除并触发重新仲裁。
    // 同一个 context 重复注册视为替换旧绑定。priority 越大越优先，默认 0。
    void addContextAction(QAction* action, const ContextId& context, int priority = 0);
    void removeContextAction(const ContextId& context);
    bool hasContextAction(const ContextId& context) const noexcept;
    std::vector<ContextId> contexts() const;

private:
    int findAuthoritativeIndex() const;

    // 根据 CommandManager 的上下文管理当前激活集合重新计算"权威绑定"，如发生变化则：
    //  1) 断开旧的 proxy -> 旧权威 realAction 转发连接
    //  2) 按 Attributes 策略同步代理的可见性/禁用状态与展示属性
    //  3) 若有新权威源，建立 proxy -> 新权威 realAction 的转发连接
    // 订阅 CommandManager::contextChanged 后自动调用；addContextAction /
    // removeContextAction / realAction 销毁时也会主动调用一次。
    void resyncAuthoritativeBinding();

    void mirrorProperties(const QAction* from, QAction* to);

private:
    struct ContextBinding
    {
        ContextId context;
        QPointer<QAction> realAction;
        int priority = 0;
        QMetaObject::Connection changedConn;
        QMetaObject::Connection destroyedConn;
    };

    CommandManager& m_mgr;
    CommandId m_id;
    QString m_defaultText;
    QIcon m_defaultIcon;
    bool m_defaultCheckable;
    bool m_active;
    bool m_hasShortcuts;
    QList<QKeySequence> m_defaultShortcuts;
    Attributes m_attributes;

    QAction* m_proxyAction{};

    int m_authoritativeIndex = -1;           // m_bindings 中当前权威绑定的下标，-1 表示当前无权威源
    QMetaObject::Connection m_triggeredConn; // proxyAction::triggered -> 权威 realAction::trigger
    QMetaObject::Connection m_toggledConn;   // proxyAction::toggled -> 权威 realAction::toggled
    std::vector<ContextBinding> m_bindings;
};

} // namespace bakuon::gui

Q_DECLARE_OPERATORS_FOR_FLAGS(bakuon::gui::Command::Attributes)
