#pragma once

#include <cstddef>
#include <memory>

#include "gui/b_gui_export.h"
#include "gui/detail/b_treenode.h"

#include <bakuon/gui/ICommandLayout.h>

namespace bakuon::gui {

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
class BAKUON_GUI_EXPORT CommandLayout : public ICommandLayout
{
public:
    using ItemDataList = std::unordered_map<int, QVariant>;
    using Node         = TreeNode<ItemDataList>;

    CommandLayout();
    ~CommandLayout() override = default;

    CommandLayout(const CommandLayout &)                = delete;
    CommandLayout &operator=(const CommandLayout &)     = delete;
    CommandLayout(CommandLayout &&) noexcept            = default;
    CommandLayout &operator=(CommandLayout &&) noexcept = default;

    [[nodiscard]] Node *node(const CommandItem &item) const;

    // 整棵树的遍历视图，直接转发到根节点。
    Generator<Node *> traverse(TraversalOrder order = TraversalOrder::PreOrder) const
    {
        return m_root->descendants(order);
    }

    template<typename Pred>
    auto filtered(Pred pred) const
    {
        return m_root->filteredDescendants(std::move(pred));
    }

    [[nodiscard]] CommandItem invisibleItem() const override;
    [[nodiscard]] CommandItem parentItem(const CommandItem &child) const override;
    [[nodiscard]] CommandItem itemAt(std::size_t index, const CommandItem &parent) const override;
    [[nodiscard]] std::size_t count(const CommandItem &parent = {}) const noexcept override;
    [[nodiscard]] std::size_t size() const noexcept override;
    [[nodiscard]] bool isEmpty() const noexcept override;
    [[nodiscard]] int itemIndex(const CommandItem &item) const override;
    [[nodiscard]] std::size_t itemDepth(const CommandItem &item) const override;
    [[nodiscard]] std::vector<std::size_t> itemPath(const CommandItem &item) const override;
    [[nodiscard]] CommandItem itemFromPath(std::span<const std::size_t> path) const noexcept override;
    [[nodiscard]] bool isValidPath(std::span<const std::size_t> path) const noexcept override;

    // 将已存在的 item 插入到 parent 下（用于 takeAt 后重新挂接，或外部构造的临时项）
    bool add(CommandItem item, CommandItem parent, int index = -1) override;
    bool remove(CommandItem item) override;
    CommandItem take(CommandItem parent, int index) override;
    bool move(CommandItem sourceItem, CommandItem targetParent, int targetIndex) override;
    bool move(CommandItem sourceParent, int sourceIndex, CommandItem targetParent,
              int targetIndex) override;

    CommandItem addContainer(const QString &title, CommandItem parent = {}, int index = -1) override;
    CommandItem addCommand(const QString &id, CommandItem parent, int index = -1) override;
    CommandItem addSeparator(CommandItem parent, int index = -1) override;
    CommandItem addSection(const QString &title, CommandItem parent, int index = -1) override;

    QVariant itemData(CommandItem item, int role) const override;
    void setItemData(CommandItem item, int role, const QVariant &value) override;

    [[nodiscard]] QJsonObject serialize() const override;
    void deserialize(const QJsonObject &obj) override; // 整体替换当前结构

    bool save(const QString &filePath) const override;
    bool load(const QString &filePath) override;

private:
    static Node *insertChild(Node *parent, ItemDataList data, int index);
    static QJsonObject nodeToJson(const Node *n);
    static void populateFromJson(Node *parent, const QJsonArray &arr);

private:
    std::unique_ptr<Node> m_root;
};

} // namespace bakuon::gui
