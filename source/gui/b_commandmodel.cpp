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

bool isContainer(const CommandItem& item)
{
    if (!item.isValid())
        return false;
    const auto type = item.data(CommandItem::ItemRole::TypeRole).value<CommandItem::Type>();
    return type == CommandItem::Type::Container;
}
} // namespace

CommandModel::CommandModel(ICommandLayout* layout, QObject* parent)
    : QAbstractItemModel(parent)
    , m_layout(layout)
{
    Q_ASSERT_X(m_layout != nullptr, "CommandModel", "layout 不能为空");
}

CommandItem CommandModel::itemFromIndex(const QModelIndex& index) const
{
    return index.isValid() ? m_layout->createItem(index.row(), index.internalPointer())
                           : m_layout->invisibleItem();
}

QModelIndex CommandModel::indexFromItem(const CommandItem& item) const
{
    if (!item || item == m_layout->invisibleItem()) {
        return {};
    }
    // TreeNode::index() 是"在父节点子列表中的下标"，与 QModelIndex 的 row 定义完全一致
    return createIndex(item.index(), 0, item.pointer());
}

QModelIndex CommandModel::index(int row, int column, const QModelIndex& parent) const
{
    if (column < 0 || row < 0) {
        return {};
    }
    auto child = m_layout->itemAt(static_cast<std::size_t>(row), itemFromIndex(parent));
    return child.isValid() ? createIndex(row, column, child.pointer()) : QModelIndex{};
}

QModelIndex CommandModel::parent(const QModelIndex& child) const
{
    if (!child.isValid()) {
        return {};
    }
    return indexFromItem(itemFromIndex(child).parent());
}

int CommandModel::rowCount(const QModelIndex& parent) const
{
    auto item = itemFromIndex(parent);
    if (!isContainer(item))
        return 0;
    return static_cast<int>(item.childCount());
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

    auto item       = itemFromIndex(index);
    const auto type = item.data(CommandItem::ItemRole::TypeRole).value<CommandItem::Type>();

    switch (role) {
    case Qt::DisplayRole:
    case Qt::EditRole   : {
        if (index.column() == 0) {
            switch (type) {
            case CommandItem::Type::Root     : break;
            case CommandItem::Type::Container: return item.data(CommandItem::DisplayRole);
            case CommandItem::Type::Command  : {
                // 展示文本实时向 CommandSystem 查询，做到"所见即所得"；
                // 查不到（命令尚未注册/已被移除）时给出明确提示而不是空白，便于排查。
                const QString id = item.data(CommandItem::CommandRole).toString();
                if (Command* cmd = CommandSystem::command(CommandId(id))) {
                    return cmd->action()->text();
                }
                return QStringLiteral("<未知命令: %1>").arg(id);
            }
            case CommandItem::Type::Section  : return item.data(CommandItem::DisplayRole);
            case CommandItem::Type::Separator: return item.data(CommandItem::DisplayRole);
            case CommandItem::Type::Custom   :
            default                          : return {};
            }
        }

        if (type == CommandItem::Type::Command) {
            if (index.column() == 1) {
                return item.data(CommandItem::CommandRole);
            }
            if (index.column() == 2) {
                const QString id    = item.data(CommandItem::CommandRole).toString();
                const auto contexts = CommandSystem::contextsForCommand(CommandId(id));
                QStringList list;
                list.reserve(contexts.size());
                for (const auto& c : contexts) {
                    list.append(c.toString());
                }
                return list.join(" | ");
            }
        }

        break;
    }
    case CommandTypeRole   : return static_cast<int>(type);
    case CommandIdRole     : return item.data(CommandItem::CommandRole);
    case CommandContextRole: {
        const QString id    = item.data(CommandItem::CommandRole).toString();
        const auto contexts = CommandSystem::contextsForCommand(CommandId(id));
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
    if (!index.isValid() || index.column() != 0 || role != Qt::EditRole)
        return false;
    if ((flags(index) & Qt::ItemIsEditable) == 0)
        return false;

    auto item = itemFromIndex(index);

    // 只有菜单节点的标题允许改名；命令节点的文案跟随 realAction 镜像，不可在此编辑
    if (item.data(CommandItem::TypeRole).value<CommandItem::Type>()
        != CommandItem::Type::Container) // 只有 Container 可改标题
        return false;

    m_layout->setItemData(item, CommandItem::DisplayRole, value);
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

    auto item       = itemFromIndex(index);
    const auto type = item.data(CommandItem::TypeRole).value<CommandItem::Type>();

    Qt::ItemFlags f = Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsDragEnabled;
    if (type == CommandItem::Type::Container) {
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

    auto item = itemFromIndex(indexes.first());
    // item->path() 是 TreeNode 自带的方法：从根到该节点、每一层在其父节点中的下标，
    // 恰好就是 Qt QModelIndex 体系定位一个节点所需要的信息，不用再手写路径计算。
    QJsonArray pathJson;
    for (std::size_t r : item.path()) {
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

    auto targetParent = itemFromIndex(parent);
    if (!isContainer(targetParent)) {
        return false; // 叶子节点不能作为容器接收拖放
    }
    const int targetRow = (row < 0) ? static_cast<int>(targetParent.childCount()) : row;

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
        auto item = m_layout->itemFromPath(path);
        if (!item.isValid()) {
            return false;
        }
        QModelIndex sourceParent = indexFromItem(item.parent());
        return moveRow(sourceParent, item.index(), parent, targetRow);
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

    if (!beginMoveRows(sourceParent, sourceRow, sourceRow, destinationParent, destinationChild))
        return false;

    const auto ok = m_layout->move(itemFromIndex(sourceParent),
                                   sourceRow,
                                   itemFromIndex(destinationParent),
                                   destinationChild);
    endMoveRows();
    return ok;
}

bool CommandModel::removeRows(int row, int count, const QModelIndex& parent)
{
    if (count != 1) {
        return false; // 简化：一次只删一行；批量删除由调用方从后往前循环调用
    }
    auto parentItem = itemFromIndex(parent);
    auto child      = m_layout->itemAt(static_cast<std::size_t>(row), parentItem);
    if (!child.isValid()) {
        return false;
    }
    beginRemoveRows(parent, row, row);
    bool ok = m_layout->remove(child);
    endRemoveRows();
    return ok;
}

QModelIndex CommandModel::addContainer(const QModelIndex& parent, int row, const QString& title)
{
    auto parentItem = itemFromIndex(parent);
    if (!isContainer(parentItem))
        return {};

    const int insertRow = (row < 0 || row > static_cast<int>(parentItem.childCount()))
                              ? static_cast<int>(parentItem.childCount())
                              : row;
    beginInsertRows(parent, insertRow, insertRow);
    auto item = m_layout->addContainer(title, parentItem, insertRow);
    endInsertRows();
    return item.isValid() ? createIndex(insertRow, 0, item.pointer()) : QModelIndex{};
}

QModelIndex CommandModel::addCommand(const QModelIndex& parent, int row, const CommandId& id)
{
    auto parentItem = itemFromIndex(parent);
    if (!isContainer(parentItem))
        return {};

    const int insertRow = (row < 0 || row > static_cast<int>(parentItem.childCount()))
                              ? static_cast<int>(parentItem.childCount())
                              : row;
    beginInsertRows(parent, insertRow, insertRow);
    auto item = m_layout->addCommand(id.toString(), parentItem, insertRow);
    endInsertRows();
    return item.isValid() ? createIndex(insertRow, 0, item.pointer()) : QModelIndex{};
}

QModelIndex CommandModel::addSeparator(const QModelIndex& parent, int row)
{
    auto parentItem = itemFromIndex(parent);
    if (!isContainer(parentItem) || parentItem == m_layout->invisibleItem())
        return {};

    const int insertRow = (row < 0 || row > static_cast<int>(parentItem.childCount()))
                              ? static_cast<int>(parentItem.childCount())
                              : row;
    beginInsertRows(parent, insertRow, insertRow);
    auto item = m_layout->addSeparator(parentItem, insertRow);
    endInsertRows();
    return item.isValid() ? createIndex(insertRow, 0, item.pointer()) : QModelIndex{};
}

bool CommandModel::moveItemChecked(const CommandItem& item, const CommandItem& destParent,
                                   int destRow)
{
    if (!item.isValid() || item == m_layout->invisibleItem())
        return false;
    if (!isContainer(destParent))
        return false;

    // 环检测可借助 path / isDescendant 逻辑，或直接让 Layout::move 返回 false
    const int sourceRow         = item.index();
    QModelIndex sourceParentIdx = indexFromItem(item.parent());
    QModelIndex destParentIdx   = indexFromItem(destParent);

    const int clampedDestRow = (destRow < 0) ? static_cast<int>(destParent.childCount()) : destRow;

    if (item.parent() == destParent
        && (clampedDestRow == sourceRow || clampedDestRow == sourceRow + 1))
        return true;

    if (!beginMoveRows(sourceParentIdx, sourceRow, sourceRow, destParentIdx, clampedDestRow))
        return false;

    const bool ok = m_layout->move(item, destParent, clampedDestRow);
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
