#pragma once

#include <QtCore/QDebug>
#include <QtCore/QJsonObject>

#include "gui/b_types.h"
#include "gui/detail/b_treenode.h"

namespace bakuon::gui {

class CommandLayout;

// CommandLayoutData：CommandLayout 树中每个节点携带的数据（TreeNode<T> 的 T）。
// 纯数据结构，不含任何 Qt Model/View 相关内容。
struct CommandLayoutData
{
    enum class Type : quint8 {
        Root,      // 仅根节点使用，从不出现在序列化结果里（根是隐式的容器，不作为一个"节点"落盘）
        Container, // 一级/多级子菜单或工具栏容器
        Command,   // 引用一个 CommandId
        Separator, // 分隔线
    };

    Type type = Type::Root;
    QString title;       // 仅 Menu 节点使用（支持 & 助记符）
    CommandId commandId; // 仅 Command 节点使用
};

/** CommandLayout
 * CommandLayout：菜单/工具栏"分组布局"的纯数据层——树形结构 + 序列化，
 * 不依赖 QAbstractItemModel、不依赖任何 QWidget，只依赖 Qt Core（QString/QJsonObject 等）
 * 和项目自带的通用侵入式树 bakuon::details::TreeNode<T>。
 *
 * 与 CommandModel（Qt Model/View 适配器）的关系：
 *   CommandLayout 是"数据"，CommandModel 是"给 QTreeView 用的视图适配器"，两者分层：
 *     MainWindow 可以直接持有并操作一个 CommandLayout（例如程序启动时搭建默认布局），
 *     完全不需要创建 CommandModel/QTreeView；
 *     只有当需要把布局交给用户在 QTreeView 里可视化编辑时，才用一个 CommandModel
 *     包一层同一个 CommandLayout 实例。
 *
 * !!! 生命周期约束（务必遵守）!!!
 *   一旦某个 CommandLayout 实例已经被某个 CommandModel 包裹、且该 CommandModel
 *   已经绑定到一个存活的 QTreeView，就不要再绕开 CommandModel、直接调用
 *   CommandLayout 的结构性编辑方法（addMenu/addCommand/addSeparator/moveNode/
 *   removeNode/loadFromJson/loadFromFile）——这些改动不会经过 beginInsertRows 等
 *   Qt Model 信号，视图会静默失去同步，进而在后续操作中出现越界/崩溃。
 *   正确用法：结构性编辑要么在还没有 CommandModel 包裹时直接操作 CommandLayout
 *   （典型场景：启动时搭建默认布局），要么在已有 CommandModel 时一律通过
 *   CommandModel 的接口去改（CommandModel 内部会转调 CommandLayout 并补上信号）。
 *   只读操作（exportToJson/saveToFile）任何时候直接调用都是安全的。
 */
class CommandLayout
{
public:
    using Item = TreeNode<CommandLayoutData>;

    CommandLayout();
    CommandLayout(const CommandLayout&)                = delete;
    CommandLayout& operator=(const CommandLayout&)     = delete;
    CommandLayout(CommandLayout&&) noexcept            = default;
    CommandLayout& operator=(CommandLayout&&) noexcept = default;
    ~CommandLayout() = default; // 根节点的 unique_ptr 递归释放整棵树

    Item* root() noexcept { return m_root.get(); }
    const Item* root() const noexcept { return m_root.get(); }

    std::size_t size() const noexcept { return m_root ? m_root->subtreeSize() : 0; }
    bool empty() const noexcept { return m_root == nullptr; }

    // 按行路径(从根出发的下标序列)定位节点；非法路径返回 nullptr。
    Item* pathItem(std::span<const std::size_t> path) const noexcept
    {
        return m_root ? m_root->pathNode(path) : nullptr;
    }

    bool isValidPath(std::span<const std::size_t> path) const noexcept
    {
        return pathItem(path) != nullptr;
    }

    // 整棵树的遍历视图，直接转发到根节点。
    Generator<Item*> traverse(TraversalOrder order = TraversalOrder::PreOrder) const
    {
        return m_root->descendants(order);
    }

    template<typename Pred>
    auto filtered(Pred pred) const
    {
        return m_root->filteredDescendants(std::move(pred));
    }

    Item* addContainer(Item* parent, std::size_t index, const QString& title);
    Item* addMenu(Item* parent, std::size_t index, const QString& title);
    Item* addCommand(Item* parent, std::size_t index, const CommandId& id);
    Item* addSeparator(Item* parent, std::size_t index);

    bool removeItem(Item* item);
    bool moveItem(Item* srcParent, int srcIndex, Item* destParent, int destIndex);
    bool moveItem(Item* item, Item* destParent, std::size_t destIndex);

    // ---- 序列化 ----
    QJsonObject serialize() const;
    void deserialize(const QJsonObject& root); // 整体替换当前结构
    bool save(const QString& path) const;
    bool load(const QString& path);

private:
    std::unique_ptr<Item> m_root;
};

} // namespace bakuon::gui
