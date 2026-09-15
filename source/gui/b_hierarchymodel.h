#pragma once

#include <QtCore/QAbstractItemModel>
#include <QtCore/QHash>

#include <bakuon/core/Handle.h>
#include <bakuon/core/Hierarchy.h>
#include <bakuon/core/Registry.h>

#include "gui/b_gui_export.h"

QT_BEGIN_NAMESPACE
class QItemSelectionModel;
QT_END_NAMESPACE

namespace bakuon::gui {

class DocumentSession;

/**
 * @brief 基于 core::hierarchy 的大纲树模型（QAbstractItemModel）。
 *
 * - 虚拟根：所有 hierarchy 根节点作为顶层行。
 * - DisplayRole：优先 components::Name，否则 Handle 的调试字符串。
 * - UserRole：存放 core::Handle（已 Q_DECLARE_METATYPE）。
 * - 数据变更通过 DocumentSession::bridge() 的 Hierarchy/Name 信号增量刷新。
 *
 * ## 索引编码
 * internalId 使用 entt entity 的整型表示；虚拟根为 0（无效 Handle）。
 * 不缓存完整树指针，每次导航走 hierarchy:: API，保证与 Registry 一致。
 *
 * ## 选择同步（可选）
 * 调用 bindSelection(QItemSelectionModel*) 后：
 * - 视图选中 → 写入 session.selection()
 * - session.selectionChanged → 回写视图选中（避免环：用标志位）
 */
class BAKUON_GUI_EXPORT HierarchyModel : public QAbstractItemModel
{
    Q_OBJECT
public:
    enum Roles {
        HandleRole = Qt::UserRole + 1,
    };

    explicit HierarchyModel(DocumentSession& session, QObject* parent = nullptr);
    ~HierarchyModel() override;

    // QAbstractItemModel
    [[nodiscard]] QModelIndex index(int row, int column,
                                    const QModelIndex& parent = QModelIndex()) const override;
    [[nodiscard]] QModelIndex parent(const QModelIndex& child) const override;
    [[nodiscard]] int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    [[nodiscard]] int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    [[nodiscard]] Qt::ItemFlags flags(const QModelIndex& index) const override;
    [[nodiscard]] QVariant headerData(int section, Qt::Orientation orientation,
                                      int role = Qt::DisplayRole) const override;

    [[nodiscard]] core::Handle handleForIndex(const QModelIndex& index) const;
    [[nodiscard]] QModelIndex indexForHandle(core::Handle handle) const;

    /// 全量重置（结构大变时）；日常优先走 bridge 信号增量路径
    void reload();

    /// 双向绑定视图选中与 session.selection()；传 nullptr 解除
    void bindSelection(class QItemSelectionModel* selectionModel);

private:
    void connectBridge();
    void onHierarchyChanged(core::Handle handle);
    void onNameChanged(core::Handle handle);
    void onSelectionFromView();
    void onSelectionFromSession();

    [[nodiscard]] static quintptr idFromHandle(core::Handle h) noexcept;
    [[nodiscard]] static core::Handle handleFromId(quintptr id) noexcept;

    DocumentSession& m_session;
    core::Registry& m_registry;

    QItemSelectionModel* m_selModel = nullptr;
    QMetaObject::Connection m_viewSelConn;
    QMetaObject::Connection m_sessionSelConn;
    bool m_syncingSelection = false;
};

} // namespace bakuon::gui
