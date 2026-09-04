#include "gui/b_commandmodel.h"

#include <QtCore/QFile>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QMimeData>

#include "gui/b_commandsystem.h"

namespace bakuon::gui {

namespace {
constexpr auto kKeySource          = "source";
constexpr auto kKeyPath            = "path";
constexpr auto kSourceInternalMove = "internal-move";

bool isContainer(const CommandLayout::Item* item)
{
    return item->data().type != CommandLayoutData::Type::Command
           && item->data().type != CommandLayoutData::Type::Separator;
}
} // namespace

CommandModel::CommandModel(CommandLayout* layout, QObject* parent)
    : QAbstractItemModel(parent)
    , m_layout(layout)
{
    Q_ASSERT_X(m_layout != nullptr, "CommandModel", "layout 不能为空");
}

CommandModel::Item* CommandModel::itemFromIndex(const QModelIndex& index) const
{
    return index.isValid() ? static_cast<Item*>(index.internalPointer()) : m_layout->root();
}

QModelIndex CommandModel::indexFromItem(Item* item) const
{
    if (!item || item->isRoot()) {
        return {};
    }
    // TreeNode::index() 是"在父节点子列表中的下标"，与 QModelIndex 的 row 定义完全一致
    return createIndex(static_cast<int>(item->index()), 0, item);
}

QModelIndex CommandModel::index(int row, int column, const QModelIndex& parent) const
{
    if (column < 0 || row < 0) {
        return {};
    }
    Item* child = itemFromIndex(parent)->childAt(static_cast<std::size_t>(row));
    return child ? createIndex(row, column, child) : QModelIndex{};
}

QModelIndex CommandModel::parent(const QModelIndex& child) const
{
    if (!child.isValid()) {
        return {};
    }
    return indexFromItem(static_cast<Item*>(child.internalPointer())->parent());
}

int CommandModel::rowCount(const QModelIndex& parent) const
{
    const Item* item = itemFromIndex(parent);
    const auto type  = item->data().type;
    if (type == CommandLayoutData::Type::Command || type == CommandLayoutData::Type::Separator) {
        return 0; // 叶子节点不允许有子行
    }
    return static_cast<int>(item->childCount());
}

int CommandModel::columnCount(const QModelIndex& /*parent*/) const
{
    return 3;
}

QVariant CommandModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid()) {
        return {};
    }
    const auto& v = static_cast<Item*>(index.internalPointer())->data();
    switch (role) {
    case Qt::DisplayRole:
        if (v.type == CommandLayoutData::Type::Command) {
            if (index.column() == 1) {
                return v.commandId.toString();
            }
            if (index.column() == 2) {
                const auto contexts = CommandSystem::contextsForCommand(v.commandId);
                QStringList list;
                list.reserve(contexts.size());
                for (const auto& c : contexts) {
                    list.append(c.toString());
                }
                return list.join(" | ");
            }
        }
        Q_FALLTHROUGH();
    case Qt::EditRole: {
        if (index.column() == 0) {
            switch (v.type) {
            case CommandLayoutData::Type::Root     : break;
            case CommandLayoutData::Type::Container: return v.title;
            case CommandLayoutData::Type::Command  : {
                // 展示文本实时向 CommandSystem 查询，做到"所见即所得"；
                // 查不到（命令尚未注册/已被移除）时给出明确提示而不是空白，便于排查。
                if (Command* cmd = CommandSystem::command(v.commandId)) {
                    return cmd->action()->text();
                }
                return QStringLiteral("<未知命令: %1>").arg(v.commandId.toString());
            }
            case CommandLayoutData::Type::Separator: return QStringLiteral("──────────");
            default                                : break;
            }
        }
        break;
    }
    case CommandTypeRole   : return static_cast<int>(v.type);
    case CommandIdRole     : return v.commandId.toString();
    case CommandContextRole: {
        const auto contexts = CommandSystem::contextsForCommand(v.commandId);
        QStringList list;
        list.reserve(contexts.size());
        for (const auto& c : contexts) {
            list.append(c.toString());
        }
        return list.join(" | ");
    }
    default: break;
    }
    return {};
}

bool CommandModel::setData(const QModelIndex& index, const QVariant& value, int role)
{
    if (!index.isValid() || index.column() != 0 || role != Qt::EditRole
        || (flags(index) & Qt::ItemIsEditable) == 0) {
        return false;
    }

    auto* item = static_cast<Item*>(index.internalPointer());
    if (item->data().type != CommandLayoutData::Type::Container) {
        return false; // 只有菜单节点的标题允许改名；命令节点的文案跟随 realAction 镜像，不可在此编辑
    }
    item->data().title = value.toString();
    emit dataChanged(index, index, {Qt::DisplayRole, Qt::EditRole});
    return true;
}

QVariant CommandModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
        return QAbstractItemModel::headerData(section, orientation, role);

    switch (section) {
    case 0 : return QLatin1String("Title");
    case 1 : return QLatin1String("Command");
    case 2 : return QLatin1String("Contexts");
    default: break;
    }
    return {};
}

Qt::ItemFlags CommandModel::flags(const QModelIndex& index) const
{
    if (!index.isValid()) {
        return Qt::ItemIsDropEnabled; // 允许拖放到空白区域（即根节点/菜单栏顶层）
    }
    const auto& v   = static_cast<Item*>(index.internalPointer())->data();
    Qt::ItemFlags f = Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsDragEnabled;
    if (v.type == CommandLayoutData::Type::Container) {
        if (index.column() == 0) {
            f |= Qt::ItemIsDropEnabled; // 只有菜单节点能作为容器接收拖放
            f |= Qt::ItemIsEditable;    // 菜单标题可双击改名
        }
    }
    return f;
}

Qt::DropActions CommandModel::supportedDropActions() const
{
    return Qt::MoveAction | Qt::CopyAction;
}

QStringList CommandModel::mimeTypes() const
{
    return {QString::fromLatin1(kMimeType), QString::fromLatin1(kExternalCommandMimeType)};
}

QMimeData* CommandModel::mimeData(const QModelIndexList& indexes) const
{
    if (indexes.isEmpty()) {
        return nullptr;
    }
    // 简化处理：一次只支持拖拽一个节点，理由同前一版。
    auto* item = static_cast<Item*>(indexes.first().internalPointer());

    // item->paths() 是 TreeNode 自带的方法：从根到该节点、每一层在其父节点中的下标，
    // 恰好就是 Qt QModelIndex 体系定位一个节点所需要的信息，不用再手写路径计算。
    QJsonArray pathJson;
    for (std::size_t r : item->paths()) {
        pathJson.append(static_cast<qint64>(r));
    }
    QJsonObject payload;
    payload[QLatin1String(kKeySource)] = QLatin1String(kSourceInternalMove);
    payload[QLatin1String(kKeyPath)]   = pathJson;

    auto* mime = new QMimeData();
    mime->setData(QString::fromLatin1(kMimeType),
                  QJsonDocument(payload).toJson(QJsonDocument::Compact));
    return mime;
}

bool CommandModel::dropMimeData(const QMimeData* data, Qt::DropAction action, int row,
                                int /*column*/, const QModelIndex& parent)
{
    if (action == Qt::IgnoreAction) {
        return true;
    }
    Item* targetParent = itemFromIndex(parent);
    if (!isContainer(targetParent)) {
        return false; // 叶子节点不能作为容器接收拖放
    }
    const int targetRow = (row < 0) ? static_cast<int>(targetParent->childCount()) : row;

    if (data->hasFormat(QString::fromLatin1(kMimeType))) {
        const QJsonObject payload
            = QJsonDocument::fromJson(data->data(QString::fromLatin1(kMimeType))).object();
        if (payload.value(QLatin1String(kKeySource)).toString()
            != QLatin1String(kSourceInternalMove)) {
            return false;
        }
        std::vector<std::size_t> path;
        for (const auto v : payload.value(QLatin1String(kKeyPath)).toArray()) {
            path.push_back(static_cast<std::size_t>(v.toInt()));
        }

        // pathNode() 是 TreeNode 自带的方法：按行路径从根出发定位节点，路径失效（比如拖拽
        // 过程中模型发生了其它结构变化）时返回 nullptr，不用再手写逐层校验的循环。
        Item* item = m_layout->root()->pathNode(path);
        if (!item) {
            return false;
        }
        return moveItemChecked(item, targetParent, targetRow);
    }

    if (data->hasFormat(QString::fromLatin1(kExternalCommandMimeType))) {
        // 供外部"可用命令列表"拖入使用：mime 内容直接是 CommandId 字符串（UTF-8）
        const CommandId id{
            QString::fromUtf8(data->data(QString::fromLatin1(kExternalCommandMimeType)))};
        return addCommand(parent, targetRow, id).isValid();
    }

    return false;
}

bool CommandModel::moveRows(const QModelIndex& sourceParent, int sourceRow, int count,
                            const QModelIndex& destinationParent, int destinationChild)
{
    if (count != 1) {
        return false; // 简化：一次只移动一行
    }
    Item* item = itemFromIndex(sourceParent)->childAt(static_cast<std::size_t>(sourceRow));
    if (!item) {
        return false;
    }

    return moveItemChecked(item, itemFromIndex(destinationParent), destinationChild);
}

bool CommandModel::removeRows(int row, int count, const QModelIndex& parent)
{
    if (count != 1) {
        return false; // 简化：一次只删一行；批量删除由调用方从后往前循环调用
    }
    Item* parentItem = itemFromIndex(parent);
    Item* child      = parentItem->childAt(static_cast<std::size_t>(row));
    if (!child) {
        return false;
    }
    beginRemoveRows(parent, row, row);
    bool ok = m_layout->removeItem(child);
    endRemoveRows();
    return ok;
}

QModelIndex CommandModel::addMenu(const QModelIndex& parent, int row, const QString& title)
{
    Item* parentItem = itemFromIndex(parent);
    const auto v     = parentItem->data();
    if (v.type == CommandLayoutData::Type::Command || v.type == CommandLayoutData::Type::Separator) {
        return {}; // 提前拒绝，不触碰 begin/end 系列信号——理由同 moveNodeChecked 的注释
    }
    const int insertRow = (row < 0 || row > static_cast<int>(parentItem->childCount()))
                              ? static_cast<int>(parentItem->childCount())
                              : row;
    beginInsertRows(parent, insertRow, insertRow);
    auto item = m_layout->addMenu(parentItem, static_cast<std::size_t>(insertRow), title);
    endInsertRows();
    return item ? createIndex(insertRow, 0, item) : QModelIndex{};
}

QModelIndex CommandModel::addCommand(const QModelIndex& parent, int row, const CommandId& id)
{
    Item* parentItem = itemFromIndex(parent);
    const auto v     = parentItem->data();
    if (v.type == CommandLayoutData::Type::Command || v.type == CommandLayoutData::Type::Separator) {
        return {};
    }
    const int insertRow = (row < 0 || row > static_cast<int>(parentItem->childCount()))
                              ? static_cast<int>(parentItem->childCount())
                              : row;
    beginInsertRows(parent, insertRow, insertRow);
    auto item = m_layout->addCommand(parentItem, static_cast<std::size_t>(insertRow), id);
    endInsertRows();
    return item ? createIndex(insertRow, 0, item) : QModelIndex{};
}

QModelIndex CommandModel::addSeparator(const QModelIndex& parent, int row)
{
    Item* parentItem = itemFromIndex(parent);
    const auto v     = parentItem->data();
    if (parentItem->isRoot() || v.type == CommandLayoutData::Type::Command
        || v.type == CommandLayoutData::Type::Separator) {
        return {}; // 根节点下不允许分隔线，叶子节点不能作为容器——同样提前拒绝
    }
    const int insertRow = (row < 0 || row > static_cast<int>(parentItem->childCount()))
                              ? static_cast<int>(parentItem->childCount())
                              : row;
    beginInsertRows(parent, insertRow, insertRow);
    auto item = m_layout->addSeparator(parentItem, static_cast<std::size_t>(insertRow));
    endInsertRows();
    return item ? createIndex(insertRow, 0, item) : QModelIndex{};
}

bool CommandModel::move(const QModelIndex& index, const QModelIndex& newParent, int newRow)
{
    if (!index.isValid()) {
        return false;
    }

    return moveItemChecked(static_cast<Item*>(index.internalPointer()),
                           itemFromIndex(newParent),
                           newRow);
}

bool CommandModel::moveItemChecked(Item* item, Item* destParent, int destRow)
{
    Item* sourceParent = item->parent();
    if (!sourceParent) {
        return false; // 不能移动根节点
    }
    // 以下两条校验与 CommandLayout::moveNode 内部的校验完全重复——这是刻意的：
    // CommandModel 必须在调用 beginMoveRows() 之前就知道这次移动是否合法，
    // 因为 Qt 的模型协议要求 begin/endMoveRows 之间必须真的发生了一次移动，
    // 不能"先 begin，最后发现不合法又不移动"。如果未来 CommandLayout::moveNode
    // 增加新的失败条件，必须同步补充到这里，否则会破坏这个协议前提。
    if (destParent == item || destParent->isDescendantOf(item)) {
        return false;
    }
    if (destParent->data().type == CommandLayoutData::Type::Command
        || destParent->data().type == CommandLayoutData::Type::Separator) {
        return false;
    }

    const int sourceRow               = static_cast<int>(item->index());
    const QModelIndex sourceParentIdx = indexFromItem(sourceParent);
    const QModelIndex destParentIdx   = indexFromItem(destParent);
    const int clampedDestRow = (destRow < 0 || destRow > static_cast<int>(destParent->childCount()))
                                   ? static_cast<int>(destParent->childCount())
                                   : destRow;

    if (sourceParent == destParent
        && (clampedDestRow == sourceRow || clampedDestRow == sourceRow + 1)) {
        return true; // Qt 语义下这就是"原地不动"，直接视为成功
    }
    if (!beginMoveRows(sourceParentIdx, sourceRow, sourceRow, destParentIdx, clampedDestRow)) {
        return false; // 目的地行号落在 Qt 禁止的区间内
    }
    const bool ok = m_layout->moveItem(item, destParent, clampedDestRow);
    endMoveRows();
    return ok;
}

bool CommandModel::saveToFile(const QString& path) const
{
    return m_layout->save(path);
}

bool CommandModel::loadFromFile(const QString& path)
{
    beginResetModel();
    const bool ok = m_layout->load(path);
    endResetModel();
    return ok;
}

} // namespace bakuon::gui
