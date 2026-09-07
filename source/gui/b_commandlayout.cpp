#include "gui/b_commandlayout.h"

#include <QtCore/QDebug>
#include <QtCore/QFile>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>

namespace bakuon::gui {

CommandLayout::CommandLayout()
    : m_root(std::make_unique<Node>())
{
    // 根节点标记为 Container，并作为 invisibleRoot
    m_root->data()[CommandItem::TypeRole] = QVariant::fromValue<CommandItem::Type>(
        CommandItem::Type::Container);
}
CommandLayout::Node *CommandLayout::node(const CommandItem &item) const
{
    return item.isValid() ? static_cast<Node *>(item.pointer()) : m_root.get();
}

CommandItem CommandLayout::invisibleItem() const
{
    return createItem(m_root.get());
}

CommandItem CommandLayout::parentItem(const CommandItem &child) const
{
    Node *n = node(child);
    if (!n || n == m_root.get()) {
        return {};
    }
    return createItem(n->parent());
}

CommandItem CommandLayout::itemAt(std::size_t index, const CommandItem &parent) const
{
    Node *p = node(parent);
    if (!p || index >= p->childCount())
        return {};
    if (Node *child = p->childAt(index))
        return createItem(child);
    return {};
}

std::size_t CommandLayout::count(const CommandItem &parent) const noexcept
{
    Node *p = node(parent);
    return p ? p->childCount() : 0;
}

std::size_t CommandLayout::size() const noexcept
{
    // 含 invisible root
    return m_root ? m_root->subtreeSize() : 0;
}

bool CommandLayout::isEmpty() const noexcept
{
    return !m_root || m_root->childCount() == 0;
}

int CommandLayout::itemIndex(const CommandItem &item) const
{
    Node *n = node(item);
    if (!n || n == m_root.get())
        return -1;
    return static_cast<int>(n->index()); // O(1) 缓存
}

std::size_t CommandLayout::itemDepth(const CommandItem &item) const
{
    Node *n = node(item);
    if (!n)
        return 0;
    // invisible root 深度视为 0，其子节点从 1 开始也可，这里直接返回缓存值
    return n->depth();
}

std::vector<std::size_t> CommandLayout::itemPath(const CommandItem &item) const
{
    Node *n = node(item);
    if (!n || n == m_root.get())
        return {};
    return n->path();
}

CommandItem CommandLayout::itemFromPath(std::span<const std::size_t> path) const noexcept
{
    if (!m_root)
        return {};
    Node *n = m_root->pathNode(path);
    return n ? createItem(n) : CommandItem{};
}

bool CommandLayout::isValidPath(std::span<const std::size_t> path) const noexcept
{
    return itemFromPath(path).isValid();
}

bool CommandLayout::add(CommandItem item, CommandItem parent, int index)
{
    Node *src = node(item);
    Node *dst = node(parent);
    if (!src || !dst || src->parent() != nullptr) // 必须是已摘除的
        return false;
    if (dst == src || dst->isDescendantOf(src))
        return false;

    Node *before = nullptr;
    if (index >= 0 && static_cast<std::size_t>(index) < dst->childCount())
        before = dst->childAt(static_cast<std::size_t>(index));

    // 重新获得所有权并挂接
    std::unique_ptr<Node> owned(src);
    try {
        dst->attachChild(std::move(owned), before);
    } catch (const std::exception &e) {
        qWarning() << "CommandLayout::add:" << e.what();
        return false;
    }
    return true;
}

bool CommandLayout::remove(CommandItem item)
{
    Node *n = node(item);
    if (!n || n == m_root.get())
        return false;
    n->remove();
    return true;
}

CommandItem CommandLayout::take(CommandItem parent, int index)
{
    Node *p = node(parent);
    if (!p || index < 0 || static_cast<std::size_t>(index) >= p->childCount())
        return {};

    Node *child = p->childAt(static_cast<std::size_t>(index));
    if (!child)
        return {};

    // 摘除，所有权暂时由局部 unique_ptr 持有
    std::unique_ptr<Node> owned = child->extract();
    // 这里可以把 owned 存进一个“游离节点表”，或直接返回句柄
    // 最简单的做法：释放到裸指针，由调用方保证后续会 re-attach 或手动管理
    Node *raw                   = owned.release();
    return createItem(raw);
}

bool CommandLayout::move(CommandItem sourceItem, CommandItem targetParent, int targetIndex)
{
    Node *src  = node(sourceItem);
    Node *dest = node(targetParent);
    if (!src || !dest || src == m_root.get())
        return false;
    if (dest == src || dest->isDescendantOf(src))
        return false;

    Node *before = nullptr;
    if (targetIndex >= 0 && static_cast<std::size_t>(targetIndex) < dest->childCount())
        before = dest->childAt(static_cast<std::size_t>(targetIndex));

    if (before == src)
        return true;
    if (before && src->parent() == dest && src->nextSibling() == before)
        return true;

    try {
        src->moveAsChild(dest, before);
    } catch (const std::exception &e) {
        qWarning() << "CommandLayout::move:" << e.what();
        return false;
    }
    return true;
}

bool CommandLayout::move(CommandItem sourceParent, int sourceIndex, CommandItem targetParent,
                         int targetIndex)
{
    CommandItem srcItem = itemAt(static_cast<std::size_t>(sourceIndex), sourceParent);
    if (!srcItem.isValid())
        return false;
    return move(srcItem, targetParent, targetIndex);
}

CommandItem CommandLayout::addContainer(const QString &title, CommandItem parent, int index)
{
    Node *p = node(parent);
    if (!p)
        return {};

    ItemDataList data;
    data[CommandItem::DisplayRole] = title;
    data[CommandItem::TypeRole]    = QVariant::fromValue(CommandItem::Type::Container);

    Node *child = insertChild(p, std::move(data), index);
    return child ? createItem(child) : CommandItem{};
}

CommandItem CommandLayout::addCommand(const QString &id, CommandItem parent, int index)
{
    Node *p = node(parent);
    if (!p)
        return {};

    ItemDataList data;
    data[CommandItem::TypeRole]    = QVariant::fromValue(CommandItem::Type::Command);
    data[CommandItem::CommandRole] = id;

    Node *child = insertChild(p, std::move(data), index);
    return child ? createItem(child) : CommandItem{};
}

CommandItem CommandLayout::addSeparator(CommandItem parent, int index)
{
    Node *p = node(parent);
    if (!p || p == m_root.get()) {
        // 与历史行为一致：顶层（invisible root）不允许 separator
        qWarning() << "CommandLayout: root does not support separator";
        return {};
    }

    ItemDataList data;
    data[CommandItem::DisplayRole] = QStringLiteral("──────────");
    data[CommandItem::TypeRole]    = QVariant::fromValue(CommandItem::Type::Separator);

    Node *child = insertChild(p, std::move(data), index);
    return child ? createItem(child) : CommandItem{};
}

CommandItem CommandLayout::addSection(const QString &title, CommandItem parent, int index)
{
    Node *p = node(parent);
    if (!p || p == m_root.get()) {
        // 与历史行为一致：顶层（invisible root）不允许 section
        qWarning() << "CommandLayout: root does not support section header";
        return {};
    }

    ItemDataList data;
    data[CommandItem::DisplayRole] = title;
    data[CommandItem::TypeRole]    = QVariant::fromValue(CommandItem::Type::Section);

    Node *child = insertChild(p, std::move(data), index);
    return child ? createItem(child) : CommandItem{};
}

QVariant CommandLayout::itemData(CommandItem item, int role) const
{
    Node *n = node(item);
    if (!n)
        return {};
    auto it = n->data().find(role);
    return it != n->data().end() ? it->second : QVariant{};
}

void CommandLayout::setItemData(CommandItem item, int role, const QVariant &value)
{
    Node *n = node(item);
    if (!n || n == m_root.get())
        return;
    n->data()[role] = value;
}

QJsonObject CommandLayout::serialize() const
{
    QJsonObject root;
    root[QStringLiteral("version")] = 1;
    QJsonArray children;
    if (m_root) {
        for (Node *c : m_root->children())
            children.append(nodeToJson(c));
    }
    root[QStringLiteral("layout")] = children;
    return root;
}

void CommandLayout::deserialize(const QJsonObject &obj)
{
    m_root                                = std::make_unique<Node>();
    m_root->data()[CommandItem::TypeRole] = QVariant::fromValue(CommandItem::Type::Container);

    const QJsonArray arr = obj.value(QStringLiteral("layout")).toArray();
    populateFromJson(m_root.get(), arr);
}

bool CommandLayout::save(const QString &filePath) const
{
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        qWarning() << "CommandLayout::save: cannot open" << filePath << file.errorString();
        return false;
    }
    file.write(QJsonDocument(serialize()).toJson(QJsonDocument::Indented));
    return true;
}

bool CommandLayout::load(const QString &filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "CommandLayout::load: cannot open" << filePath << file.errorString();
        return false;
    }
    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        qWarning() << "CommandLayout::load: JSON error" << err.errorString();
        return false;
    }
    deserialize(doc.object());
    return true;
}

CommandLayout::Node *CommandLayout::insertChild(Node *parent, ItemDataList data, int index)
{
    if (index < 0 || static_cast<std::size_t>(index) >= parent->childCount())
        return parent->appendChild(std::move(data));
    return parent->insertChild(static_cast<std::size_t>(index), std::move(data));
}

QJsonObject CommandLayout::nodeToJson(const Node *n)
{
    QJsonObject obj;
    const auto &data = n->data();

    const auto type = CommandItem::Type(data.at(CommandItem::TypeRole).toInt());
    switch (type) {
    case CommandItem::Type::Container: // Container
        obj[QStringLiteral("type")]  = QStringLiteral("container");
        obj[QStringLiteral("title")] = data.at(CommandItem::DisplayRole).toString();
        break;
    case CommandItem::Type::Command: // Command
        obj[QStringLiteral("type")]    = QStringLiteral("command");
        obj[QStringLiteral("command")] = data.at(CommandItem::CommandRole).toString();
        break;
    case CommandItem::Type::Separator: // Separator
        obj[QStringLiteral("type")] = QStringLiteral("separator");
        break;
    case CommandItem::Type::Section: // Section
        obj[QStringLiteral("title")] = data.at(CommandItem::DisplayRole).toString();
        obj[QStringLiteral("type")]  = QStringLiteral("section");
        break;
    case CommandItem::Type::Root:
    case CommandItem::Type::Custom:
    default                       : break;
    }

    if (type == CommandItem::Type::Container) {
        QJsonArray children;
        for (Node *c : n->children())
            children.append(nodeToJson(c));
        obj[QStringLiteral("children")] = children;
    }
    return obj;
}

void CommandLayout::populateFromJson(Node *parent, const QJsonArray &arr)
{
    for (const auto v : arr) {
        const QJsonObject obj = v.toObject();
        const QString type    = obj.value(QStringLiteral("type")).toString();

        ItemDataList data;
        if (type == QStringLiteral("container")) {
            data[CommandItem::TypeRole]    = QVariant::fromValue(CommandItem::Type::Container);
            data[CommandItem::DisplayRole] = obj.value(QStringLiteral("title")).toString();
        } else if (type == QStringLiteral("command")) {
            data[CommandItem::TypeRole]    = QVariant::fromValue(CommandItem::Type::Command);
            data[CommandItem::CommandRole] = obj.value(QStringLiteral("command")).toString();
        } else if (type == QStringLiteral("separator")) {
            data[CommandItem::TypeRole]    = QVariant::fromValue(CommandItem::Type::Separator);
            data[CommandItem::DisplayRole] = QStringLiteral("──────────");
        } else if (type == QStringLiteral("section")) {
            data[CommandItem::TypeRole]    = QVariant::fromValue(CommandItem::Type::Section);
            data[CommandItem::DisplayRole] = obj.value(QStringLiteral("section")).toString();
        } else {
            qWarning() << "CommandLayout: unknown type skipped:" << type;
            continue;
        }

        Node *child = parent->appendChild(std::move(data));
        if (type == QStringLiteral("container")) {
            populateFromJson(child, obj.value(QStringLiteral("children")).toArray());
        }
    }
}

} // namespace bakuon::gui
