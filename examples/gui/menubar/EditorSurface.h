// 示例程序：演示"代理 QAction"命令系统 + CommandModel/MenuBarBuilder 菜单自定义布局。
//
// 核心链路：Command 内部持有一个代理 QAction（放入菜单/工具栏），各编辑器自己创建、
// 自己 connect 业务逻辑的"真实" QAction 通过 Command::registerContextAction(context, realAction)
// 注册进来；代理 QAction 的展示状态镜像"当前权威 realAction"，点击代理转发为对权威
// realAction 的 trigger() 调用（详见 Command.h 头注释）。
//
// 本版新增：菜单栏不再硬编码 menuBar()->addMenu()/addAction()，而是：
//   CommandManager（命令目录，含具体行为） -> CommandModel（纯布局结构，可编辑/可存盘）
//   -> MenuBarBuilder::build()（把布局渲染成真实 QMenuBar）
// "自定义菜单布局…" 对话框直接编辑 CommandModel（拖拽重排/改名/上移下移/删除），
// 编辑会实时反映到真实菜单栏上；"保存/加载布局"按钮把结构序列化到 JSON 文件。

#include <QtGui/QContextMenuEvent>
#include <QtWidgets/QDialog>
#include <QtWidgets/QLabel>

#include <gui/b_commandsystem.h>

using bakuon::gui::Context;
using bakuon::gui::ContextId;

namespace bakuon::examples {

// EditorSurface：两类编辑器画布的公共基类（与上一轮改造完全一致，未改动）。
class EditorSurface : public QWidget
{
    Q_OBJECT
public:
    EditorSurface(const QString& label, const ContextId& focusContext,
                  const ContextId& selectedContext, QWidget* parent = nullptr);

    Context context() const;

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;

private:
    void setupRealActions(const QString& label);

    ContextId m_selectedContext;
    ContextId m_focusContext;
    bool m_selected            = false;
    QLabel* m_hint             = nullptr;
    QAction* m_deleteAction    = nullptr;
    QAction* m_duplicateAction = nullptr;
};

} // namespace bakuon::examples
