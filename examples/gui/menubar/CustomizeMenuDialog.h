#pragma once

#include <QtWidgets/QBoxLayout>
#include <QtWidgets/QDialog>
#include <QtWidgets/QLabel>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QTreeView>

namespace bakuon::gui {
class CommandModel;
}

namespace bakuon::examples {

// CustomizeMenuDialog：编辑 CommandModel 的最小化对话框。
// 用 QTreeView 直接绑定模型即可获得"双击改名""拖拽重排/改变层级"的能力
// （改名靠 Qt::ItemIsEditable，拖拽靠 CommandModel 已实现的 mimeData/dropMimeData）；
// "上移/下移/删除"额外提供按钮操作，比纯拖拽更适合精确调整，也更容易测试。
class CustomizeMenuDialog : public QDialog
{
    Q_OBJECT
public:
    explicit CustomizeMenuDialog(gui::CommandModel* model, QWidget* parent = nullptr);

private:
    void moveSelected(int direction);

    gui::CommandModel* m_model;
    QTreeView* m_tree = nullptr;
};

} // namespace bakuon::examples
