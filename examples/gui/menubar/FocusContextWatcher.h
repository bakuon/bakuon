#pragma once

#include <QObject>
#include <QPointer>
#include <QWidget>

#include <gui/b_commandsystem.h>

// Warning: 已废弃，请使用 gui::ContextFocusRouter 代替

namespace bakuon::examples {

// 动态属性的键名：用于在任意 QObject/QWidget 上标记"获得焦点时应激活的上下文"。
//
// 设计动机：早期版本要求承载编辑器的 QWidget 额外继承一个纯抽象接口
// （class ImageCanvas : public QWidget, public IContextProvider），
// 这在以下场景会带来不必要的耦合：
//   1. 通过 Qt Designer "提升为(Promote to)" 生成/复用的控件，改动继承链成本较高；
//   2. 已有较深继承体系的第三方/历史控件，插入一层接口继承可能引发菱形继承等风险；
//   3. 接口方法 contextId() 通常只是返回一个编译期常量，用虚函数分发有点"杀鸡用牛刀"。
// 改为 Qt 原生的动态属性机制后，任何 QObject 派生类——无论是否可修改其源码——
// 都可以在运行期被"贴标签"接入上下文系统，不侵入类型定义、不引入额外继承关系。
inline constexpr char kContextIdPropertyName[] = "bakuon_contextId";

// 便捷函数：为一个 widget（或任意 QObject）标记其上下文 ID。
// 通常在构造函数中调用一次即可，之后 FocusContextWatcher 会自动识别。
inline void setWidgetContext(QObject* widget, const gui::ContextId& context)
{
    Q_ASSERT(widget != nullptr);
    widget->setProperty(kContextIdPropertyName, context.name());
}

// 便捷函数：读取一个 widget 标记的上下文 ID；未标记过则返回一个 !isValid() 的 ContextId。
[[nodiscard]] inline gui::ContextId widgetContext(const QObject* widget)
{
    const QVariant v = widget->property(kContextIdPropertyName);
    return v.isValid() ? gui::ContextId{v.toString()} : gui::ContextId{};
}

// FocusContextWatcher：安装在 QApplication 上的全局事件过滤器。
//
// 监听 QEvent::FocusIn，沿"获得焦点的部件 -> 父级部件链"向上查找第一个
// 通过 setWidgetContext() 标记过上下文属性的部件（这样焦点落在编辑器内部子控件，
// 例如图像编辑器里的一个 QLineEdit 属性输入框时，外层编辑器仍能被正确识别），
// 自动为其 push 对应上下文；焦点转移到其它已标记部件（或完全离开时）自动 pop。
// 切换时严格保证"先 push 新的、再 pop 旧的"（实现细节见 .cpp），避免中间出现
// 新旧上下文都未激活的过渡态。
//
// 另外会主动忽略两类噪声事件：(1) 压根不是 QWidget 的 QObject（比如内部样式
// 对象、承载顶层窗口的 QWidgetWindow）；(2) focusPolicy() == Qt::NoFocus 的
// QWidget（典型如 QMainWindow 自身，默认就是 NoFocus）。Qt 会在窗口激活等场景
// 下向这两类目标补发 FocusIn 用于内部簿记，这不代表真实的焦点转移，必须过滤掉，
// 否则会被误判成"焦点离开了所有已标记部件"而错误地 pop 当前上下文。
//
// 用法：
//     auto* watcher = new FocusContextWatcher(qApp);
//     qApp->installEventFilter(watcher);
// 之后任何调用过 bakuon::command::setWidgetContext() 的 QWidget 都会被自动纳管，
// 不要求该 QWidget 继承任何额外接口。
class FocusContextWatcher final : public QObject
{
    Q_OBJECT
public:
    explicit FocusContextWatcher(QObject* parent = nullptr);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    // 当前因焦点而处于激活状态的 (widget, context)，焦点转移时用于精确 pop 掉旧的引用
    QPointer<QWidget> m_focusedProviderWidget;
    gui::ContextId m_focusedContext;
};

} // namespace bakuon::examples
