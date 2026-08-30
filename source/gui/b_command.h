#pragma once

#include <QtCore/QFlags>
#include <QtCore/QPointer>
#include <QtCore/QString>
#include <QtGui/QAction>
#include <QtGui/QIcon>
#include <QtGui/QKeySequence>

#include "gui/b_gui_export.h"
#include "gui/b_types.h"

namespace bakuon::gui {

/**
 * @brief Command：一个"命令"的逻辑身份（例如"删除""旋转""另存为"）。
 *
 * 这一版彻底移除了 Command 对上下文/仲裁的任何认知——它从头到尾只关心两件事：
 * 一个供外部（菜单/工具栏）使用的代理 QAction，和"当前谁是权威 realAction"。
 * "该用哪个 realAction"这个问题完全由外部（通常是 ContextArbiter）算好之后通过
 * setRealAction() 直接告诉它，Command 不查询任何东西、不认识 ContextState/
 * ContextArbiter 这些类型。单元测试因此不需要任何上下文/仲裁基础设施，
 * 构造一个 Command，手动 new 几个 QAction，直接调 setRealAction() 断言镜像结果即可。
 */
class BAKUON_GUI_EXPORT Command : public QObject
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

    Command(const CommandId& id, QString defaultText, QObject* parent = nullptr);

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
    void setActive(bool active);

    // 声明式配置镜像策略；默认值为全部 Update* 开启、HideWhenIdle 关闭
    // （即：全量镜像 realAction 的展示属性，无权威源时仅禁用、保留最后的文案）。
    Attributes attributes() const noexcept { return m_attributes; }
    void setAttributes(Attributes attributes);
    void setAttribute(Attributes attribute, bool on = true);

    /**
     * @brief 唯一的外部驱动入口：由 ContextArbiter（仲裁结果变化时）或测试代码直接调用。
     * activeAction 为 nullptr 表示"当前没有任何激活上下文为它注册了动作"。
     * 与旧值相同时是no-op（不会重复断开/重建转发连接）。
     */
    void setRealAction(QAction* activeAction);
    /**
     * @brief 当前权威 realAction（可能为 nullptr）；供调试/UI 展示使用，不参与仲裁逻辑。
     */
    QAction* realAction() const noexcept { return m_realAction.data(); }

Q_SIGNALS:
    /// 代理是否处于“可交互权威源已就绪”状态发生变化时发出（菜单启用态、快捷键是否应响应等）
    void activeChanged(bool active);

private:
    void syncCurrentState();
    void mirrorProperties(const QAction* from, QAction* to);

private:
    CommandId m_id;
    QString m_defaultText;
    QIcon m_defaultIcon;
    bool m_defaultCheckable;
    bool m_active;
    bool m_hasShortcuts;
    QList<QKeySequence> m_defaultShortcuts;
    Attributes m_attributes;

    QAction* m_proxyAction{};
    QPointer<QAction> m_realAction; // 当前权威源；nullptr 表示当前无权威源

    QMetaObject::Connection m_changedConn;   // realAction::changed -> 镜像到 proxyAction
    QMetaObject::Connection m_triggeredConn; // proxyAction::triggered -> 权威 realAction::trigger
};

} // namespace bakuon::gui

Q_DECLARE_OPERATORS_FOR_FLAGS(bakuon::gui::Command::Attributes)
