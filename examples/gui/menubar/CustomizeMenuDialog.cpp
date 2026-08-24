#include "CustomizeMenuDialog.h"

#include <QtCore/QDir>
#include <QtWidgets/QMessageBox>

#include <gui/b_commandmodel.h>

namespace bakuon::examples {

namespace {
// 演示用固定路径；实际产品应使用
// QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation) + 应用/用户维度的子路径。
QString layoutFilePath()
{
    return QDir::temp().filePath(QStringLiteral("bakuon_menu_layout.json"));
}
} // namespace

CustomizeMenuDialog::CustomizeMenuDialog(gui::CommandModel* model, QWidget* parent)
    : QDialog(parent)
    , m_model(model)
{
    setWindowTitle(QStringLiteral("自定义菜单布局"));

    m_tree = new QTreeView(this);
    m_tree->setModel(model);
    // m_tree->setHeaderHidden(true);
    m_tree->setDragEnabled(true);
    m_tree->setAcceptDrops(true);
    m_tree->setDropIndicatorShown(true);
    m_tree->setDragDropMode(QAbstractItemView::InternalMove);
    m_tree->expandAll();

    auto* upBtn     = new QPushButton(QStringLiteral("上移"), this);
    auto* downBtn   = new QPushButton(QStringLiteral("下移"), this);
    auto* removeBtn = new QPushButton(QStringLiteral("删除"), this);
    auto* saveBtn   = new QPushButton(QStringLiteral("保存布局"), this);
    auto* loadBtn   = new QPushButton(QStringLiteral("加载布局"), this);

    connect(upBtn, &QPushButton::clicked, this, [this]() { moveSelected(-1); });
    connect(downBtn, &QPushButton::clicked, this, [this]() { moveSelected(+1); });
    connect(removeBtn, &QPushButton::clicked, this, [this]() {
        const QModelIndex idx = m_tree->currentIndex();
        if (idx.isValid()) {
            m_model->removeRows(idx.row(), 1, idx.parent());
        }
    });
    connect(saveBtn, &QPushButton::clicked, this, [this]() {
        if (m_model->saveToFile(layoutFilePath())) {
            QMessageBox::information(this,
                                     QStringLiteral("保存布局"),
                                     QStringLiteral("已保存到:\n%1").arg(layoutFilePath()));
        }
    });
    connect(loadBtn, &QPushButton::clicked, this, [this]() {
        if (m_model->loadFromFile(layoutFilePath())) {
            m_tree->expandAll();
        }
    });

    auto* btnLayout = new QHBoxLayout;
    btnLayout->addWidget(upBtn);
    btnLayout->addWidget(downBtn);
    btnLayout->addWidget(removeBtn);
    btnLayout->addStretch();
    btnLayout->addWidget(saveBtn);
    btnLayout->addWidget(loadBtn);

    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->addWidget(new QLabel(QStringLiteral("拖拽调整顺序/层级；双击菜单项可重命名"), this));
    mainLayout->addWidget(m_tree);
    mainLayout->addLayout(btnLayout);
}
void CustomizeMenuDialog::showEvent(QShowEvent* event)
{
    resize(420, 480);

    QDialog::showEvent(event);
}

void CustomizeMenuDialog::moveSelected(int direction)
{
    const QModelIndex idx = m_tree->currentIndex();
    if (!idx.isValid()) {
        return;
    }
    const QModelIndex parent = idx.parent();
    const int row            = idx.row();
    const int siblingCount   = m_model->rowCount(parent);

    int destRow = -1;
    if (direction < 0 && row > 0) {
        destRow = row - 1; // 上移一位
    } else if (direction > 0 && row < siblingCount - 1) {
        destRow = row + 2; // 下移一位（moveRows 语义：目的地行号按移除前的下标给出）
    } else {
        return; // 已在边界，无法继续移动
    }
    m_model->move(idx, parent, destRow);
}

} // namespace bakuon::examples
