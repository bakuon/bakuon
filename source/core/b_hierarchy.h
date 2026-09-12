#pragma once

#include <algorithm>
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
 * @brief Hierarchy Linked 十字链表数组结构实现
 *
 * 在 EnTT 中实现层级结构（Hierarchy），‌
 * 十字链表结构（Parent / FirstChild / LastChild / PrevSibling / NextSibling）‌
 * 远比 Parent + Children(vector<Handle>) 更符合 EnTT 的核心价值
 *（内存连续性、缓存友好性）以及高性能需求，尤其是在复杂的 GUI 应用层中。
 *
 * 劣势：随机访问第 N 个子节点需遍历链表 O(N)，但 GUI 中极少需要随机访问特定索引的子控件，通常是顺序处理。
 */
namespace hierarchy {

// ==============================================
// 核心组件定义（十字链表结构，32字节固定大小，半缓存行对齐）
// ==============================================
/**
 * @brief 层级关系核心组件（十字链表实现）
 * @details 完全无堆内存分配，所有字段内联存储，完美适配EnTT稀疏集内存布局，缓存命中率接近理论最大值
 */
struct Hierarchy
{
    Handle parent;              ///< 父节点引用，根节点为 entt::null
    Handle first_child;         ///< 第一个子节点引用
    Handle last_child;          ///< 最后一个子节点引用
    Handle prev_sibling;        ///< 前序兄弟节点引用
    Handle next_sibling;        ///< 后序兄弟节点引用
    std::size_t child_count{0}; ///< 直接子节点数缓存，O(1)查询
    std::size_t depth{0};       ///< 节点深度缓存，根节点depth=0
    std::size_t index{0};       ///< 在父节点子列表中的索引缓存，O(1)查询

    /**
     * @brief 判断当前节点是否为根节点
     * @return true 无父节点，是根节点
     */
    [[nodiscard]] constexpr bool isRoot() const noexcept { return !parent.isValid(); }

    /**
     * @brief 判断当前节点是否为叶节点
     * @return true 无子节点，是叶节点
     */
    [[nodiscard]] constexpr bool isLeaf() const noexcept { return !first_child.isValid(); }

    /**
     * @brief 判断当前节点是否存在父节点
     * @return true 有父节点
     */
    [[nodiscard]] constexpr bool hasParent() const noexcept { return parent.isValid(); }

    /**
     * @brief 判断当前节点是否存在子节点
     * @return true 有子节点
     */
    [[nodiscard]] constexpr bool hasChildren() const noexcept { return first_child.isValid(); }
};

// ==============================================
// 子节点迭代器（支持C++20 range-based for）
// ==============================================
/**
 * @brief 子节点迭代器，零开销遍历兄弟链表
 * @details 满足std::input_iterator要求，无额外内存拷贝
 */
struct ChildIterator
{
    using iterator_category = std::input_iterator_tag;
    using value_type        = Handle;
    using difference_type   = std::ptrdiff_t;
    using pointer           = const Handle*;
    using reference         = const Handle&;

    Registry* reg{nullptr};
    Handle current{entt::null};

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
        assert(reg && reg->valid(current) && "Dereferencing invalid iterator");
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

/**
 * @brief 子节点遍历范围适配器
 * @details 配合range-based for使用，自动处理首尾迭代器
 */
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
    PreOrder,  ///< 父节点先于其子孙被访问。
    PostOrder, ///< 每个节点保证在其全部子孙都被访问之后才被访问（用于安全销毁），
               ///< 但不同分支之间的相对顺序不保证等同于教科书式的严格后序——
               ///< 见 b_hierarchy.cpp 里 detail::collectPreOrder() 的实现说明。
};

// ==============================================
// 查询与谱系操作
// ==============================================

/**
 * @brief 获取父节点
 * @param registry EnTT注册表引用
 * @param child 目标子节点
 * @return 父节点；根节点/无效节点返回 entt::null
 */
[[nodiscard]] Handle parent(const Registry& registry, Handle child);
/**
 * @brief 获取父节点下指定索引的子节点
 * @param registry EnTT注册表引用
 * @param parent 父节点
 * @param index 子节点索引
 * @return Handle 子节点；索引越界/无效返回 entt::null
 */
[[nodiscard]] Handle child(const Registry& registry, std::size_t index, Handle parent);
/**
 * @brief 判断当前节点是否存在父节点
 * @param registry  EnTT注册表引用
 * @param node 目标节点
 * @return true 有父节点且是有效的
 */
[[nodiscard]] bool hasParent(const Registry& registry, Handle node);
/**
 * @brief 获取子节点遍历范围
 * @param registry EnTT注册表引用
 * @param parent 父节点
 * @return ChildRange 可直接用于range-based for的遍历范围
 * @note 示例：for (auto child : Hierarchy::children(reg, parent)) { ... }
 */
[[nodiscard]] bool hasChildren(const Registry& registry, Handle parent);
/**
 * @brief O(1)获取直接子节点数量
 * @param registry EnTT注册表引用
 * @param parent 目标节点
 * @return std::size_t 子节点数；无效节点返回 0
 */
[[nodiscard]] std::size_t childCount(const Registry& registry, Handle parent);
/**
 * @brief 获取直接子节点遍历范围
 * @param registry EnTT注册表引用
 * @param parent 父节点
 * @return ChildRange 可直接用于range-based for的遍历范围
 * @note 示例：for (auto child : Hierarchy::children(reg, parent)) { ... }
 */
[[nodiscard]] ChildRange children(const Registry& registry, Handle parent);
/**
 * @brief node 是否是 ancestor 的（直接或间接）后代； or isAncestor(...)
 * @param registry EnTT注册表引用
 * @param node 待判断后代
 * @param ancestor 祖先节点
 * @return true 是后代；false 不是
 */
[[nodiscard]] bool isDescendant(const Registry& registry, Handle node, Handle ancestor);

/**
 * @brief O(1)获取节点在父节点下的索引
 * @param registry EnTT注册表引用
 * @param node 目标节点
 * @return std::size_t 节点索引；根节点/无效节点返回0
 */
[[nodiscard]] std::optional<std::size_t> index(const Registry& registry, Handle node);
/**
 * @brief O(1)获取节点深度
 * @param registry EnTT注册表引用
 * @param node 目标节点
 * @return uint32_t 节点深度；无效节点返回0
 */
[[nodiscard]] std::size_t depth(const Registry& registry, Handle node);
/**
 * @brief 获取节点的所有后代节点数量（包含自己）
 * @param registry EnTT注册表引用
 * @param node 子树根节点
 * @return std::size_t 后代节点数
 * @note 前序遍历顺序
 */
[[nodiscard]] std::size_t size(const Registry& registry, Handle node);

// ==============================================
// 行路径操作
// ==============================================

/**
 * @brief 获取从根到当前节点的行路径（索引序列）
 * @details 路径格式为[第一层子索引, 第二层子索引, ..., 当前节点索引]，可用于持久化/UI定位
 * @param registry EnTT注册表引用
 * @param node 目标节点
 * @return std::vector 索引路径；根节点返回空向量
 * @note 返回值可直接传入 pathNode 做逆向查找
 */
[[nodiscard]] std::vector<std::size_t> path(const Registry& registry, Handle node);
/**
 * @brief 根据根节点和行路径查找目标节点
 * @param registry EnTT注册表引用
 * @param root 查找起点根节点
 * @param path 行路径序列
 * @return Handle 找到的节点；路径无效返回 Handle::null
 */
[[nodiscard]] Handle pathNode(const Registry& registry, Handle root,
                              std::span<const std::size_t> path);
/**
 * @brief 将行路径转换为人类可读字符串（调试用）
 * @param path 行路径
 * @return std::string 格式类似"0/2/1"的路径字符串
 */
[[nodiscard]] std::string pathString(std::span<const std::size_t> path);

// ==============================================
// 基础修改操作
// ==============================================

/**
 * @brief 将子节点追加到父节点子列表末尾
 * @details 若子节点已有父节点会自动detach，自动维护所有缓存字段
 * @param registry EnTT注册表引用
 * @param parent 父节点
 * @param child 待追加子节点
 * @return true 挂载成功；false 节点无效/循环挂载
 * @note 自动为无 Hierarchy 的节点创建组件
 */
bool append(Registry& registry, Handle child, Handle parent);

/**
 * @brief 将子节点插入到目标节点之前
 * @param registry EnTT注册表引用
 * @param target 插入位置参考节点（必须已有父节点）
 * @param child 待插入节点
 * @return true 插入成功；false 节点无效/循环挂载/目标节点无父节点
 */
bool insertBefore(Registry& registry, Handle child, Handle target);

/**
 * @brief 将子节点插入到目标节点之后
 * @param registry EnTT注册表引用
 * @param target 插入位置参考节点（必须已有父节点）
 * @param child 待插入节点
 * @return true 插入成功；false 节点无效/循环挂载/目标节点无父节点
 */
bool insertAfter(Registry& registry, Handle child, Handle target);

/**
 * @brief 把 child 挂接为 parent 的子节点
 * @details "移动"语义：若 child 当前已有父节点，会先把它从原来的位置摘除，
 *          再挂到新位置——与 Qt::setParent()/常见场景图 API 的重新挂接习惯一致
 * @param registry EnTT注册表引用
 * @param parent 父节点
 * @param child 待挂载子节点
 * @param before 为空 Handle（默认）时追加到末尾；否则插入到这个兄弟节点
 *               之前，before 必须已经是 parent 的直接子节点。
 * @return true 挂载成功；false 表示前置条件不满足：
 *         节点无效或parent == child/循环挂载/显式指定了 before 但它不是 parent 的直接子节点
 * @note 自动为无 Hierarchy 的节点创建组件
 */
bool attach(Registry& registry, Handle child, Handle parent, Handle before = {});

/**
 * @brief 将节点从父节点脱离，成为独立根节点
 * @details 自动更新父节点/兄弟节点指针、索引和深度缓存，不销毁节点本身
 * @param registry EnTT注册表引用
 * @param node 要脱离的节点
 * @note 子树depth会自动重置为以当前节点为根的层级
 */
void detach(Registry& registry, Handle node);

/// 解散：从父节点摘掉，直系孩子变成根级孤儿，
/// 然后销毁自己，子孙实体仍然存活，只是失去了这一层父亲。

/**
 * @brief 从父节点摘掉，然后销毁自己
 * @details 直系孩子变成根级孤儿，子孙实体仍然存活，只是失去了这一层父亲。
 * @param registry EnTT注册表引用
 * @param node 要摘取的节点
 * @note 子树depth会自动重置为以当前节点为根的层级
 */
void extract(Registry& registry, Handle node);

/**
 * @brief 销毁节点及其完整子树
 * @details 先detach避免父节点野指针，迭代式后序遍历销毁所有后代，无内存泄漏
 * @param registry EnTT注册表引用
 * @param node 待销毁子树的根节点
 * @note 自动销毁所有节点上的其他组件，不会留下孤儿实体
 */
void destroy(Registry& registry, Handle node);

/**
 * @brief 获取节点的所有后代节点（前序遍历顺序）
 * @param registry EnTT注册表引用
 * @param node 子树根节点
 * @param out 后代节点列表
 * @param with_self 是否包含当前节点
 */
void collect(const Registry& registry, Handle node, std::vector<Handle>& out,
             bool with_self = false);

/**
 * @brief 遍历 parent 的直接子节点，回调签名为 void(Handle child)。
 * @param func void(Handle child)
 * @todo 遍历前会先把当前的子节点顺序整体快照到一个临时数组，再逐个调用回调，
 *       如果 func() 内部又反过来调用了 attachChild()/detach()（比如"删
 *       除选中的子节点"这类典型场景），直接在原始链表上遍历会在回调修改链
 *       表后读到已经失效的nextSibling，使用快照可以规避这个问题。
 */
template<typename Func>
void eachChild(const Registry& registry, Handle parent, Func&& func)
{
    // registry.native().view<Hierarchy>().each(std::forward<Func>(func));
    for (Handle child : children(registry, parent)) {
        func(child);
    }
}

template<typename Func>
void eachDescendant(const Registry& registry, Handle parent, Func&& func,
                    TraversalOrder order = TraversalOrder::PreOrder)
{
    std::vector<Handle> descendants;
    collect(registry, parent, descendants);

    if (order == TraversalOrder::PreOrder) {
        for (Handle h : descendants) {
            func(h);
        }
    } else {
        // 后序（近似）：反转先序序列。这不是教科书式的严格后序（同一层兄弟节点
        // 之间的相对顺序在反转后会被打乱），但保留了"任意节点一定排在它的全部
        // 祖先之前"这一核心不变量——对 destroy subtree 这类"必须先销毁子孙、
        // 再销毁自己"的场景而言已经足够且更简单，不需要为了追求教科书式的
        // 双栈后序算法而增加实现复杂度。
        for (auto it = descendants.rbegin(); it != descendants.rend(); ++it) {
            func(*it);
        }
    }
}

} // namespace hierarchy

} // namespace bakuon::core
