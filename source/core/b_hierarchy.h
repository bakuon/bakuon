#pragma once

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "core/b_entity.h"

namespace bakuon::core::hierarchy {

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
    Entity parent{nullentity};       ///< 父节点；根为无效 Entity
    Entity first_child{nullentity};  ///< 第一个子节点
    Entity last_child{nullentity};   ///< 最后一个子节点
    Entity prev_sibling{nullentity}; ///< 前一个兄弟
    Entity next_sibling{nullentity}; ///< 后一个兄弟
    std::size_t child_count{0};      ///< 直接子节点数
    std::size_t depth{0};            ///< 深度；根为 0
    std::size_t index{0};            ///< 在父节点子列表中的下标
};

// ==============================================
// 子节点迭代器（C++20 range-based for）
// ==============================================
struct ChildIterator
{
    using iterator_category = std::input_iterator_tag;
    using value_type        = Entity;
    using difference_type   = std::ptrdiff_t;
    using pointer           = const Entity*;
    using reference         = const Entity&;

    Registry* reg{nullptr};
    Entity current{nullentity};

    constexpr ChildIterator() = default;
    constexpr ChildIterator(Registry& r, Entity e) noexcept
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
    Entity parent;

    [[nodiscard]] ChildIterator begin() const
    {
        if (!reg.valid(parent) || !reg.all_of<Hierarchy>(parent)) {
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

[[nodiscard]] Entity parent(const Registry& registry, Entity child);
[[nodiscard]] Entity child(const Registry& registry, std::size_t index, Entity parent);
[[nodiscard]] bool hasParent(const Registry& registry, Entity node);
[[nodiscard]] bool hasChildren(const Registry& registry, Entity parent);
[[nodiscard]] std::size_t childCount(const Registry& registry, Entity parent);
[[nodiscard]] ChildRange children(const Registry& registry, Entity parent);
[[nodiscard]] bool isDescendant(const Registry& registry, Entity node, Entity ancestor);
[[nodiscard]] std::optional<std::size_t> index(const Registry& registry, Entity node);
[[nodiscard]] std::size_t depth(const Registry& registry, Entity node);
[[nodiscard]] std::size_t size(const Registry& registry, Entity node);

[[nodiscard]] std::vector<std::size_t> path(const Registry& registry, Entity node);
[[nodiscard]] Entity pathNode(const Registry& registry, Entity root,
                              std::span<const std::size_t> path);
[[nodiscard]] std::string pathString(std::span<const std::size_t> path);

/**
 * @brief 收集当前所有根节点（带 Hierarchy 且 parent 无效，或未参与 Hierarchy 的实体不强制纳入）。
 * 典型用于大纲顶层：只列出真正挂过 Hierarchy 且无父的节点。
 */
void roots(const Registry& registry, std::vector<Entity>& out);

template<typename Func>
void eachRoot(const Registry& registry, Func&& func)
{
    std::vector<Entity> list;
    roots(registry, list);
    for (Entity h : list) {
        func(h);
    }
}

// ==============================================
// 修改（attach / detach 为链接唯一入口）
// ==============================================

/**
 * @brief 追加到 parent 子列表末尾。移动语义：若 child 已有父节点会先 detach。
 * @return false 若节点无效或会成环
 */
bool append(Registry& registry, Entity child, Entity parent);

/**
 * @brief 插入到 target 之前。target 必须已有父节点。
 */
bool insertBefore(Registry& registry, Entity child, Entity target);

/**
 * @brief 插入到 target 之后。target 必须已有父节点。
 */
bool insertAfter(Registry& registry, Entity child, Entity target);

/**
 * @brief 把 child 挂到 parent 下。
 * @param before 无效时追加到末尾；否则插入到该兄弟之前（before 必须是 parent 的直接子节点）。
 * @return false 若节点无效、成环、或 before 不是 parent 的直接子节点
 */
bool attach(Registry& registry, Entity child, Entity parent, Entity before = nullentity);

/**
 * @brief 从父节点脱离，成为独立根；子树 depth 相对归零。不销毁实体。
 */
void detach(Registry& registry, Entity node);

/**
 * @brief 解链: 从父节点摘掉后销毁自己；直接子节点变成独立根（保留各自子树）。
 */
void extract(Registry& registry, Entity node);

/**
 * @brief 销毁 node 及其完整子树（先收集再倒序 destroy，避免遍历中组件失效）。
 */
void destroy(Registry& registry, Entity node);

/**
 * @brief 同一父节点内重排到 newIndex（0-based）。
 * @return false 若节点无效、无父、或 newIndex 越界
 */
bool reorder(Registry& registry, Entity child, std::size_t newIndex);

/** 与前一个兄弟交换位置；已是第一个则空操作返回 false。 */
bool moveUp(Registry& registry, Entity child);

/** 与后一个兄弟交换位置；已是最后一个则空操作返回 false。 */
bool moveDown(Registry& registry, Entity child);

/**
 * @brief 前序收集子树节点。
 * @param with_self 是否包含 node 自身
 */
void collect(const Registry& registry, Entity node, std::vector<Entity>& out, bool with_self = true);

/**
 * @brief 遍历直接子节点。回调前先快照，回调内改树安全。
 */
template<typename Func>
void eachChild(const Registry& registry, Entity parent, Func&& func)
{
    std::vector<Entity> snapshot;
    if (const Hierarchy* hier = registry.try_get<Hierarchy>(parent)) {
        for (Entity current = hier->first_child; registry.valid(current);) {
            snapshot.push_back(current);
            const Hierarchy* childHier = registry.try_get<Hierarchy>(current);
            current                    = childHier ? childHier->next_sibling : nullentity;
        }
    }
    for (Entity child : snapshot) {
        func(child);
    }
}

/**
 * @brief 遍历全部后代（不含 parent 自身）。
 * PostOrder 为先序反转的近似后序，保证「子孙先于祖先」，满足 destroy 需求。
 */
template<typename Func>
void eachDescendant(const Registry& registry, Entity parent, Func&& func,
                    TraversalOrder order = TraversalOrder::PreOrder)
{
    std::vector<Entity> descendants;
    collect(registry, parent, descendants, /*with_self=*/false);

    if (order == TraversalOrder::PreOrder) {
        for (Entity h : descendants) {
            func(h);
        }
    } else {
        for (auto it = descendants.rbegin(); it != descendants.rend(); ++it) {
            func(*it);
        }
    }
}

} // namespace bakuon::core::hierarchy
