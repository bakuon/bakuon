#pragma once

#include <algorithm>
#include <cassert>
#include <coroutine>
#include <cstddef>
#include <memory>
#include <ranges>
#include <span>
#include <stdexcept>
#include <utility>
#include <vector>

namespace bakuon::gui {

// =============================================================================
//  Generator<T> —— C++20 协程生成器
//
//  标准库要到 C++23 才提供 std::generator，这里手写一个最小但完整可用的
//  版本，用于实现"惰性"的先序/后序/层序遍历视图：调用者每次解引用迭代器
//  才会真正 resume 协程、产出下一个节点，不会预先构造整棵遍历结果的
//  vector，天然适合和 std::ranges 管道 (views::filter / views::transform)
//  组合使用。
// =============================================================================
template<typename T>
class Generator : public std::ranges::view_interface<Generator<T>>
{
public:
    struct promise_type
    {
        T m_value{};

        Generator get_return_object() { return Generator{handle_type::from_promise(*this)}; }
        std::suspend_always initial_suspend() noexcept { return {}; }
        std::suspend_always final_suspend() noexcept { return {}; }
        std::suspend_always yield_value(T v)
        {
            m_value = v;
            return {};
        }
        void return_void() noexcept {}
        [[noreturn]] void unhandled_exception()
        {
            std::rethrow_exception(std::current_exception());
        }
    };
    using handle_type = std::coroutine_handle<promise_type>;

    Generator() noexcept = default;
    explicit Generator(handle_type h) noexcept
        : m_handle(h)
    {
    }
    Generator(const Generator&) = delete;
    Generator(Generator&& other) noexcept
        : m_handle(std::exchange(other.m_handle, {}))
    {
    }
    Generator& operator=(Generator&& other) noexcept
    {
        if (this != &other) {
            if (m_handle)
                m_handle.destroy();
            m_handle = std::exchange(other.m_handle, {});
        }
        return *this;
    }
    ~Generator()
    {
        if (m_handle)
            m_handle.destroy();
    }

    struct iterator
    {
        using iterator_category = std::input_iterator_tag;
        using value_type        = T;
        using difference_type   = std::ptrdiff_t;
        using pointer           = void;
        using reference         = T;

        handle_type m_handle{};

        iterator() = default;
        explicit iterator(handle_type h)
            : m_handle(h)
        {
        }

        iterator& operator++()
        {
            m_handle.resume();
            return *this;
        }
        void operator++(int) { ++(*this); }
        T operator*() const { return m_handle.promise().m_value; }

        bool operator==(std::default_sentinel_t) const noexcept
        {
            return !m_handle || m_handle.done();
        }
    };

    iterator begin()
    {
        if (m_handle)
            m_handle.resume();
        return iterator{m_handle};
    }
    std::default_sentinel_t end() noexcept { return {}; }

private:
    handle_type m_handle{};
};

// =============================================================================
//  前置声明
// =============================================================================
template<typename T>
class TreeNode;

// 遍历顺序
enum class TraversalOrder { PreOrder, PostOrder, LevelOrder };

// =============================================================================
//  ChildRange<T> —— 直接子节点的 O(1) 双向 ranges 视图
//
//  基于十字链表指针直接构造迭代器，不做任何拷贝/搬移，满足
//  std::ranges::bidirectional_range，可直接接入 views::filter /
//  views::transform / views::reverse 等管道操作。
// =============================================================================
template<typename T>
class ChildRange : public std::ranges::view_interface<ChildRange<T>>
{
public:
    class iterator
    {
    public:
        using iterator_category = std::bidirectional_iterator_tag;
        using value_type        = TreeNode<T>*;
        using difference_type   = std::ptrdiff_t;
        using pointer           = void;
        using reference         = TreeNode<T>*;

        iterator() = default;
        iterator(TreeNode<T>* owner, TreeNode<T>* cur)
            : owner_(owner)
            , cur_(cur)
        {
        }

        TreeNode<T>* operator*() const noexcept { return cur_; }

        iterator& operator++() noexcept
        {
            cur_ = cur_->nextSibling();
            return *this;
        }
        iterator operator++(int) noexcept
        {
            auto tmp = *this;
            ++(*this);
            return tmp;
        }
        iterator& operator--() noexcept
        {
            cur_ = cur_ ? cur_->prevSibling() : (owner_ ? owner_->lastChild() : nullptr);
            return *this;
        }
        iterator operator--(int) noexcept
        {
            auto tmp = *this;
            --(*this);
            return tmp;
        }
        bool operator==(const iterator& rhs) const noexcept { return cur_ == rhs.cur_; }

    private:
        TreeNode<T>* owner_ = nullptr; // 用于从 end() 回退到 lastChild()
        TreeNode<T>* cur_   = nullptr;
    };

    ChildRange() = default;
    explicit ChildRange(TreeNode<T>* owner) noexcept
        : m_owner(owner)
    {
    }

    iterator begin() const noexcept
    {
        return iterator(m_owner, m_owner ? m_owner->firstChild() : nullptr);
    }
    iterator end() const noexcept { return iterator(m_owner, nullptr); }

private:
    TreeNode<T>* m_owner = nullptr;
};

// =============================================================================
//  TreeNode<T>
// =============================================================================
template<typename T>
class TreeNode
{
public:
    using value_type = T;

public:
    TreeNode() = default; // 仅供构造虚拟/哨兵根节点使用

    explicit TreeNode(T data)
        : m_data(std::move(data))
    {
    }

    // 禁用拷贝语义，防止树形结构意外深拷贝导致性能问题，显式保留移动语义
    TreeNode(const TreeNode&)                = delete;
    TreeNode& operator=(const TreeNode&)     = delete;
    TreeNode(TreeNode&&) noexcept            = default;
    TreeNode& operator=(TreeNode&&) noexcept = default;
    // 整个子树会通过 unique_ptr 链条自动引发递归干净析构
    ~TreeNode()                              = default;

    // ---------------------------------------------------------------
    //  数据访问
    //  TODO: 是否需要支持 setValue(T value)?
    // ---------------------------------------------------------------
    value_type& data() noexcept { return m_data; }
    const value_type& data() const noexcept { return m_data; }

    // ---------------------------------------------------------------
    //  拓扑查询 —— 均为 O(1)，直接来自十字链表指针
    // ---------------------------------------------------------------
    TreeNode* parent() const noexcept { return m_parent; }
    TreeNode* firstChild() const noexcept { return m_firstChild.get(); }
    TreeNode* lastChild() const noexcept { return m_lastChild; }
    TreeNode* nextSibling() const noexcept { return m_nextSibling.get(); }
    TreeNode* prevSibling() const noexcept { return m_prevSibling; }

    bool isRoot() const noexcept { return m_parent == nullptr; }
    bool isLeaf() const noexcept { return m_firstChild == nullptr; }
    bool hasChildren() const noexcept { return m_firstChild != nullptr; }
    std::size_t childCount() const noexcept { return m_childCount; }

    // ---------------------------------------------------------------
    //  索引 / 谱系查询 —— O(depth) 或 O(index)，文档中明确标注复杂度
    // ---------------------------------------------------------------

    // 按下标取子节点；O(index)。
    TreeNode* childAt(std::size_t index) const noexcept
    {
        auto* n = m_firstChild.get();
        for (std::size_t i = 0; n && i < index; ++i)
            n = n->m_nextSibling.get();
        return n;
    }

    // 在父节点子列表中的下标；O(k)，k 为下标本身(需要沿 m_prevSibling 回溯计数)。
    std::size_t index() const noexcept
    {
        std::size_t row = 0;
        for (auto* s = m_prevSibling; s; s = s->m_prevSibling)
            ++row;
        return row;
    }

    // 节点深度(根为 0)；O(depth)。
    std::size_t depth() const noexcept
    {
        std::size_t d = 0;
        for (auto* p = m_parent; p; p = p->m_parent)
            ++d;
        return d;
    }

    // 子树节点总数(含自身)；O(n)，遍历整棵子树。
    std::size_t subtreeSize() const noexcept
    {
        std::size_t n = 1;
        for (auto* c = m_firstChild.get(); c; c = c->m_nextSibling.get())
            n += c->subtreeSize();
        return n;
    }

    // 从当前节点到根的"谱系"，下标 0 为自身，最后一个为根。
    std::vector<TreeNode*> lineages() const
    {
        std::vector<TreeNode*> out;
        for (auto* n = const_cast<TreeNode*>(this); n; n = n->m_parent)
            out.push_back(n);
        return out;
    }

    // 从根到当前节点的"行路径"(row path)：每一层在其父节点中的下标。
    // 例如 [0, 2, 1] 表示 root->child(0)->child(2)->child(1) == this。
    // 这正是 Qt QModelIndex 体系中定位一个节点所需要的信息。
    std::vector<std::size_t> paths() const
    {
        std::vector<std::size_t> out;
        for (auto* n = this; n->m_parent; n = n->m_parent)
            out.push_back(n->index());
        std::ranges::reverse(out);
        return out;
    }

    // 按行路径从当前节点出发定位后代；路径非法时返回 nullptr(路径状态校验)。
    TreeNode* pathNode(std::span<const std::size_t> path) const noexcept
    {
        const TreeNode* n = this;
        for (std::size_t idx : path) {
            n = n->childAt(idx);
            if (!n)
                return nullptr;
        }
        return const_cast<TreeNode*>(n);
    }

    bool isValidPath(std::span<const std::size_t> path) const noexcept
    {
        return pathNode(path) != nullptr;
    }

    bool isDescendantOf(const TreeNode* item) const noexcept
    {
        for (auto* p = m_parent; p; p = p->m_parent)
            if (p == item)
                return true;
        return false;
    }

    bool isAncestorOf(const TreeNode* item) const noexcept
    {
        return item != nullptr && item->isDescendantOf(this);
    }

    // ---------------------------------------------------------------
    //  子节点视图 —— O(1) 构造，惰性双向 range
    // ---------------------------------------------------------------
    ChildRange<T> children() const noexcept { return ChildRange<T>(const_cast<TreeNode*>(this)); }

    // 子树遍历(先序/后序/层序)；基于协程惰性产出，可直接接 views::filter。
    Generator<TreeNode*> descendants(TraversalOrder order = TraversalOrder::PreOrder) const
    {
        auto* self = const_cast<TreeNode*>(this);
        switch (order) {
        case TraversalOrder::PreOrder: {
            co_yield self;
            for (auto* c = self->firstChild(); c; c = c->nextSibling())
                for (auto* n : c->descendants(TraversalOrder::PreOrder))
                    co_yield n;
            break;
        }
        case TraversalOrder::PostOrder: {
            for (auto* c = self->firstChild(); c; c = c->nextSibling())
                for (auto* n : c->descendants(TraversalOrder::PostOrder))
                    co_yield n;
            co_yield self;
            break;
        }
        case TraversalOrder::LevelOrder: {
            std::vector<TreeNode*> queue{self};
            for (std::size_t i = 0; i < queue.size(); ++i) {
                TreeNode* n = queue[i];
                co_yield n;
                for (auto* c = n->firstChild(); c; c = c->nextSibling())
                    queue.push_back(c);
            }
            break;
        }
        default: break;
        }
    }

    // 多属性过滤视图：predicate 作用于 T&，对整棵子树(先序)做筛选。
    // 底层仍然是对十字链表的一次惰性先序遍历 + std::views::filter 管道，
    // 不做任何提前物化(eager materialization)。
    template<typename Pred>
    auto filteredDescendants(Pred pred) const
    {
        return descendants(TraversalOrder::PreOrder)
               | std::views::filter(
                   [pred = std::move(pred)](TreeNode* n) { return pred(n->data()); });
    }

    // 同时满足多个谓词(逻辑与)的组合过滤 —— "多属性过滤视图"。
    template<typename... Preds>
    auto filteredAll(Preds... preds) const
    {
        return filteredDescendants([preds...](const T& v) { return (preds(v) && ...); });
    }

    // ---------------------------------------------------------------
    //  追加 / 插入
    // ---------------------------------------------------------------

    // 尾部追加子节点；O(1)，得益于 m_lastChild 缓存指针。
    TreeNode* appendChild(T value)
    {
        return appendOwnedChild(std::make_unique<TreeNode>(std::move(value)));
    }

    // 按下标插入；O(index)(需要先定位插入点)，插入动作本身是 O(1)。
    TreeNode* insertChildAt(std::size_t index, T value)
    {
        if (TreeNode* ref = childAt(index))
            return insertChildBefore(ref, std::move(value));
        return appendChild(std::move(value));
    }

    // 在给定兄弟节点前插入；给定节点句柄时严格 O(1)。
    TreeNode* insertChildBefore(TreeNode* refChild, T value)
    {
        assert(refChild && refChild->m_parent == this && "refChild 必须是 this 的直接子节点");
        return attachBefore(refChild, std::make_unique<TreeNode>(std::move(value)));
    }

    // 在给定兄弟节点后插入；给定节点句柄时严格 O(1)。
    TreeNode* insertChildAfter(TreeNode* refChild, T value)
    {
        assert(refChild && refChild->m_parent == this && "refChild 必须是 this 的直接子节点");
        if (TreeNode* nxt = refChild->m_nextSibling.get())
            return attachBefore(nxt, std::make_unique<TreeNode>(std::move(value)));
        return appendOwnedChild(std::make_unique<TreeNode>(std::move(value)));
    }

    // ---------------------------------------------------------------
    //  摘除 / 删除
    // ---------------------------------------------------------------

    // 将 this(及其整棵子树)从树中摘除，所有权转移给调用者；O(1)。
    // 根节点不允许通过此接口摘除，请使用 Tree::releaseRoot(){ return std::move(m_root); }。
    [[nodiscard]] std::unique_ptr<TreeNode> extract()
    {
        if (!m_parent)
            throw std::logic_error("TreeNode::extract(): 不能摘除根节点");

        TreeNode* prev = m_prevSibling;
        TreeNode* next = m_nextSibling.get();

        std::unique_ptr<TreeNode>& ownerSlot = prev ? prev->m_nextSibling : m_parent->m_firstChild;
        std::unique_ptr<TreeNode> self       = std::move(ownerSlot); // self 现在拥有 *this

        ownerSlot = std::move(self->m_nextSibling); // ownerSlot 转而拥有原来的 next
        if (next)
            next->m_prevSibling = prev;
        if (m_parent->m_lastChild == this)
            m_parent->m_lastChild = prev;

        --m_parent->m_childCount;
        self->m_parent      = nullptr;
        self->m_prevSibling = nullptr;
        return self; // self->m_nextSibling 已在上面被移空
    }

    // 摘除并销毁整棵子树；O(subtreeSize)(析构链式发生，非"摘除"本身开销)。
    void remove() { [[maybe_unused]] auto discarded = extract(); }

    // ---------------------------------------------------------------
    //  移动(重新挂接) —— 作为子节点移动 / 作为兄弟移动
    // ---------------------------------------------------------------

    // 将 this 移动为 newParent 的子节点；beforeChild 为 nullptr 时追加到末尾，
    // 否则插入到 beforeChild 之前。给定节点句柄时为 O(1) 结构调整。
    void moveAsChild(TreeNode* newParent, TreeNode* beforeChild = nullptr)
    {
        if (!newParent)
            throw std::invalid_argument("newParent 不能为空");
        if (newParent == this || newParent->isDescendantOf(this))
            throw std::logic_error("不能将节点移动到其自身子树内部(会形成环)");
        auto self = extract();
        if (beforeChild) {
            assert(beforeChild->m_parent == newParent);
            newParent->attachBefore(beforeChild, std::move(self));
        } else {
            newParent->appendOwnedChild(std::move(self));
        }
    }

    // 将 this 移动到 target 之前，成为 target 的兄弟(同一父节点下)。
    void moveBefore(TreeNode* target)
    {
        if (!target || target == this)
            return;
        if (target->isDescendantOf(this))
            throw std::logic_error("不能移动到自身子树内部");
        TreeNode* newParent = target->m_parent;
        if (!newParent)
            throw std::logic_error("target 是根节点，没有可插入的兄弟位置");
        auto self = extract();
        newParent->attachBefore(target, std::move(self));
    }

    // 将 this 移动到 target 之后，成为 target 的兄弟。
    void moveAfter(TreeNode* target)
    {
        if (!target || target == this)
            return;
        if (target->isDescendantOf(this))
            throw std::logic_error("不能移动到自身子树内部");
        TreeNode* newParent = target->m_parent;
        if (!newParent)
            throw std::logic_error("target 是根节点，没有可插入的兄弟位置");
        auto self = extract();
        if (TreeNode* nxt = target->m_nextSibling.get())
            newParent->attachBefore(nxt, std::move(self));
        else
            newParent->appendOwnedChild(std::move(self));
    }

private:
    // 在 refChild 之前插入一个已构造(或已摘除)的子树；O(1)。
    TreeNode* attachBefore(TreeNode* refChild, std::unique_ptr<TreeNode> child)
    {
        TreeNode* raw      = child.get();
        TreeNode* prev     = refChild->m_prevSibling;
        raw->m_parent      = this;
        raw->m_prevSibling = prev;

        std::unique_ptr<TreeNode>& refOwnerSlot = prev ? prev->m_nextSibling : m_firstChild;
        raw->m_nextSibling = std::move(refOwnerSlot); // raw 接管原本拥有 refChild 的那个 unique_ptr
        refOwnerSlot       = std::move(child);        // 该槽位转而拥有 raw
        refChild->m_prevSibling = raw;

        ++m_childCount;
        return raw;
    }

    // 尾部追加一个已构造(或已摘除)的子树；O(1)，依赖 m_lastChild 缓存。
    TreeNode* appendOwnedChild(std::unique_ptr<TreeNode> child)
    {
        TreeNode* raw      = child.get();
        raw->m_parent      = this;
        raw->m_prevSibling = m_lastChild;
        if (m_lastChild)
            m_lastChild->m_nextSibling = std::move(child);
        else
            m_firstChild = std::move(child);
        m_lastChild = raw;
        ++m_childCount;
        return raw;
    }

private:
    T m_data{};

    TreeNode* m_parent      = nullptr;       // 非拥有
    TreeNode* m_prevSibling = nullptr;       // 非拥有
    TreeNode* m_lastChild   = nullptr;       // 非拥有，O(1) 尾部追加缓存
    std::unique_ptr<TreeNode> m_firstChild;  // 拥有
    std::unique_ptr<TreeNode> m_nextSibling; // 拥有
    std::size_t m_childCount = 0;
};

// =============================================================================
//  Tree<T> —— 整棵树的所有者
//
//  持有唯一的根节点 unique_ptr；根节点销毁(或 Tree 自身析构/移动)时，
//  沿着十字链表的 unique_ptr 链条递归释放全部节点，保证不出现内存泄漏。
// =============================================================================
template<typename T>
class Tree
{
public:
    using Node = TreeNode<T>;

    explicit Tree(T rootValue)
        : m_root(std::unique_ptr<Node>(new Node(std::move(rootValue))))
    {
    }

    Tree(const Tree&)                = delete;
    Tree& operator=(const Tree&)     = delete;
    Tree(Tree&&) noexcept            = default;
    Tree& operator=(Tree&&) noexcept = default;
    ~Tree()                          = default; // 根节点的 unique_ptr 递归释放整棵树

    Node* root() noexcept { return m_root.get(); }
    const Node* root() const noexcept { return m_root.get(); }

    std::size_t size() const noexcept { return m_root ? m_root->subtreeSize() : 0; }
    bool empty() const noexcept { return m_root == nullptr; }

    // 按行路径(从根出发的下标序列)定位节点；非法路径返回 nullptr。
    Node* pathNode(std::span<const std::size_t> path) const noexcept
    {
        return m_root ? m_root->pathNode(path) : nullptr;
    }

    bool isValidPath(std::span<const std::size_t> path) const noexcept
    {
        return pathNode(path) != nullptr;
    }

    // 整棵树的遍历视图，直接转发到根节点。
    Generator<Node*> traverse(TraversalOrder order = TraversalOrder::PreOrder) const
    {
        return m_root->descendants(order);
    }

    template<typename Pred>
    auto filtered(Pred pred) const
    {
        return m_root->filteredDescendants(std::move(pred));
    }

    // 摘除整棵树的根节点，所有权转移给调用者；之后 Tree 变为空树。
    [[nodiscard]] std::unique_ptr<Node> releaseRoot() noexcept { return std::move(m_root); }

    // 将一棵已摘除的子树(或另一个 Tree 摘下的根)重新接管为本树的根。
    void resetRoot(std::unique_ptr<Node> newRoot) noexcept
    {
        m_root = std::move(newRoot);
        if (m_root) {
            m_root->parent_      = nullptr;
            m_root->prevSibling_ = nullptr;
        }
    }

private:
    std::unique_ptr<Node> m_root;
};

} // namespace bakuon::gui
