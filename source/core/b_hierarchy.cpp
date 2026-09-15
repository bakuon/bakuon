#include "core/b_hierarchy.h"

#include <cstdint>
#include <limits>

namespace bakuon::core::hierarchy {

// ==============================================
// 内部实现细节
// ==============================================
namespace detail {

/// size_t += int，带下溢/上溢保护。下溢时钳制为 0。
inline std::size_t safe_add(std::size_t a, int b) noexcept
{
    if (b < 0) {
        const auto abs_b = static_cast<std::size_t>(-static_cast<long long>(b));
        return a < abs_b ? 0 : a - abs_b;
    }
    const auto bb = static_cast<std::size_t>(b);
    return a > (std::numeric_limits<std::size_t>::max() - bb) ? a : a + bb;
}

/**
 * @brief 迭代式更新子树（含 root 自身）所有节点的 depth 缓存。
 * @param delta 深度偏移量（正增负减）
 * @note DFS 前序，无递归栈溢出风险。调用方不要再单独给 root 写 depth。
 */
inline void update_subtree_depth(Registry& registry, Handle root, int delta)
{
    if (!registry.valid(root) || delta == 0) {
        return;
    }

    std::stack<Handle> stack;
    stack.push(root);

    while (!stack.empty()) {
        const Handle node = stack.top();
        stack.pop();

        auto& hier = registry.get<Hierarchy>(node);
        hier.depth = safe_add(hier.depth, delta);

        // 逆序压栈，使出栈顺序与兄弟逻辑顺序一致（可选，仅影响遍历顺序）
        for (Handle it = hier.last_child; it.isValid();) {
            stack.push(it);
            it = registry.get<Hierarchy>(it).prev_sibling;
        }
    }
}

/**
 * @brief 从 start 起（含）到同级末尾，所有兄弟的 index += delta。
 */
inline void update_sibling_indices_from(Registry& registry, Handle start, int delta)
{
    if (delta == 0) {
        return;
    }
    for (Handle current = start; current.isValid();) {
        auto& hier = registry.get<Hierarchy>(current);
        hier.index = safe_add(hier.index, delta);
        current    = hier.next_sibling;
    }
}

/// 确保节点带有 Hierarchy 组件；若是新建则已是根状态（全 null / 0）。
inline Hierarchy& ensure(Registry& registry, Handle node)
{
    return registry.getOrEmplace<Hierarchy>(node);
}

} // namespace detail

// ==============================================
// 查询
// ==============================================

Handle parent(const Registry& registry, Handle child)
{
    const Hierarchy* hy = registry.tryGet<Hierarchy>(child);
    return hy ? hy->parent : Handle{};
}

Handle child(const Registry& registry, std::size_t index, Handle parent)
{
    const Hierarchy* hy = registry.tryGet<Hierarchy>(parent);
    if (!hy || index >= hy->child_count) {
        return {};
    }

    Handle current = hy->first_child;
    for (std::size_t i = 0; i < index; ++i) {
        current = registry.get<Hierarchy>(current).next_sibling;
    }
    return current;
}

bool hasParent(const Registry& registry, Handle node)
{
    return parent(registry, node).isValid();
}

bool hasChildren(const Registry& registry, Handle parent)
{
    return childCount(registry, parent) > 0;
}

std::size_t childCount(const Registry& registry, Handle parent)
{
    const Hierarchy* hy = registry.tryGet<Hierarchy>(parent);
    return hy ? hy->child_count : 0;
}

ChildRange children(const Registry& registry, Handle parent)
{
    // ChildIterator 只读访问组件，const_cast 仅用于适配非 const 引用成员。
    return {const_cast<Registry&>(registry), parent};
}

std::optional<std::size_t> index(const Registry& registry, Handle node)
{
    if (!registry.valid(node)) {
        return std::nullopt;
    }
    const Hierarchy* hy = registry.tryGet<Hierarchy>(node);
    return hy ? std::optional<std::size_t>{hy->index} : std::nullopt;
}

bool isDescendant(const Registry& registry, Handle node, Handle ancestor)
{
    if (!ancestor.isValid() || node == ancestor) {
        return false;
    }
    for (Handle p = parent(registry, node); p.isValid(); p = parent(registry, p)) {
        if (p == ancestor) {
            return true;
        }
    }
    return false;
}

std::size_t depth(const Registry& registry, Handle node)
{
    const Hierarchy* hy = registry.tryGet<Hierarchy>(node);
    return hy ? hy->depth : 0;
}

std::size_t size(const Registry& registry, Handle node)
{
    std::vector<Handle> out;
    collect(registry, node, out, /*with_self=*/true);
    return out.size();
}

std::vector<std::size_t> path(const Registry& registry, Handle node)
{
    std::vector<std::size_t> out;
    if (!registry.valid(node) || !registry.has<Hierarchy>(node)) {
        return out;
    }

    for (Handle current = node; hasParent(registry, current); current = parent(registry, current)) {
        out.push_back(registry.get<Hierarchy>(current).index);
    }
    std::ranges::reverse(out);
    return out;
}

Handle pathNode(const Registry& registry, Handle root, std::span<const std::size_t> path)
{
    if (!registry.valid(root) || !registry.has<Hierarchy>(root)) {
        return {};
    }

    Handle current = root;
    for (const std::size_t idx : path) {
        const Hierarchy& hier = registry.get<Hierarchy>(current);
        if (idx >= hier.child_count) {
            return {};
        }
        current = hier.first_child;
        for (std::size_t i = 0; i < idx; ++i) {
            current = registry.get<Hierarchy>(current).next_sibling;
        }
    }
    return current;
}

std::string pathString(std::span<const std::size_t> path)
{
    std::string s;
    for (std::size_t i = 0; i < path.size(); ++i) {
        if (i > 0) {
            s += '/';
        }
        s += std::to_string(path[i]);
    }
    return s;
}

void roots(const Registry& registry, std::vector<Handle>& out)
{
    out.clear();
    registry.each<Hierarchy>([&](Handle handle, const Hierarchy& hier) {
        if (!hier.parent.isValid() && handle.isValid()) {
            out.push_back(handle);
        }
    });
}

// ==============================================
// 修改：以 attach / detach 为唯一链接入口
// ==============================================

void detach(Registry& registry, Handle node)
{
    if (!registry.valid(node)) {
        return;
    }
    Hierarchy* nodeHier = registry.tryGet<Hierarchy>(node);
    if (!nodeHier || !nodeHier->parent.isValid()) {
        return; // 本就是根 / 未参与层级
    }

    Hierarchy& parentHier = registry.get<Hierarchy>(nodeHier->parent);

    // 断前驱
    if (nodeHier->prev_sibling.isValid()) {
        registry.get<Hierarchy>(nodeHier->prev_sibling).next_sibling = nodeHier->next_sibling;
    } else {
        parentHier.first_child = nodeHier->next_sibling;
    }

    // 断后继，并让后续兄弟 index - 1
    if (nodeHier->next_sibling.isValid()) {
        registry.get<Hierarchy>(nodeHier->next_sibling).prev_sibling = nodeHier->prev_sibling;
        detail::update_sibling_indices_from(registry, nodeHier->next_sibling, -1);
    } else {
        parentHier.last_child = nodeHier->prev_sibling;
    }

    if (parentHier.child_count > 0) {
        --parentHier.child_count;
    }

    // 成为独立根：清空链接字段，depth 整棵子树相对归零
    const int depthDelta   = -static_cast<int>(nodeHier->depth);
    nodeHier->parent       = Handle{};
    nodeHier->prev_sibling = Handle{};
    nodeHier->next_sibling = Handle{};
    nodeHier->index        = 0;

    if (depthDelta != 0) {
        detail::update_subtree_depth(registry, node, depthDelta);
    }
}

bool attach(Registry& registry, Handle child, Handle parent, Handle before)
{
    if (!registry.valid(parent) || !registry.valid(child) || parent == child) {
        return false;
    }
    // 禁止把祖先挂到自己的后代下
    if (isDescendant(registry, parent, child)) {
        return false;
    }

    Hierarchy* beforeHier = nullptr;
    if (before.isValid()) {
        if (!registry.valid(before)) {
            return false;
        }
        beforeHier = registry.tryGet<Hierarchy>(before);
        if (!beforeHier || beforeHier->parent != parent) {
            return false; // before 必须是 parent 的直接子节点
        }
    }

    // 移动语义：无论原先在哪（含同一 parent），先完整脱离
    detach(registry, child);

    Hierarchy& childHier  = detail::ensure(registry, child);
    Hierarchy& parentHier = detail::ensure(registry, parent);

    // —— 链接 ——
    if (before.isValid()) {
        // detach 可能改过 before 的 index，重新取一次
        beforeHier               = registry.tryGet<Hierarchy>(before);
        const Handle prev        = beforeHier->prev_sibling;
        childHier.prev_sibling   = prev;
        childHier.next_sibling   = before;
        beforeHier->prev_sibling = child;
        if (prev.isValid()) {
            registry.get<Hierarchy>(prev).next_sibling = child;
        } else {
            parentHier.first_child = child;
        }
        // child 占据 before 的旧 index，before 及之后全部 +1
        childHier.index = beforeHier->index;
        detail::update_sibling_indices_from(registry, before, 1);
    } else {
        // 追加到末尾
        childHier.prev_sibling = parentHier.last_child;
        childHier.next_sibling = Handle{};
        if (parentHier.last_child.isValid()) {
            Hierarchy& lastHier   = registry.get<Hierarchy>(parentHier.last_child);
            lastHier.next_sibling = child;
            childHier.index       = lastHier.index + 1;
        } else {
            parentHier.first_child = child;
            childHier.index        = 0;
        }
        parentHier.last_child = child;
    }

    childHier.parent = parent;
    ++parentHier.child_count;

    // —— depth：只通过 helper 更新整棵子树（含 child 自身）——
    const int depthDelta = static_cast<int>(parentHier.depth + 1)
                           - static_cast<int>(childHier.depth);
    if (depthDelta != 0) {
        detail::update_subtree_depth(registry, child, depthDelta);
    }

    return true;
}

bool append(Registry& registry, Handle child, Handle parent)
{
    return attach(registry, child, parent, /*before=*/Handle{});
}

bool insertBefore(Registry& registry, Handle child, Handle target)
{
    if (!registry.valid(target)) {
        return false;
    }
    const Hierarchy* targetHier = registry.tryGet<Hierarchy>(target);
    if (!targetHier || !targetHier->parent.isValid()) {
        return false;
    }
    return attach(registry, child, targetHier->parent, target);
}

bool insertAfter(Registry& registry, Handle child, Handle target)
{
    if (!registry.valid(target)) {
        return false;
    }
    const Hierarchy* targetHier = registry.tryGet<Hierarchy>(target);
    if (!targetHier || !targetHier->parent.isValid()) {
        return false;
    }
    // 插到 target 之后 ≡ 以 target 的 next 为 before（若无 next 则 append）
    return attach(registry, child, targetHier->parent, targetHier->next_sibling);
}

void extract(Registry& registry, Handle node)
{
    if (!registry.valid(node)) {
        return;
    }

    // 先从父链摘掉，避免父节点留下悬空兄弟指针
    detach(registry, node);

    // 快照直接子节点，把它们变成独立根（保留各自子树）
    std::vector<Handle> kids;
    eachChild(registry, node, [&](Handle h) { kids.push_back(h); });

    for (Handle c : kids) {
        if (!registry.valid(c)) {
            continue;
        }
        Hierarchy* h = registry.tryGet<Hierarchy>(c);
        if (!h) {
            continue;
        }
        // 断开与原父/兄弟的链接，成为根
        const int depthDelta = -static_cast<int>(h->depth);
        h->parent            = Handle{};
        h->prev_sibling      = Handle{};
        h->next_sibling      = Handle{};
        h->index             = 0;
        if (depthDelta != 0) {
            detail::update_subtree_depth(registry, c, depthDelta);
        }
    }

    // 清空 node 自身的子链（即将销毁，防御性清理）
    if (Hierarchy* nodeHier = registry.tryGet<Hierarchy>(node)) {
        nodeHier->first_child = Handle{};
        nodeHier->last_child  = Handle{};
        nodeHier->child_count = 0;
    }

    registry.destroy(node);
}

void destroy(Registry& registry, Handle node)
{
    if (!registry.valid(node)) {
        return;
    }

    // 先摘除，避免父节点链表悬空
    detach(registry, node);

    // 完整收集子树后再倒序销毁：不能边遍历边 destroy，
    // 否则 Hierarchy 组件被摘掉后兄弟链会读到失效数据。
    std::vector<Handle> toDestroy;
    collect(registry, node, toDestroy, /*with_self=*/true);

    for (auto it = toDestroy.rbegin(); it != toDestroy.rend(); ++it) {
        registry.destroy(*it);
    }
}

bool reorder(Registry& registry, Handle child, std::size_t newIndex)
{
    if (!registry.valid(child)) {
        return false;
    }
    const Hierarchy* childHier = registry.tryGet<Hierarchy>(child);
    if (!childHier || !childHier->parent.isValid()) {
        return false;
    }
    const Handle parentHandle   = childHier->parent;
    const Hierarchy& parentHier = registry.get<Hierarchy>(parentHandle);
    if (newIndex >= parentHier.child_count) {
        return false;
    }
    if (childHier->index == newIndex) {
        return true; // 已在目标位置
    }

    // 目标位置当前的节点（reorder 前）；若 newIndex 指向自身之后的槽位，
    // 先 detach 再 attach 时索引会变化，因此用“目标 before”语义：
    // newIndex == child_count-1 且移动到末尾 → before 无效（append）。
    Handle before{};
    if (newIndex + 1 < parentHier.child_count) {
        // 想插到原 newIndex 位置 ≡ 以“当前占该位置的节点”为 before
        // 但若该节点就是 child 自身，需取其后继。
        Handle at = hierarchy::child(registry, newIndex, parentHandle);
        if (at == child) {
            at = registry.get<Hierarchy>(child).next_sibling;
        }
        // 若 child 当前 index < newIndex，detach 后后面节点前移，
        // 原 newIndex 位置的节点会变成 newIndex-1，before 应取“原 newIndex+1”。
        if (childHier->index < newIndex) {
            at = hierarchy::child(registry, newIndex + 1, parentHandle);
            // child 自己若在 newIndex+1 则再往后
            if (at == child) {
                at = registry.get<Hierarchy>(child).next_sibling;
            }
        }
        before = at;
    }
    // else: 移到末尾，before 保持无效

    return attach(registry, child, parentHandle, before);
}

bool moveUp(Registry& registry, Handle child)
{
    const auto idx = index(registry, child);
    if (!idx || *idx == 0) {
        return false;
    }
    return reorder(registry, child, *idx - 1);
}

bool moveDown(Registry& registry, Handle child)
{
    const auto idx = index(registry, child);
    if (!idx) {
        return false;
    }
    const Handle p = parent(registry, child);
    if (!p.isValid()) {
        return false;
    }
    if (*idx + 1 >= childCount(registry, p)) {
        return false;
    }
    return reorder(registry, child, *idx + 1);
}

void collect(const Registry& registry, Handle node, std::vector<Handle>& out, bool with_self)
{
    // 迭代前序：栈里逆序压入子节点，使出栈顺序与兄弟顺序一致
    std::vector<Handle> stack;
    if (with_self) {
        if (!registry.valid(node)) {
            return;
        }
        stack.push_back(node);
    } else {
        std::vector<Handle> rootChildren;
        eachChild(registry, node, [&](Handle h) { rootChildren.push_back(h); });
        for (auto it = rootChildren.rbegin(); it != rootChildren.rend(); ++it) {
            stack.push_back(*it);
        }
    }

    while (!stack.empty()) {
        const Handle current = stack.back();
        stack.pop_back();
        out.push_back(current);

        std::vector<Handle> kids;
        eachChild(registry, current, [&](Handle h) { kids.push_back(h); });
        for (auto it = kids.rbegin(); it != kids.rend(); ++it) {
            stack.push_back(*it);
        }
    }
}

} // namespace bakuon::core::hierarchy
