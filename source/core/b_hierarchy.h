#pragma once

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <limits>
#include <optional>
#include <span>
#include <stack>
#include <string>
#include <vector>

#include "core/b_handle.h"
#include "core/b_registry.h"

namespace bakuon::core {

/**
 * @brief 父节点。缺席 = 根级实体。
 *
 * @warning 不要直接 emplace/remove 这个组件来改树结构——那样 Children 另一侧
 *          会立刻失去同步。一律走 hierarchy::setParent() / detach()
 *          （见 b_hierarchy.h）。
 */
struct Parent
{
    Handle handle{};
};

/**
 * @brief 有序子节点列表。view/each 本身无序，同级顺序只存在这份向量里。
 *
 * 空容器仍然可以挂在"文件夹"实体上（表示这是一个容器，只是还没有孩子）。
 * 维护规则与 Parent 相同：只通过 hierarchy:: 下的函数改。
 */
struct Children
{
    std::vector<Handle> entities;
};

struct HierarchyNode
{
    Handle parent;
    Handle firstChild;
    Handle lastChild;
    Handle prevSibling;
    Handle nextSibling;
    std::size_t childCount{0};
    std::size_t index{0};
    std::size_t depth{0};
};

/// setParent() 的 index 哨兵：接到当前子列表末尾。
inline constexpr std::size_t kAppend = std::numeric_limits<std::size_t>::max();

/**
 * @brief 父子层级 —— 补上 EnTT 不提供的"有序树"。
 * @todo 除非不使用下面的 "十字链表" 的实现才将此实现 "类" 拆开到命名空间，或者直接删除该实现方案。
 *
 * Registry 的 view/each 没有稳定顺序，也没有父子。GUI 里的图层栈、大纲树、
 * 嵌套面板必须自己维护一份顺序。用 Parent + Children 两个组件互为
 * 镜像，所有改树操作都走这里的函数，保证：
 *  - A 的 Parent 是 B  ⟺  A 出现在 B 的 Children.entities 里，且只出现一次；
 *  - 同级顺序 = Children.entities 的下标；
 *  - 不允许成环、不允许挂到无效实体上。
 *
 * ## 为什么不做成 Registry 的自动钩子
 * 在 onDestroy 里改另一侧组件很容易和"正在销毁的实体"重入。第一期选择
 * **显式 API**：调用方改树、销毁带孩子的节点，都走本命名空间。
 * `Registry::destroy()` / 直接 emplace<Parent>() 会打破双向不变量——
 * 这和 CommandLayout 被 CommandModel 包裹之后不许绕开 Model 直接改树
 * 是同一类约束（见 source/gui/b_commandlayout.h 头部）。
 *
 * 销毁：
 *  - destroy()         先从父节点摘掉、再让直系孩子变孤儿，然后销毁自己；
 *  - destroySubtree()  先递归销毁全部子孙，再销毁自己。
 *
 * 还没有孩子的容器：setParent 不会给叶子自动挂 Children；只有真正被
 * 当作父节点用过的实体才会有 Children 组件（可以是空向量）。
 */
class Hierarchy
{
public:
    Hierarchy(Registry& registry) noexcept;
    ~Hierarchy();

    /*
    // 需要使用 "十字链表"：在频繁中间插入/删除、极深树、子节点极多时才真正占优
    [[nodiscard]] std::size_t size(Handle node) const; // 包括子树
    [[nodiscard]] std::size_t depth(Handle node) const;
    [[nodiscard]] std::vector<std::size_t> pathOf(Handle node) const;
    [[nodiscard]] Handle entityOf(std::span<const std::size_t> path) const;
    */

    [[nodiscard]] bool isValid(Handle node) const;  // 不判定是否孤儿节点(orphan)
    [[nodiscard]] bool isOrphan(Handle node) const; // 是否为孤儿节点(sa: isAncestor)

    [[nodiscard]] std::optional<std::size_t> index(Handle node) const;
    [[nodiscard]] Handle parent(Handle child) const;
    [[nodiscard]] Handle child(std::size_t index, Handle parent) const;
    [[nodiscard]] bool hasChildren(Handle parent) const;
    [[nodiscard]] std::size_t childCount(Handle parent) const;
    [[nodiscard]] const std::vector<Handle>& children(Handle parent) const;
    [[nodiscard]] bool isAncestor(Handle node, Handle ancestor) const; // is descendant

    void append(Handle child, Handle parent);
    void insert(Handle child, std::size_t index, Handle parent);
    bool remove(std::size_t index, Handle parent, std::size_t count = 1);

    bool attach(Handle child, std::size_t index, Handle parent); /// setParent like
    void detach(Handle child);

    /// 摘除：从父节点摘掉，直系孩子变成根级孤儿，
    /// 然后销毁自己，子孙实体仍然存活，只是失去了这一层父亲。
    void extract(Handle node);

    /// 递归销毁 node 及其全部子孙(包括组件)。
    void destroy(Handle node);

private:
    bool reorder(Handle child, std::size_t index, Handle parent);

private:
    Handle m_root;
    Registry& m_registry;
};

/**
 * @brief Hierarchy 十字链表实现
 *
 * 在 EnTT 中用 Parent / FirstChild / LastChild / PrevSibling / NextSibling
 * 表达有序树。组件固定大小、无堆分配，适配稀疏集布局与缓存友好访问。
 *
 * 劣势：按索引随机访问第 N 个子节点是 O(N)；GUI 场景通常顺序遍历，可接受。
 * 所有改树操作必须走本命名空间的函数，禁止直接 emplace/remove Hierarchy。
 */
namespace hierarchy {

// ==============================================
// 核心组件（十字链表，固定大小）
// ==============================================
/**
 * @brief 层级关系组件
 * @details 全部字段内联，无堆分配；child_count / depth / index 为 O(1) 缓存。
 */
struct Hierarchy
{
    Handle parent;              ///< 父节点；根为无效 Handle
    Handle first_child;         ///< 第一个子节点
    Handle last_child;          ///< 最后一个子节点
    Handle prev_sibling;        ///< 前一个兄弟
    Handle next_sibling;        ///< 后一个兄弟
    std::size_t child_count{0}; ///< 直接子节点数
    std::size_t depth{0};       ///< 深度；根为 0
    std::size_t index{0};       ///< 在父节点子列表中的下标

    [[nodiscard]] constexpr bool isRoot() const noexcept { return !parent.isValid(); }
    [[nodiscard]] constexpr bool isLeaf() const noexcept { return !first_child.isValid(); }
    [[nodiscard]] constexpr bool hasParent() const noexcept { return parent.isValid(); }
    [[nodiscard]] constexpr bool hasChildren() const noexcept { return first_child.isValid(); }
};

// ==============================================
// 子节点迭代器（C++20 range-based for）
// ==============================================
struct ChildIterator
{
    using iterator_category = std::input_iterator_tag;
    using value_type        = Handle;
    using difference_type   = std::ptrdiff_t;
    using pointer           = const Handle*;
    using reference         = const Handle&;

    Registry* reg{nullptr};
    Handle current{};

    constexpr ChildIterator() = default;
    constexpr ChildIterator(Registry& r, Handle e) noexcept
        : reg(&r)
        , current(e)
    {
    }

    [[nodiscard]] constexpr reference operator*() const noexcept { return current; }
    [[nodiscard]] constexpr pointer operator->() const noexcept { return &current; }

    ChildIterator& operator++()
    {
        assert(reg && reg->valid(current) && "Dereferencing invalid ChildIterator");
        current = reg->get<Hierarchy>(current).next_sibling;
        return *this;
    }

    ChildIterator operator++(int)
    {
        ChildIterator tmp = *this;
        ++(*this);
        return tmp;
    }

    [[nodiscard]] constexpr bool operator==(const ChildIterator& other) const noexcept
    {
        return current == other.current;
    }
    [[nodiscard]] constexpr bool operator!=(const ChildIterator& other) const noexcept
    {
        return !(*this == other);
    }
};

struct ChildRange
{
    Registry& reg;
    Handle parent;

    [[nodiscard]] ChildIterator begin() const
    {
        if (!reg.valid(parent) || !reg.has<Hierarchy>(parent)) {
            return {};
        }
        return {reg, reg.get<Hierarchy>(parent).first_child};
    }

    [[nodiscard]] constexpr ChildIterator end() const noexcept { return {}; }
};

enum class TraversalOrder {
    PreOrder,  ///< 父先于子孙
    PostOrder, ///< 近似后序：任意节点一定排在其全部子孙之后（兄弟相对顺序不保证）
};

// ==============================================
// 查询
// ==============================================

[[nodiscard]] Handle parent(const Registry& registry, Handle child);
[[nodiscard]] Handle child(const Registry& registry, std::size_t index, Handle parent);
[[nodiscard]] bool hasParent(const Registry& registry, Handle node);
[[nodiscard]] bool hasChildren(const Registry& registry, Handle parent);
[[nodiscard]] std::size_t childCount(const Registry& registry, Handle parent);
[[nodiscard]] ChildRange children(const Registry& registry, Handle parent);
[[nodiscard]] bool isDescendant(const Registry& registry, Handle node, Handle ancestor);
[[nodiscard]] std::optional<std::size_t> index(const Registry& registry, Handle node);
[[nodiscard]] std::size_t depth(const Registry& registry, Handle node);
[[nodiscard]] std::size_t size(const Registry& registry, Handle node);

[[nodiscard]] std::vector<std::size_t> path(const Registry& registry, Handle node);
[[nodiscard]] Handle pathNode(const Registry& registry, Handle root,
                              std::span<const std::size_t> path);
[[nodiscard]] std::string pathString(std::span<const std::size_t> path);

// ==============================================
// 修改（attach / detach 为链接唯一入口）
// ==============================================

/**
 * @brief 追加到 parent 子列表末尾。移动语义：若 child 已有父节点会先 detach。
 * @return false 若节点无效或会成环
 */
bool append(Registry& registry, Handle child, Handle parent);

/**
 * @brief 插入到 target 之前。target 必须已有父节点。
 */
bool insertBefore(Registry& registry, Handle child, Handle target);

/**
 * @brief 插入到 target 之后。target 必须已有父节点。
 */
bool insertAfter(Registry& registry, Handle child, Handle target);

/**
 * @brief 把 child 挂到 parent 下。
 * @param before 无效时追加到末尾；否则插入到该兄弟之前（before 必须是 parent 的直接子节点）。
 * @return false 若节点无效、成环、或 before 不是 parent 的直接子节点
 */
bool attach(Registry& registry, Handle child, Handle parent, Handle before = {});

/**
 * @brief 从父节点脱离，成为独立根；子树 depth 相对归零。不销毁实体。
 */
void detach(Registry& registry, Handle node);

/**
 * @brief 从父节点摘掉后销毁自己；直接子节点变成独立根（保留各自子树）。
 */
void extract(Registry& registry, Handle node);

/**
 * @brief 销毁 node 及其完整子树（先收集再倒序 destroy，避免遍历中组件失效）。
 */
void destroy(Registry& registry, Handle node);

/**
 * @brief 前序收集子树节点。
 * @param with_self 是否包含 node 自身
 */
void collect(const Registry& registry, Handle node, std::vector<Handle>& out,
             bool with_self = true);

/**
 * @brief 遍历直接子节点。回调前先快照，回调内改树安全。
 */
template<typename Func>
void eachChild(const Registry& registry, Handle parent, Func&& func)
{
    std::vector<Handle> snapshot;
    if (const Hierarchy* hier = registry.tryGet<Hierarchy>(parent)) {
        for (Handle current = hier->first_child; current.isValid();) {
            snapshot.push_back(current);
            const Hierarchy* childHier = registry.tryGet<Hierarchy>(current);
            current = childHier ? childHier->next_sibling : Handle{};
        }
    }
    for (Handle child : snapshot) {
        func(child);
    }
}

/**
 * @brief 遍历全部后代（不含 parent 自身）。
 * PostOrder 为先序反转的近似后序，保证「子孙先于祖先」，满足 destroy 需求。
 */
template<typename Func>
void eachDescendant(const Registry& registry, Handle parent, Func&& func,
                    TraversalOrder order = TraversalOrder::PreOrder)
{
    std::vector<Handle> descendants;
    collect(registry, parent, descendants, /*with_self=*/false);

    if (order == TraversalOrder::PreOrder) {
        for (Handle h : descendants) {
            func(h);
        }
    } else {
        for (auto it = descendants.rbegin(); it != descendants.rend(); ++it) {
            func(*it);
        }
    }
}

} // namespace hierarchy

} // namespace bakuon::core
