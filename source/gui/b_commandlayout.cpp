#include "gui/b_commandlayout.h"

#include <QtCore/QDebug>
#include <QtCore/QFile>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>

namespace bakuon::gui {

namespace {
constexpr auto kKeyType     = "type";
constexpr auto kKeyTitle    = "title";
constexpr auto kKeyCommand  = "command";
constexpr auto kKeyChildren = "children";
constexpr auto kKeyVersion  = "version";
constexpr auto kKeyLayout   = "layout";

bool isContainer(const CommandLayout::Item* item)
{
    return item->data().type != CommandLayoutData::Type::Command
           && item->data().type != CommandLayoutData::Type::Separator;
}
} // namespace

CommandLayout::CommandLayout()
{
    // TreeNode() 默认构造得到一个 T{} 数据的哨兵节点；CommandLayoutNode::type
    // 的默认值就是 Type::Root，因此这里不需要再手动赋值。
    m_root = std::make_unique<CommandLayout::Item>();
}

CommandLayout::Item* CommandLayout::addContainer(Item* parent, std::size_t index,
                                                 const QString& title)
{
    if (!parent) {
        parent = m_root.get();
    }
    if (!isContainer(parent)) {
        qWarning() << "CommandLayout: Sub-nodes cannot be added to leaf nodes (commands/separator)";
        return nullptr;
    }
    CommandLayoutData data;
    data.type  = CommandLayoutData::Type::Container;
    data.title = title;
    return parent->insertChildAt(index, std::move(data));
}

CommandLayout::Item* CommandLayout::addMenu(Item* parent, std::size_t index, const QString& title)
{
    return addContainer(parent, index, title);
}

CommandLayout::Item* CommandLayout::addCommand(Item* parent, std::size_t index, const CommandId& id)
{
    if (!parent) {
        parent = m_root.get();
    }
    if (!isContainer(parent)) {
        qWarning() << "CommandLayout: Sub-nodes cannot be added to leaf nodes (commands/separator)";
        return nullptr;
    }
    CommandLayoutData data;
    data.type      = CommandLayoutData::Type::Command;
    data.commandId = id;
    return parent->insertChildAt(index, std::move(data));
}

CommandLayout::Item* CommandLayout::addSeparator(Item* parent, std::size_t index)
{
    if (!parent) {
        parent = m_root.get();
    }
    if (parent->isRoot()) {
        // QMenuBar 没有 addSeparator()（顶层菜单之间没有"分隔线"这个概念），
        // 在数据层就拒绝这种无法渲染的结构，比等到 MenuBarBuilder 渲染时才发现更早暴露问题。
        qWarning()
            << "CommandLayout::addSeparator:The top-level menu bar does not support a separator.";
        return nullptr;
    }
    if (!isContainer(parent)) {
        qWarning() << "CommandLayout: Sub-nodes cannot be added to leaf nodes (commands/separator)";
        return nullptr;
    }
    CommandLayoutData data;
    data.type  = CommandLayoutData::Type::Separator;
    data.title = QStringLiteral("──────────");
    return parent->insertChildAt(index, std::move(data));
}

bool CommandLayout::removeItem(Item* item)
{
    if (!item || item->isRoot()) {
        return false;
    }
    item->remove(); // TreeNode 自带：extract() + 递归析构整棵子树
    return true;
}

bool CommandLayout::moveItem(Item* srcParent, int srcIndex, Item* destParent, int destIndex)
{
    // BUGFIX: 原先错误地拒绝了 srcParent 为根的情况，导致无法移动顶层菜单/工具栏节点。
    // 真正禁止移动的是根节点自身，而不是“父节点是根”的子节点。
    if (!srcParent) {
        return false;
    }

    if (!destParent) {
        destParent = m_root.get();
    }

    auto* srcNode = srcParent->childAt(static_cast<std::size_t>(srcIndex));
    if (!srcNode || srcNode->isRoot()) {
        return false; // 禁止移动根节点，或源下标越界
    }
    if (destParent == srcNode || destParent->isDescendantOf(srcNode)) {
        return false; // 环路保护：不能把节点拖进它自己的子孙里
    }

    if (!isContainer(destParent)) {
        return false; // 叶子节点不能作为容器
    }

    // beforeChild 在"移动前"的子节点列表里查找——这是 TreeNode 句柄式 API 的核心价值：
    // 摘除 node 不会改变 beforeChild 自身的身份，因此这里完全不需要像基于整数下标的
    // 实现那样手动处理"先移除导致后续下标整体前移一位"的修正。
    auto* destNode    = destParent->childAt(static_cast<std::size_t>(destIndex));
    auto* beforeChild = (destIndex < static_cast<int>(destParent->childCount())) ? destNode
                                                                                 : nullptr;
    if (beforeChild == srcNode) {
        return true; // 目标位置就是自己当前所在的位置，视为无操作成功
    }
    if (beforeChild && srcNode->parent() == destParent && srcNode->nextSibling() == beforeChild) {
        return true; // "移动到当前下一个兄弟之前"等价于原地不动
    }

    try {
        srcNode->moveAsChild(destParent, beforeChild);
    } catch (const std::exception& e) {
        qWarning() << "CommandLayout::moveItem:" << e.what();
        return false;
    }
    return true;
}

bool CommandLayout::moveItem(Item* item, Item* destParent, std::size_t destIndex)
{
    if (!item || item->isRoot()) {
        return false; // 禁止移动根节点
    }

    if (!destParent) {
        destParent = m_root.get();
    }

    if (destParent == item || destParent->isDescendantOf(item)) {
        return false; // 环路保护：不能把节点拖进它自己的子孙里
    }

    if (!isContainer(destParent)) {
        return false; // 叶子节点不能作为容器
    }

    Item* beforeChild = (destIndex < destParent->childCount()) ? destParent->childAt(destIndex)
                                                               : nullptr;

    if (beforeChild == item) {
        return true; // 目标位置就是自己当前所在的位置，视为无操作成功
    }
    if (beforeChild && item->parent() == destParent && item->nextSibling() == beforeChild) {
        return true; // "移动到当前下一个兄弟之前"等价于原地不动
    }

    try {
        item->moveAsChild(destParent, beforeChild);
    } catch (const std::exception& e) {
        qWarning() << "CommandLayout::moveNode:" << e.what();
        return false;
    }
    return true;
}

static QJsonObject nodeToJson(const CommandLayout::Item* item)
{
    QJsonObject obj;
    const CommandLayoutData& v = item->data();
    switch (v.type) {
    case CommandLayoutData::Type::Root: break; // 不会被调用到：nodeToJson 只对非根节点递归调用
    case CommandLayoutData::Type::Container:
        obj[QLatin1String(kKeyType)]  = QStringLiteral("container");
        obj[QLatin1String(kKeyTitle)] = v.title;
        break;
    case CommandLayoutData::Type::Command:
        obj[QLatin1String(kKeyType)]    = QStringLiteral("command");
        obj[QLatin1String(kKeyCommand)] = v.commandId.name();
        break;
    case CommandLayoutData::Type::Separator:
        obj[QLatin1String(kKeyType)] = QStringLiteral("separator");
        break;
    default: break;
    }

    if (v.type == CommandLayoutData::Type::Container) {
        QJsonArray children;
        for (auto* child : item->children()) { // TreeNode 自带的 O(1) 双向子节点视图
            children.append(nodeToJson(child));
        }
        obj[QLatin1String(kKeyChildren)] = children;
    }
    return obj;
}

static void populateFromJson(CommandLayout::Item* parent, const QJsonArray& childrenJson)
{
    // 必须自顶向下、边解析边挂接（parent->insertChildAt/appendChild 之后立刻拿到已挂接的
    // 节点指针再递归处理其子节点），因为 TreeNode 只提供"用一个值在某个已存在节点下
    // 构造新子节点"的公开接口，没有"把一整棵已经在内存里搭好的游离子树整体挂上去"的接口——
    // 这与自底向上先构建完整子树、最后再整体挂接的写法（很多树的常见写法）不同，
    // 是使用这个特定 TreeNode API 时需要注意的一点。
    for (const auto v : childrenJson) {
        const QJsonObject obj = v.toObject();
        const QString type    = obj.value(QLatin1String(kKeyType)).toString();

        CommandLayoutData data;
        if (type == QStringLiteral("container")) {
            data.type  = CommandLayoutData::Type::Container;
            data.title = obj.value(QLatin1String(kKeyTitle)).toString();
        } else if (type == QStringLiteral("command")) {
            data.type      = CommandLayoutData::Type::Command;
            data.commandId = CommandId{obj.value(QLatin1String(kKeyCommand)).toString()};
        } else if (type == QStringLiteral("separator")) {
            data.type = CommandLayoutData::Type::Separator;
        } else {
            qWarning() << "CommandLayout: An unknown node type was encountered in JSON and has "
                          "been skipped:"
                       << type;
            continue;
        }

        auto node = parent->appendChild(std::move(data));
        if (node->data().type == CommandLayoutData::Type::Container) {
            populateFromJson(node, obj.value(QLatin1String(kKeyChildren)).toArray());
        }
    }
}

QJsonObject CommandLayout::serialize() const
{
    QJsonObject root;
    root[QLatin1String(kKeyVersion)] = 1; // 预留格式版本号，便于未来结构升级时做兼容处理
    QJsonArray children;
    for (Item* child : m_root->children()) {
        children.append(nodeToJson(child));
    }
    root[QLatin1String(kKeyLayout)] = children;
    return root;
}

void CommandLayout::deserialize(const QJsonObject& root)
{
    m_root = std::make_unique<Item>(); // 整体替换：先重置为一棵只有哨兵根节点的空树
    const QJsonArray children = root.value(QLatin1String(kKeyLayout)).toArray();
    populateFromJson(m_root.get(), children);
}

bool CommandLayout::save(const QString& path) const
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        qWarning() << "CommandLayout::saveToFile: Unable to open file for writing" << path
                   << file.errorString();
        return false;
    }
    file.write(QJsonDocument(serialize()).toJson(QJsonDocument::Indented));
    return true;
}

bool CommandLayout::load(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "CommandLayout::loadFromFile: Unable to open file for reading" << path
                   << file.errorString();
        return false;
    }
    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        qWarning() << "CommandLayout::loadFromFile: JSON parsing failed" << err.errorString();
        return false;
    }
    deserialize(doc.object());
    return true;
}

} // namespace bakuon::gui
