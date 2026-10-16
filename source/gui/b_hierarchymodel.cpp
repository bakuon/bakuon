#include "gui/b_hierarchymodel.h"

#include <type_traits>

#include <QtCore/QItemSelectionModel>

#include <entt/entity/entity.hpp>

#include <bakuon/core/Components.h>

namespace bakuon::gui {
namespace h = bakuon::core::hierarchy;
using namespace bakuon::core;
using bakuon::core::components::Name;

quintptr HierarchyModel::idFromHandle(Entity entity) const noexcept
{
    if (!m_registry.valid(entity)) {
        return 0;
    }
    using Underlying = std::underlying_type_t<Entity>;
    return static_cast<quintptr>(static_cast<Underlying>(entity));
}

Entity HierarchyModel::handleFromId(quintptr id) const noexcept
{
    // NOTE: id == 0 是有效的
    using Underlying = std::underlying_type_t<Entity>;
    return Entity{static_cast<Entity>(static_cast<Underlying>(id))};
}

HierarchyModel::HierarchyModel(core::Registry& registry, QObject* parent)
    : QAbstractItemModel(parent)
    , m_registry(registry)
{
    connectBridge();
}

HierarchyModel::~HierarchyModel()
{
    bindSelection(nullptr);
}

void HierarchyModel::connectBridge()
{
    // ...
}

QModelIndex HierarchyModel::index(int row, int column, const QModelIndex& parent) const
{
    if (row < 0 || column != 0) {
        return {};
    }

    if (!parent.isValid()) {
        std::vector<Entity> rootList;
        h::roots(m_registry, rootList);
        if (row >= static_cast<int>(rootList.size())) {
            return {};
        }
        return createIndex(row, 0, idFromHandle(rootList[static_cast<std::size_t>(row)]));
    }

    const Entity parentHandle = handleFromId(parent.internalId());
    if (!m_registry.valid(parentHandle)) {
        return {};
    }
    const Entity kid = h::child(m_registry, static_cast<std::size_t>(row), parentHandle);
    if (!m_registry.valid(kid)) {
        return {};
    }
    return createIndex(row, 0, idFromHandle(kid));
}

QModelIndex HierarchyModel::parent(const QModelIndex& child) const
{
    if (!child.isValid()) {
        return {};
    }
    const Entity node = handleFromId(child.internalId());
    if (!m_registry.valid(node)) {
        return {};
    }
    const Entity p = h::parent(m_registry, node);
    if (!m_registry.valid(p)) {
        return {}; // 顶层根
    }
    const auto idxOpt = h::index(m_registry, p);
    // 父节点在其兄弟中的行号：若父也是根，用 roots 列表定位
    if (!h::hasParent(m_registry, p)) {
        std::vector<Entity> rootList;
        h::roots(m_registry, rootList);
        for (std::size_t i = 0; i < rootList.size(); ++i) {
            if (rootList[i] == p) {
                return createIndex(static_cast<int>(i), 0, idFromHandle(p));
            }
        }
        return {};
    }
    const int row = idxOpt ? static_cast<int>(*idxOpt) : 0;
    return createIndex(row, 0, idFromHandle(p));
}

int HierarchyModel::rowCount(const QModelIndex& parent) const
{
    if (parent.column() > 0) {
        return 0;
    }
    if (!parent.isValid()) {
        std::vector<Entity> rootList;
        h::roots(m_registry, rootList);
        return static_cast<int>(rootList.size());
    }
    const Entity parentHandle = handleFromId(parent.internalId());
    if (!m_registry.valid(parentHandle)) {
        return 0;
    }
    return static_cast<int>(h::childCount(m_registry, parentHandle));
}

int HierarchyModel::columnCount(const QModelIndex& parent) const
{
    Q_UNUSED(parent);
    return 1;
}

QVariant HierarchyModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid()) {
        return {};
    }
    const Entity node = handleFromId(index.internalId());
    if (!m_registry.valid(node)) {
        return {};
    }

    switch (role) {
    case Qt::DisplayRole:
    case Qt::EditRole   : {
        if (const Name* name = m_registry.try_get<Name>(node)) {
            return QString::fromStdString(name->value);
        }
        return QStringLiteral("Entity %1").arg(static_cast<quint64>(idFromHandle(node)));
    }
    case HandleRole: return QVariant::fromValue(node);
    default        : return {};
    }
}

Qt::ItemFlags HierarchyModel::flags(const QModelIndex& index) const
{
    if (!index.isValid()) {
        return Qt::NoItemFlags;
    }
    return Qt::ItemIsEnabled | Qt::ItemIsSelectable;
}

QVariant HierarchyModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation == Qt::Horizontal && role == Qt::DisplayRole && section == 0) {
        return QStringLiteral("Name");
    }
    return {};
}

Entity HierarchyModel::handleForIndex(const QModelIndex& index) const
{
    if (!index.isValid()) {
        return {};
    }
    return handleFromId(index.internalId());
}

QModelIndex HierarchyModel::indexForHandle(Entity entity) const
{
    if (!m_registry.valid(entity) || !m_registry.valid(entity)) {
        return {};
    }
    if (!m_registry.all_of<h::Hierarchy>(entity)) {
        return {};
    }

    // 自底向上拼路径，再从根走下来建 QModelIndex
    const auto path = h::path(m_registry, entity);
    if (!h::hasParent(m_registry, entity)) {
        // 自身是根
        std::vector<Entity> rootList;
        h::roots(m_registry, rootList);
        for (std::size_t i = 0; i < rootList.size(); ++i) {
            if (rootList[i] == entity) {
                return createIndex(static_cast<int>(i), 0, idFromHandle(entity));
            }
        }
        return {};
    }

    // 找到顶层根
    Entity root = entity;
    while (h::hasParent(m_registry, root)) {
        root = h::parent(m_registry, root);
    }
    std::vector<Entity> rootList;
    h::roots(m_registry, rootList);
    int rootRow = -1;
    for (std::size_t i = 0; i < rootList.size(); ++i) {
        if (rootList[i] == root) {
            rootRow = static_cast<int>(i);
            break;
        }
    }
    if (rootRow < 0) {
        return {};
    }

    QModelIndex idx = createIndex(rootRow, 0, idFromHandle(root));
    for (std::size_t step : path) {
        idx = this->index(static_cast<int>(step), 0, idx);
        if (!idx.isValid()) {
            return {};
        }
    }
    return idx;
}

void HierarchyModel::reload()
{
    beginResetModel();
    endResetModel();
}

void HierarchyModel::onHierarchyChanged(Entity /*entity*/)
{
    // 十字链表局部变化也可能影响兄弟 index；首期用全量重置保持正确性。
    // 后续可按 parent 做 layoutChanged 优化。
    reload();
}

void HierarchyModel::onNameChanged(Entity entity)
{
    const QModelIndex idx = indexForHandle(entity);
    if (idx.isValid()) {
        Q_EMIT dataChanged(idx, idx, {Qt::DisplayRole, Qt::EditRole});
    }
}

void HierarchyModel::bindSelection(QItemSelectionModel* selectionModel)
{
    if (m_viewSelConn) {
        QObject::disconnect(m_viewSelConn);
        m_viewSelConn = {};
    }
    if (m_sessionSelConn) {
        QObject::disconnect(m_sessionSelConn);
        m_sessionSelConn = {};
    }
    m_selModel = selectionModel;
    if (!m_selModel) {
        return;
    }

    m_viewSelConn = connect(m_selModel, &QItemSelectionModel::selectionChanged, this, [this]() {
        onSelectionFromView();
    });

    onSelectionFromSession();
}

void HierarchyModel::onSelectionFromView()
{
    if (m_syncingSelection || !m_selModel) {
        return;
    }
    m_syncingSelection = true;

    std::vector<Entity> handles;
    const QModelIndexList indexes = m_selModel->selectedIndexes();
    handles.reserve(static_cast<std::size_t>(indexes.size()));
    for (const QModelIndex& idx : indexes) {
        const Entity h = handleForIndex(idx);
        if (m_registry.valid(h)) {
            handles.push_back(h);
        }
    }
    m_syncingSelection = false;
}

void HierarchyModel::onSelectionFromSession()
{
    if (m_syncingSelection || !m_selModel) {
        return;
    }
    m_syncingSelection = true;

    QItemSelection itemSel;
    // ...
    m_selModel->select(itemSel, QItemSelectionModel::ClearAndSelect);

    m_syncingSelection = false;
}

} // namespace bakuon::gui
