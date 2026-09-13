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

namespace bakuon::core::hierarchy {

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

/**
 * @brief Hierarchy 十字链表实现
 *
 * 在 EnTT 中用 Parent / FirstChild / LastChild / PrevSibling / NextSibling
 * 表达有序树。组件固定大小、无堆分配，适配稀疏集布局与缓存友好访问。
 *
 * 劣势：按索引随机访问第 N 个子节点是 O(N)；GUI 场景通常顺序遍历，可接受。
 * 所有改树操作必须走本命名空间的函数，禁止直接 emplace/remove Hierarchy。
 */

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
 * @brief 解链: 从父节点摘掉后销毁自己；直接子节点变成独立根（保留各自子树）。
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
void collect(const Registry& registry, Handle node, std::vector<Handle>& out, bool with_self = true);

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
            current                    = childHier ? childHier->next_sibling : Handle{};
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

} // namespace bakuon::core::hierarchy
