#include "core/b_hierarchy.h"

#include <cstdint>

namespace bakuon::core {

Hierarchy::Hierarchy(Registry& registry) noexcept
    : m_registry(registry)
{
    m_root = m_registry.create();
    m_registry.emplace<Parent>(m_root, Parent{});
    m_registry.emplace<Children>(m_root);
}

Hierarchy::~Hierarchy()
{
    // 摘除并整体销毁 root 及其全部子孙实体（连同它们各自携带的所有组件）
    destroy(m_root);
}

bool Hierarchy::isValid(Handle node) const
{
    return m_registry.valid(node) && m_registry.has<Parent>(node);
}

bool Hierarchy::isOrphan(Handle node) const
{
    return !isAncestor(node, m_root);
}

std::optional<std::size_t> Hierarchy::index(Handle node) const
{
    const auto* parent = m_registry.tryGet<Parent>(node);
    if (!parent) {
        return std::nullopt;
    }
    const auto* children = m_registry.tryGet<Children>(parent->handle);
    if (!children) {
        return std::nullopt;
    }

    auto& entities = children->entities;
    const auto it  = std::find(entities.begin(), entities.end(), node);
    if (it == entities.end()) {
        return std::nullopt;
    }

    return static_cast<std::size_t>(it - entities.begin());
}

Handle Hierarchy::parent(Handle child) const
{
    const auto* parent = m_registry.tryGet<Parent>(child);
    return parent ? parent->handle : m_root;
}

Handle Hierarchy::child(std::size_t index, Handle parent) const
{
    const auto* children = m_registry.tryGet<Children>(parent);
    return children ? children->entities.at(index) : Handle{};
}

const std::vector<Handle>& Hierarchy::children(Handle parent) const
{
    static const std::vector<Handle> empty;
    const auto* children = m_registry.tryGet<Children>(parent);
    return children ? children->entities : empty;
}

bool Hierarchy::hasChildren(Handle parent) const
{
    const Children* children = m_registry.tryGet<Children>(parent);
    return children && children->entities.size();
}

std::size_t Hierarchy::childCount(Handle parent) const
{
    const Children* children = m_registry.tryGet<Children>(parent);
    return children ? children->entities.size() : 0;
}

bool Hierarchy::isAncestor(Handle node, Handle ancestor) const
{
    if (!m_registry.valid(ancestor) || !m_registry.valid(node) || ancestor == node) {
        return false;
    }

    for (auto p = parent(node); p.isValid(); p = parent(p)) {
        if (p == ancestor) {
            return true;
        }
    }

    return false;
}

void Hierarchy::append(Handle child, Handle parent)
{
    insert(child, kAppend, parent);
}

void Hierarchy::insert(Handle child, std::size_t index, Handle parent)
{
    if (!m_registry.has<Children>(parent)) {
        m_registry.emplace<Children>(parent);
    }

    m_registry.patch<Children>(parent, [child, index](Children& children) {
        if (index >= children.entities.size()) {
            children.entities.push_back(child);
        } else {
            children.entities.insert(children.entities.begin() + static_cast<std::ptrdiff_t>(index),
                                     child);
        }
    });
}

bool Hierarchy::remove(std::size_t index, Handle parent, std::size_t count)
{
    if (count == 0) {
        return false;
    }

    const auto* kids = m_registry.tryGet<Children>(parent);
    if (kids == nullptr) {
        return false;
    }

    if (index >= kids->entities.size()) {
        return false;
    }

    // begin() + index 指向起始位置
    // begin() + index + count 指向结束位置（开区间，不包含该位置）
    // 期望删除 [index, index + count) 范围内的元素
    const std::size_t actual = std::min(index + count, kids->entities.size());

    m_registry.patch<Children>(parent, [this, index, actual](Children& children) {
        // 1. 先从层级结构中断绝关系
        for (std::size_t i = index; i < actual; ++i) {
            m_registry.remove<Parent>(children.entities[i]);
        }
        // 2. 再从层级结构中移除
        children.entities.erase(children.entities.begin() + static_cast<std::ptrdiff_t>(index),
                                children.entities.begin() + static_cast<std::ptrdiff_t>(actual));
    });
    return true;
}

bool Hierarchy::reorder(Handle child, std::size_t index, Handle parent)
{
    auto* children = m_registry.tryGet<Children>(parent);
    if (children == nullptr) {
        return false;
    }
    auto& entities = children->entities;
    const auto it  = std::find(entities.begin(), entities.end(), child);
    if (it == entities.end()) {
        return false;
    }
    const auto oldIndex = static_cast<std::size_t>(it - entities.begin());
    std::size_t desired = index;
    if (index == kAppend || index >= entities.size()) {
        desired = entities.size() - 1;
    }
    if (oldIndex == desired) {
        return true;
    }
    // index 是"完成后 child 应处的下标"。先删再在 desired 处插入：
    // 删掉靠前元素后右侧左移，插入 desired 恰好让它落在最终下标 desired 上
    // （不要再减一，那是"删除前的插入点"语义，会把右移少挪一格）。
    m_registry.patch<Children>(parent, [child, oldIndex, desired](Children& c) {
        c.entities.erase(c.entities.begin() + static_cast<std::ptrdiff_t>(oldIndex));
        c.entities.insert(c.entities.begin() + static_cast<std::ptrdiff_t>(desired), child);
    });
    return true;
}

bool Hierarchy::attach(Handle child, std::size_t index, Handle parent)
{
    if (!m_registry.valid(child)) {
        return false;
    }
    if (!parent.isValid()) {
        detach(child);
        return true;
    }
    if (!m_registry.valid(parent) || child == parent) {
        return false;
    }
    if (isAncestor(child, parent)) {
        return false;
    }

    const Handle currentParent = this->parent(child);
    if (currentParent == parent) {
        if (!reorder(child, index, parent)) {
            // 数据已不一致（有 Parent 却不在 Children 里）：按插入修复，而不是把
            // 一次合法的 setParent 变成 false。
            insert(child, index, parent);
        }
        return true;
    }

    detach(child);
    m_registry.emplace<Parent>(child, Parent{parent});
    insert(child, index, parent);

    return true;
}

void Hierarchy::detach(Handle child)
{
    const Parent* parentComp = m_registry.tryGet<Parent>(child);
    if (parentComp == nullptr) {
        return;
    }
    const Handle parent = parentComp->handle;
    m_registry.remove<Parent>(child);

    if (!m_registry.valid(parent) || !m_registry.has<Children>(parent)) {
        return;
    }
    m_registry.patch<Children>(parent, [child](Children& children) {
        children.entities.erase(std::remove(children.entities.begin(),
                                            children.entities.end(),
                                            child),
                                children.entities.end());
    });
}

void Hierarchy::extract(Handle node)
{
    if (!m_registry.valid(node)) {
        return;
    }
    detach(node); // 先摘除自己，避免在父节点的链表里留下悬空引用

    const std::vector<Handle> kids = children(node);
    std::vector<Handle> orphans    = kids; // 拷贝/快照：随后会拆掉 Children
    for (Handle child : orphans) {
        if (m_registry.valid(child)) {
            m_registry.remove<Parent>(child);
        }
    }
    m_registry.destroy(node);
}

void Hierarchy::destroy(Handle node)
{
    if (!m_registry.valid(node)) {
        return;
    }
    const std::vector<Handle> kids = children(node);
    for (Handle child : kids) {
        destroy(child);
    }

    detach(node);
    m_registry.destroy(node);
}

namespace hierarchy {

// ==============================================
// 内部实现细节
// ==============================================
namespace detail {
/**
 * @brief 检查挂载操作是否会形成循环引用
 * @param registry EnTT注册表引用
 * @param parent 目标父节点
 * @param child 待挂载子节点
 * @return true 会形成环（禁止挂载）；false 挂载合法
 */
inline bool check_cycle(Registry& registry, Handle parent, Handle child)
{
    if (!parent.isValid() || !child.isValid() || parent == child)
        return true;

    auto current = parent;
    while (current.isValid()) {
        if (current == child)
            return true;
        current = registry.get<Hierarchy>(current).parent;
    }
    return false;
}

inline size_t safe_add(size_t a, int b, bool* ok = nullptr)
{
    if (b < 0) {
        const auto abs_b = static_cast<size_t>(-static_cast<long long>(b));
        if (a < abs_b) {
            // 溢出
            if (ok) {
                *ok = false;
            }
            return 0;
        }

        return a - abs_b;
    }

    // b >= 0
    const auto bb = static_cast<size_t>(b);
    if (a > SIZE_MAX - bb) {
        // 溢出
        if (ok) {
            *ok = false;
        }
        return 0;
    }

    return a + bb;
}

/**
 * @brief 迭代式更新子树所有节点的depth缓存（避免递归栈溢出）
 * @param registry EnTT注册表引用
 * @param root 子树根节点
 * @param delta depth偏移量（正增负减）
 * @note 采用DFS前序遍历，支持任意深度子树，无栈溢出风险
 */
inline void update_subtree_depth(Registry& registry, Handle root, int delta)
{
    if (!registry.valid(root) || delta == 0)
        return;

    std::stack<Handle> stack;
    stack.push(root);

    while (!stack.empty()) {
        auto node = stack.top();
        stack.pop();

        auto& hier = registry.get<Hierarchy>(node);
        // warning: 严禁直接  static_cast<size_t>(delta)。
        // 禁止直接使用 size_t 无符号类型的 depth 直接
        // 与 static_cast<size_t>(delta) 转换相加，
        // int 类型 delta 可能是负数转成 size_t 无符号类型结果是无法预知的。
        hier.depth = safe_add(hier.depth, delta);

        // 逆序压入子节点，保证遍历顺序与逻辑顺序一致
        for (auto it = hier.last_child; it.isValid();) {
            stack.push(it);
            it = registry.get<Hierarchy>(it).prev_sibling;
        }
    }
}

/**
 * @brief 更新从起始节点开始所有后续兄弟节点的index缓存
 * @param registry EnTT注册表引用
 * @param start 起始节点
 * @param delta index偏移量（+1插入/-1删除）
 */
inline void update_sibling_indices_after(Registry& registry, Handle start, int delta)
{
    for (auto current = start; current.isValid();) {
        auto& hier = registry.get<Hierarchy>(current);
        // warning: 严禁直接 hier.index += static_cast<size_t>(delta); 原因同上
        hier.index = safe_add(hier.depth, delta);
        current    = hier.next_sibling;
    }
}

} // namespace detail

Handle parent(const Registry& registry, Handle child)
{
    const auto* hy = registry.tryGet<Hierarchy>(child);
    return hy ? Handle{hy->parent} : Handle{};
}

Handle child(const Registry& registry, std::size_t index, Handle parent)
{
    if (!registry.valid(parent) || !registry.has<Hierarchy>(parent))
        return {};

    const auto* hy = registry.tryGet<Hierarchy>(parent);
    if (index >= hy->child_count)
        return {};

    auto current = hy->first_child;
    for (uint32_t i = 0; i < index; ++i) {
        current = registry.get<Hierarchy>(Handle{current}).next_sibling;
    }
    return Handle{current};
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
    const auto* hy = registry.tryGet<Hierarchy>(parent);
    return hy ? hy->child_count : 0;
}

ChildRange children(const Registry& registry, Handle parent)
{
    return {const_cast<Registry&>(registry), parent};
}

std::optional<std::size_t> index(const Registry& registry, Handle node)
{
    if (!registry.valid(node))
        return std::nullopt;

    const auto* hy = registry.tryGet<Hierarchy>(node);
    return hy ? std::optional(hy->index) : std::nullopt;
}

bool isDescendant(const Registry& registry, Handle node, Handle ancestor)
{
    if (!ancestor.isValid()) {
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
    const auto* hy = registry.tryGet<Hierarchy>(node);
    return hy ? hy->depth : 0;
}

std::size_t size(const Registry& registry, Handle node)
{
    std::vector<Handle> out;
    collect(registry, node, out);
    return out.size();
}

std::vector<std::size_t> path(const Registry& registry, Handle node)
{
    std::vector<std::size_t> out;
    if (!registry.valid(node) || !registry.has<Hierarchy>(node))
        return out;

    for (auto current = node; hasParent(registry, current); current = parent(registry, current)) {
        auto& hier = registry.get<Hierarchy>(current);
        out.push_back(hier.index);
    }
    std::ranges::reverse(out);
    return out;
}

Handle pathNode(const Registry& registry, Handle root, std::span<const std::size_t> path)
{
    if (!registry.valid(root) || !registry.has<Hierarchy>(root))
        return {};

    auto current = root;
    for (auto index : path) {
        auto& hier = registry.get<Hierarchy>(current);
        if (index >= hier.child_count)
            return {};

        current = hier.first_child;
        for (std::size_t i = 0; i < index; ++i) {
            current = registry.get<Hierarchy>(Handle{current}).next_sibling;
        }
    }
    return current;
}

std::string pathString(std::span<const std::size_t> path)
{
    std::string s;
    for (std::size_t i = 0; i < path.size(); ++i) {
        if (i > 0)
            s += '/';
        s += std::to_string(path[i]);
    }
    return s;
}

bool append(Registry& registry, Handle child, Handle parent)
{
    if (!registry.valid(parent) || !registry.valid(child) || parent == child) {
        return false;
    }
    // 检测循环
    if (isDescendant(registry, parent, child)) {
        // parent 现在是 child 的后代：把 child 挂到 parent 下会形成环，拒绝。
        return false;
    }

    // 若子节点已有父节点先脱离
    detach(registry, child);

    // 确保父子节点都有组件
    Hierarchy& childHier  = registry.getOrEmplace<Hierarchy>(child);
    Hierarchy& parentHier = registry.getOrEmplace<Hierarchy>(parent);

    // 更新父子关系
    childHier.parent           = parent;
    const auto old_child_depth = childHier.depth;
    const auto new_child_depth = parentHier.depth + 1;
    childHier.depth            = new_child_depth;

    // 批量更新子树深度
    if (childHier.hasChildren()) {
        detail::update_subtree_depth(registry,
                                     child,
                                     static_cast<int>(new_child_depth - old_child_depth));
    }

    // 父节点无子节点场景
    if (parentHier.isLeaf()) {
        parentHier.first_child = child;
        parentHier.last_child  = child;
        childHier.prev_sibling = Handle::null;
        childHier.next_sibling = Handle::null;
        childHier.index        = 0;
    } else {
        // 追加到末尾
        const auto last        = parentHier.last_child;
        auto& lastHier         = registry.get<Hierarchy>(last);
        lastHier.next_sibling  = child;
        childHier.prev_sibling = last;
        childHier.next_sibling = Handle::null;
        parentHier.last_child  = child;
        childHier.index        = parentHier.child_count;
    }

    parentHier.child_count++;
    return true;
}

bool insertBefore(Registry& registry, Handle child, Handle target)
{
    if (!registry.valid(target) || !registry.valid(child) || child == target)
        return false;
    if (!registry.has<Hierarchy>(target))
        return false;

    const auto parent = registry.get<Hierarchy>(target).parent;
    if (!parent.isValid())
        return false;

    // 检测循环
    if (isDescendant(registry, parent, child)) {
        // parent 现在是 child 的后代：把 child 挂到 parent 下会形成环，拒绝。
        return false;
    }

    // 若子节点已有父节点先脱离
    detach(registry, child);

    // 确保父子节点都有组件
    auto& parentHier = registry.get<Hierarchy>(parent);
    auto& targetHier = registry.get<Hierarchy>(target);
    auto& childHier  = registry.getOrEmplace<Hierarchy>(child);

    // 更新父子关系
    childHier.parent           = parent;
    const auto old_child_depth = childHier.depth;
    const auto new_child_depth = parentHier.depth + 1;
    childHier.depth            = new_child_depth;

    // 更新子树深度
    if (childHier.hasChildren()) {
        detail::update_subtree_depth(registry,
                                     child,
                                     static_cast<int>(new_child_depth - old_child_depth));
    }

    // 链接前后节点
    childHier.prev_sibling = targetHier.prev_sibling;
    childHier.next_sibling = target;
    if (targetHier.prev_sibling.isValid()) {
        registry.get<Hierarchy>(targetHier.prev_sibling).next_sibling = child;
    } else {
        parentHier.first_child = child;
    }
    targetHier.prev_sibling = child;

    // 更新索引
    childHier.index = targetHier.index;
    detail::update_sibling_indices_after(registry, target, 1);
    parentHier.child_count++;

    return true;
}

bool insertAfter(Registry& registry, Handle child, Handle target)
{
    if (!registry.valid(target) || !registry.valid(child) || child == target)
        return false;
    if (!registry.has<Hierarchy>(target))
        return false;
    const auto parent = registry.get<Hierarchy>(target).parent;
    if (!parent.isValid())
        return false;
    // 检测循环
    if (isDescendant(registry, parent, child)) {
        // parent 现在是 child 的后代：把 child 挂到 parent 下会形成环，拒绝。
        return false;
    }

    // 若子节点已有父节点先脱离
    detach(registry, child);

    // 确保父子节点都有组件
    auto& parentHier = registry.get<Hierarchy>(parent);
    auto& targetHier = registry.get<Hierarchy>(target);
    auto& childHier  = registry.getOrEmplace<Hierarchy>(child);

    // 更新父子关系
    childHier.parent           = parent;
    const auto old_child_depth = childHier.depth;
    const auto new_child_depth = parentHier.depth + 1;
    childHier.depth            = new_child_depth;

    // 更新子树深度
    if (childHier.hasChildren()) {
        detail::update_subtree_depth(registry,
                                     child,
                                     static_cast<int>(new_child_depth - old_child_depth));
    }

    // 链接前后节点
    childHier.next_sibling = targetHier.next_sibling;
    childHier.prev_sibling = target;
    if (targetHier.next_sibling.isValid()) {
        registry.get<Hierarchy>(targetHier.next_sibling).prev_sibling = child;
    } else {
        parentHier.last_child = child;
    }
    targetHier.next_sibling = child;

    // 更新索引
    childHier.index = targetHier.index + 1;
    detail::update_sibling_indices_after(registry, childHier.next_sibling, 1);
    parentHier.child_count++;

    return true;
}

bool attach(Registry& registry, Handle child, Handle parent, Handle before)
{
    if (!registry.valid(parent) || !registry.valid(child) || parent == child) {
        return false;
    }
    if (isDescendant(registry, parent, child)) {
        // parent 现在是 child 的后代：把 child 挂到 parent 下会形成环，拒绝。
        return false;
    }

    Hierarchy* beforeHier = nullptr;
    if (before.isValid()) {
        if (!registry.valid(before)) {
            return false;
        }
        beforeHier = registry.tryGet<Hierarchy>(before);
        if (!beforeHier || beforeHier->parent != parent) {
            return false; // before 不是 parent 的直接子节点
        }
    }

    // "移动"语义：child 若已经挂在别处（含挂在同一个 parent 下的情况），
    // 先摘除，统一走"追加到新位置"这一条路径，不单独处理"同父重排序"。
    detach(registry, child);

    Hierarchy& childHier  = registry.getOrEmplace<Hierarchy>(child);
    Hierarchy& parentHier = registry.getOrEmplace<Hierarchy>(parent);
    childHier.parent      = parent;

    if (before.isValid()) {
        // detach() 可能让 beforeHier 指向的组件本身发生了 emplace/挪动？不会——
        // detach() 只操作 child 自己的 Hierarchy 以及它原父节点/原兄弟的
        // Hierarchy，与 before 无关，这里重新取一次指针只是防御性地
        // 避免对 unordered/稀疏集重新分配这类实现细节做假设。
        beforeHier               = registry.tryGet<Hierarchy>(before);
        const Handle prev        = beforeHier->prev_sibling;
        childHier.prev_sibling   = prev;
        childHier.next_sibling   = before;
        beforeHier->prev_sibling = child;
        if (prev.isValid()) {
            registry.tryGet<Hierarchy>(prev)->next_sibling = child;
        } else {
            parentHier.first_child = child;
        }
        // 更新 before 及后面兄弟节点索引
        childHier.index = beforeHier->index;
        for (auto current = beforeHier->next_sibling; current.isValid();) {
            auto& hier = registry.get<Hierarchy>(current);
            ++hier.index;
            current = hier.next_sibling;
        }
    } else {
        childHier.prev_sibling = parentHier.last_child;
        childHier.next_sibling = Handle{};
        if (parentHier.last_child.isValid()) {
            auto lastHier          = registry.tryGet<Hierarchy>(parentHier.last_child);
            lastHier->next_sibling = child;
            // 更新追加在最后的索引
            childHier.index        = lastHier->index + 1;
        } else {
            parentHier.first_child = child;
            // 前面没有兄弟节点，索引默认为 0
            childHier.index        = 0;
        }
        parentHier.last_child = child;
    }

    // 更新子树深度
    const auto old_child_depth = childHier.depth;
    const auto new_child_depth = parentHier.depth + 1;
    childHier.depth            = new_child_depth;
    if (childHier.hasChildren()) {
        detail::update_subtree_depth(registry,
                                     child,
                                     static_cast<int>(new_child_depth - old_child_depth));
    }

    ++parentHier.child_count;
    return true;
}

void detach(Registry& registry, Handle node)
{
    if (!registry.valid(node) || !registry.has<Hierarchy>(node))
        return;
    auto& nodeHier = registry.get<Hierarchy>(node);
    if (!nodeHier.parent.isValid())
        return; // 从未参与过层级关系，或本来就是游离状态：安全空操作

    auto& parentHier = registry.get<Hierarchy>(nodeHier.parent);

    // 更新前序兄弟指针
    if (nodeHier.prev_sibling.isValid()) {
        registry.get<Hierarchy>(nodeHier.prev_sibling).next_sibling = nodeHier.next_sibling;
    } else {
        parentHier.first_child = nodeHier.next_sibling;
    }

    // 更新后序兄弟指针
    if (nodeHier.next_sibling.isValid()) {
        auto& nextHier        = registry.get<Hierarchy>(nodeHier.next_sibling);
        nextHier.prev_sibling = nodeHier.prev_sibling;
        // 修正后继兄弟节点的 index（每个减 1）
        // detail::update_sibling_indices_after(registry, nodeHier.next_sibling, -1);
        for (auto current = nodeHier.next_sibling; current.isValid();) {
            auto& hier = registry.get<Hierarchy>(current);
            --hier.index;
            current = hier.next_sibling;
        }
    } else {
        parentHier.last_child = nodeHier.prev_sibling;
    }

    // 更新父节点计数
    if (parentHier.child_count > 0) {
        --parentHier.child_count;
    }

    // parentHier 为空指针的情况理论上不应该发生（attachChild() 总是同时
    // ensure() 了父子两侧的 Hierarchy），这里防御性地整体跳过链表修复而不是
    // 崩溃——万一外部直接摆弄过组件数据导致状态不一致，至少不会连锁出错。

    // 重置当前节点的父子关系字段
    nodeHier.parent       = Handle::null;
    nodeHier.prev_sibling = Handle::null;
    nodeHier.next_sibling = Handle::null;
    nodeHier.index        = 0;

    // 更新子树深度
    const auto old_depth = nodeHier.depth;
    nodeHier.depth       = 0;
    if (old_depth != 0 && nodeHier.hasChildren()) {
        detail::update_subtree_depth(registry, node, -static_cast<int>(old_depth));
    }
}

void extract(Registry& registry, Handle node)
{
    if (!registry.valid(node)) {
        return;
    }
    // 先摘除自己，避免在父节点的链表里留下悬空引用
    detach(registry, node);

    // 移除直接子节点的层级关系组件
    for (auto child : children(registry, node)) {
        if (registry.valid(child)) {
            registry.remove<Hierarchy>(child);
        }
    }

    // 销毁自己
    registry.destroy(node);
}

void destroy(Registry& registry, Handle node)
{
    if (!registry.valid(node)) {
        return;
    }
    // 先摘除自己，避免在父节点的链表里留下悬空引用
    detach(registry, node);

    // 先完整收集整棵子树（后序，保证子孙排在自己前面，之后反转先序序列），再统一销毁——
    // 不能一边遍历一边 destroy()：destroy() 会连带移除 Hierarchy 组件本身，
    // 一旦当前节点的 Hierarchy 在遍历尚未走完时就被摘掉，后续兄弟链接会
    // 读到已经失效的数据。
    std::vector<Handle> to_destroyed;
    collect(registry, node, to_destroyed);

    // 从叶子到根倒序销毁
    for (auto it = to_destroyed.rbegin(); it != to_destroyed.rend(); ++it) {
        registry.destroy(*it);
    }
}

void collect(const Registry& registry, Handle node, std::vector<Handle>& out, bool with_self)
{
    std::vector<Handle> stack;
    if (with_self) {
        stack.push_back(node);
    } else {
        std::vector<Handle> rootChildren;
        eachChild(registry, node, [&](Handle h) { rootChildren.push_back(h); });
        for (auto it = rootChildren.rbegin(); it != rootChildren.rend(); ++it) {
            stack.push_back(*it);
        }
    }

    while (!stack.empty()) {
        const auto current = stack.back();
        stack.pop_back();
        out.push_back(current);

        std::vector<Handle> children;
        eachChild(registry, current, [&](Handle h) { children.push_back(h); });
        for (auto it = children.rbegin(); it != children.rend(); ++it) {
            stack.push_back(*it);
        }
    }
}

} // namespace hierarchy
} // namespace bakuon::core
