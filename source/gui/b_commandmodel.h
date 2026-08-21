#pragma once

#include <QtCore/QAbstractItemModel>
#include <QtCore/QJsonObject>

#include "gui/b_commandlayout.h"

namespace bakuon::gui {

// CommandModel：把 CommandLayout（纯数据）适配成 Qt Model/View 体系能理解的
// QAbstractItemModel，供 QTreeView 等控件绑定、编辑（拖拽重排/改名/上移下移/删除）。
//
// 本类不拥有、也不缓存任何布局数据本身——internalPointer 直接就是
// CommandLayout::Node*，一切读操作（index/parent/rowCount/data）直接查询
// CommandLayout 的树；一切结构性写操作（addMenu/addCommand/addSeparator/
// moveRows/removeRows/loadLayoutFromFile）都转调 CommandLayout 对应的方法，
// 只是额外包一层 beginXxx/endXxx 让 Qt 视图知道发生了什么变化。
//
// 生命周期：调用方必须保证传入的 CommandLayout* 比本 CommandModel 活得久
// （典型做法：CommandLayout 是宿主窗口的成员，CommandModel 随该窗口一起创建/销毁）。
// 一旦某个 CommandLayout 已经被某个 CommandModel 包裹，后续所有结构性编辑都应该
// 通过这个 CommandModel 进行，不要绕开它直接调用 CommandLayout 的编辑方法
// ——详见 CommandLayout.h 头部的生命周期约束说明。
class CommandModel final : public QAbstractItemModel
{
    Q_OBJECT
public:
    enum Role {
        CommandTypeRole = Qt::UserRole + 1,
        CommandIdRole,
        CommandContextRole,
    };

    static constexpr char kMimeType[]                = "application/x-bakuon-commandmodel-node";
    static constexpr char kExternalCommandMimeType[] = "application/x-bakuon-commandid";

    explicit CommandModel(CommandLayout* layout, QObject* parent = nullptr);

    // ---- QAbstractItemModel 接口 ----
    [[nodiscard]] QModelIndex index(int row, int column,
                                    const QModelIndex& parent = {}) const override;
    [[nodiscard]] QModelIndex parent(const QModelIndex& child) const override;
    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] int columnCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    bool setData(const QModelIndex& index, const QVariant& value, int role = Qt::EditRole) override;
    QVariant headerData(int section, Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;
    [[nodiscard]] Qt::ItemFlags flags(const QModelIndex& index) const override;
    [[nodiscard]] Qt::DropActions supportedDropActions() const override;
    [[nodiscard]] QStringList mimeTypes() const override;
    [[nodiscard]] QMimeData* mimeData(const QModelIndexList& indexes) const override;
    bool dropMimeData(const QMimeData* data, Qt::DropAction action, int row, int column,
                      const QModelIndex& parent) override;
    bool moveRows(const QModelIndex& sourceParent, int sourceRow, int count,
                  const QModelIndex& destinationParent, int destinationChild) override;
    bool removeRows(int row, int count, const QModelIndex& parent = {}) override;

    // ---- 结构性编辑 API（转调 CommandLayout，并补上对应的 Qt Model 信号）----
    QModelIndex addMenu(const QModelIndex& parent, int row, const QString& title);
    QModelIndex addCommand(const QModelIndex& parent, int row, const CommandId& id);
    QModelIndex addSeparator(const QModelIndex& parent, int row);
    bool move(const QModelIndex& index, const QModelIndex& newParent, int newRow);

    bool saveToFile(const QString& path) const;
    // 整体重置：包一层 beginResetModel/endResetModel 后转调 CommandLayout::loadFromFile
    bool loadFromFile(const QString& path);

private:
    using Item = CommandLayout::Item;

    [[nodiscard]] Item* itemFromIndex(const QModelIndex& index) const;
    [[nodiscard]] QModelIndex indexFromItem(Item* item) const;

    // moveRows() 与 dropMimeData() 的内部移动分支共用同一套"前置校验 + begin/endMoveRows"逻辑
    bool moveItemChecked(Item* item, Item* destParent, int destRow);

    CommandLayout* m_layout = nullptr; // 非拥有，见类文档的生命周期约束
};

} // namespace bakuon::gui
