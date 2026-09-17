#include <gtest/gtest.h>

#include <algorithm>
#include <vector>

#include <bakuon/core/Entity.h>
#include <bakuon/core/Hierarchy.h>

using namespace bakuon::core;
namespace h = bakuon::core::hierarchy;

namespace {

struct Name
{
    std::string value;
};

} // namespace

TEST(HierarchyTest, FreshEntityHasNoParentAndNoChildren)
{
    Registry registry;
    const Entity node = registry.create();

    EXPECT_FALSE(h::hasParent(registry, node));
    EXPECT_FALSE(registry.valid(h::parent(registry, node)));
    EXPECT_EQ(h::childCount(registry, node), 0u);
}

TEST(HierarchyTest, AttachChildAppendsInOrder)
{
    Registry registry;
    const Entity parent = registry.create();
    const Entity a      = registry.create();
    const Entity b      = registry.create();
    const Entity c      = registry.create();

    EXPECT_TRUE(h::attach(registry, a, parent));
    EXPECT_TRUE(h::attach(registry, b, parent));
    EXPECT_TRUE(h::attach(registry, c, parent));

    EXPECT_EQ(h::childCount(registry, parent), 3u);
    EXPECT_EQ(h::parent(registry, a), parent);
    EXPECT_EQ(h::parent(registry, b), parent);
    EXPECT_EQ(h::parent(registry, c), parent);

    std::vector<Entity> order;
    h::eachChild(registry, parent, [&](Entity child) { order.push_back(child); });
    ASSERT_EQ(order.size(), 3u);
    EXPECT_EQ(order[0], a);
    EXPECT_EQ(order[1], b);
    EXPECT_EQ(order[2], c);
}

TEST(HierarchyTest, AttachChildBeforeSiblingInsertsAtCorrectPosition)
{
    Registry registry;
    const Entity parent   = registry.create();
    const Entity a        = registry.create();
    const Entity b        = registry.create();
    const Entity inserted = registry.create();

    ASSERT_TRUE(h::attach(registry, a, parent));
    ASSERT_TRUE(h::attach(registry, b, parent));

    EXPECT_TRUE(h::attach(registry, inserted, parent, /*beforeSibling=*/b));

    std::vector<Entity> order;
    h::eachChild(registry, parent, [&](Entity child) { order.push_back(child); });
    ASSERT_EQ(order.size(), 3u);
    EXPECT_EQ(order[0], a);
    EXPECT_EQ(order[1], inserted);
    EXPECT_EQ(order[2], b);
}

TEST(HierarchyTest, AttachChildRejectsSelfParenting)
{
    Registry registry;
    const Entity node = registry.create();
    EXPECT_FALSE(h::attach(registry, node, node));
}

TEST(HierarchyTest, AttachChildRejectsCycles)
{
    Registry registry;
    const Entity grandparent = registry.create();
    const Entity parent      = registry.create();
    const Entity child       = registry.create();

    ASSERT_TRUE(h::attach(registry, parent, grandparent));
    ASSERT_TRUE(h::attach(registry, child, parent));

    // 试图把 grandparent 挂到它自己的后代（child）下面——必须被拒绝，且不应该
    // 破坏现有的任何层级关系。
    EXPECT_FALSE(h::attach(registry, grandparent, child));
    EXPECT_EQ(h::parent(registry, parent), grandparent);
    EXPECT_EQ(h::parent(registry, child), parent);
}

TEST(HierarchyTest, AttachChildRejectsInvalidBeforeSibling)
{
    Registry registry;
    const Entity parent  = registry.create();
    const Entity a       = registry.create();
    const Entity strayer = registry.create(); // 不属于 parent 的子节点
    ASSERT_TRUE(h::attach(registry, a, parent));

    EXPECT_FALSE(h::attach(registry, parent, registry.create(), strayer));
    EXPECT_EQ(h::childCount(registry, parent), 1u) << "拒绝时不应该产生任何副作用";
}

TEST(HierarchyTest, ReattachingMovesChildToNewParent)
{
    Registry registry;
    const Entity parentA = registry.create();
    const Entity parentB = registry.create();
    const Entity child   = registry.create();

    ASSERT_TRUE(h::attach(registry, child, parentA));
    ASSERT_EQ(h::childCount(registry, parentA), 1u);

    // 再次 attach 到不同的父节点：应视为"移动"，而不是报错或产生重复挂接。
    EXPECT_TRUE(h::attach(registry, child, parentB));
    EXPECT_EQ(h::parent(registry, child), parentB);
    EXPECT_EQ(h::childCount(registry, parentA), 0u);
    EXPECT_EQ(h::childCount(registry, parentB), 1u);
}

TEST(HierarchyTest, DetachMakesNodeFreeWithoutAffectingItsOwnChildren)
{
    Registry registry;
    const Entity parent = registry.create();
    const Entity child  = registry.create();
    const Entity grand  = registry.create();

    ASSERT_TRUE(h::attach(registry, child, parent));
    ASSERT_TRUE(h::attach(registry, grand, child));

    h::detach(registry, child);

    EXPECT_FALSE(h::hasParent(registry, child));
    EXPECT_EQ(h::childCount(registry, parent), 0u);
    // child 自己的子树应该原样保留，只是不再挂在 parent 下面了。
    EXPECT_EQ(h::parent(registry, grand), child);
    EXPECT_EQ(h::childCount(registry, child), 1u);
}

TEST(HierarchyTest, DetachMiddleSiblingPreservesRemainingOrder)
{
    Registry registry;
    const Entity parent = registry.create();
    const Entity a      = registry.create();
    const Entity b      = registry.create();
    const Entity c      = registry.create();
    ASSERT_TRUE(h::attach(registry, a, parent));
    ASSERT_TRUE(h::attach(registry, b, parent));
    ASSERT_TRUE(h::attach(registry, c, parent));

    h::detach(registry, b);

    std::vector<Entity> order;
    h::eachChild(registry, parent, [&](Entity child) { order.push_back(child); });
    ASSERT_EQ(order.size(), 2u);
    EXPECT_EQ(order[0], a);
    EXPECT_EQ(order[1], c);
    EXPECT_EQ(h::childCount(registry, parent), 2u);
}

TEST(HierarchyTest, DetachOnFreeEntityIsSafeNoop)
{
    Registry registry;
    const Entity node = registry.create();
    h::detach(registry, node); // 从未挂接过，不应该崩溃
    EXPECT_FALSE(h::hasParent(registry, node));
}

TEST(HierarchyTest, ForEachChildSnapshotSurvivesMutationDuringIteration)
{
    Registry registry;
    const Entity parent = registry.create();
    const Entity a      = registry.create();
    const Entity b      = registry.create();
    const Entity c      = registry.create();
    ASSERT_TRUE(h::attach(registry, a, parent));
    ASSERT_TRUE(h::attach(registry, b, parent));
    ASSERT_TRUE(h::attach(registry, c, parent));

    std::vector<Entity> visited;
    // 回调内部对 b 做 detach()——如果没有先做快照，遍历到 a 之后读取
    // a->nextSibling 应该还是 b，但 b 已经被摘除、其 nextSibling 字段被清空，
    // 若实现不安全会在这里直接漏掉 c 或者崩溃。
    h::eachChild(registry, parent, [&](Entity child) {
        visited.push_back(child);
        if (child == a) {
            h::detach(registry, b);
        }
    });

    ASSERT_EQ(visited.size(), 3u) << "快照应该保留遍历开始那一刻的完整子节点列表";
    EXPECT_EQ(visited[0], a);
    EXPECT_EQ(visited[1], b);
    EXPECT_EQ(visited[2], c);
    EXPECT_EQ(h::childCount(registry, parent), 2u) << "本次遍历结束后 b 应该已经被摘除";
}

TEST(HierarchyTest, IsDescendantOf)
{
    Registry registry;
    const Entity root  = registry.create();
    const Entity mid   = registry.create();
    const Entity leaf  = registry.create();
    const Entity stray = registry.create();
    ASSERT_TRUE(h::attach(registry, mid, root));
    ASSERT_TRUE(h::attach(registry, leaf, mid));

    EXPECT_TRUE(h::isDescendant(registry, leaf, root));
    EXPECT_TRUE(h::isDescendant(registry, leaf, mid));
    EXPECT_TRUE(h::isDescendant(registry, mid, root));
    EXPECT_FALSE(h::isDescendant(registry, root, leaf));
    EXPECT_FALSE(h::isDescendant(registry, stray, root));
    EXPECT_FALSE(h::isDescendant(registry, leaf, nullentity));
}

TEST(HierarchyTest, ForEachDescendantPreOrderVisitsParentBeforeChildren)
{
    Registry registry;
    const Entity root = registry.create();
    const Entity a    = registry.create();
    const Entity a1   = registry.create();
    const Entity b    = registry.create();
    ASSERT_TRUE(h::attach(registry, a, root));
    ASSERT_TRUE(h::attach(registry, a1, a));
    ASSERT_TRUE(h::attach(registry, b, root));

    std::vector<Entity> order;
    h::eachDescendant(registry, root, [&](Entity node) { order.push_back(node); });

    ASSERT_EQ(order.size(), 3u);
    EXPECT_EQ(order[0], a);
    EXPECT_EQ(order[1], a1);
    EXPECT_EQ(order[2], b);
}

TEST(HierarchyTest, ForEachDescendantPostOrderVisitsChildrenBeforeAncestors)
{
    Registry registry;
    const Entity root = registry.create();
    const Entity a    = registry.create();
    const Entity a1   = registry.create();
    const Entity b    = registry.create();
    ASSERT_TRUE(h::attach(registry, a, root));
    ASSERT_TRUE(h::attach(registry, a1, a));
    ASSERT_TRUE(h::attach(registry, b, root));

    std::vector<Entity> order;
    h::eachDescendant(
        registry, root, [&](Entity node) { order.push_back(node); }, h::TraversalOrder::PostOrder);

    ASSERT_EQ(order.size(), 3u);
    // 核心不变量：任何节点必须排在它的全部祖先之前（a1 在 a 之前，a/b 都在
    // 隐含的 root 之前——root 本身不在结果里）。
    auto indexOf = [&](Entity target) {
        return static_cast<std::size_t>(std::find(order.begin(), order.end(), target)
                                        - order.begin());
    };
    EXPECT_LT(indexOf(a1), indexOf(a));
}

TEST(HierarchyTest, DestroySubtreeRemovesEntireBranchAndItsComponents)
{
    Registry registry;
    const Entity root = registry.create();
    const Entity a    = registry.create();
    const Entity a1   = registry.create();
    const Entity b    = registry.create(); // 兄弟分支，不应受影响
    ASSERT_TRUE(h::attach(registry, a, root));
    ASSERT_TRUE(h::attach(registry, a1, a));
    ASSERT_TRUE(h::attach(registry, b, root));
    registry.emplace<Name>(a1, Name{"a1"});

    h::destroy(registry, a);

    EXPECT_FALSE(registry.valid(a));
    EXPECT_FALSE(registry.valid(a1));
    EXPECT_TRUE(registry.valid(root));
    EXPECT_TRUE(registry.valid(b));
    EXPECT_EQ(h::childCount(registry, root), 1u);
    std::vector<Entity> remaining;
    h::eachChild(registry, root, [&](Entity c) { remaining.push_back(c); });
    ASSERT_EQ(remaining.size(), 1u);
    EXPECT_EQ(remaining[0], b);
}

TEST(HierarchyTest, DestroySubtreeOnLeafJustDestroysItself)
{
    Registry registry;
    const Entity parent = registry.create();
    const Entity leaf   = registry.create();
    ASSERT_TRUE(h::attach(registry, leaf, parent));

    h::destroy(registry, leaf);

    EXPECT_FALSE(registry.valid(leaf));
    EXPECT_TRUE(registry.valid(parent));
    EXPECT_EQ(h::childCount(registry, parent), 0u);
}

TEST(HierarchyTest, ReorderWithinParent)
{
    Registry registry;
    const Entity parent = registry.create();
    const Entity a      = registry.create();
    const Entity b      = registry.create();
    const Entity c      = registry.create();
    ASSERT_TRUE(h::append(registry, a, parent));
    ASSERT_TRUE(h::append(registry, b, parent));
    ASSERT_TRUE(h::append(registry, c, parent));

    EXPECT_TRUE(h::reorder(registry, c, 0)); // c, a, b
    std::vector<Entity> order;
    h::eachChild(registry, parent, [&](Entity ch) { order.push_back(ch); });
    ASSERT_EQ(order.size(), 3u);
    EXPECT_EQ(order[0], c);
    EXPECT_EQ(order[1], a);
    EXPECT_EQ(order[2], b);
    EXPECT_EQ(h::index(registry, c).value(), 0u);
    EXPECT_EQ(h::index(registry, a).value(), 1u);
    EXPECT_EQ(h::index(registry, b).value(), 2u);
}

TEST(HierarchyTest, MoveUpAndMoveDown)
{
    Registry registry;
    const Entity parent = registry.create();
    const Entity a      = registry.create();
    const Entity b      = registry.create();
    const Entity c      = registry.create();
    ASSERT_TRUE(h::append(registry, a, parent));
    ASSERT_TRUE(h::append(registry, b, parent));
    ASSERT_TRUE(h::append(registry, c, parent));

    EXPECT_FALSE(h::moveUp(registry, a));
    EXPECT_TRUE(h::moveUp(registry, c));   // a, c, b
    EXPECT_TRUE(h::moveDown(registry, a)); // c, a, b

    std::vector<Entity> order;
    h::eachChild(registry, parent, [&](Entity ch) { order.push_back(ch); });
    ASSERT_EQ(order.size(), 3u);
    EXPECT_EQ(order[0], c);
    EXPECT_EQ(order[1], a);
    EXPECT_EQ(order[2], b);
}

TEST(HierarchyTest, ExtractTurnsChildrenIntoRoots)
{
    Registry registry;
    const Entity parent = registry.create();
    const Entity child  = registry.create();
    const Entity grand  = registry.create();
    ASSERT_TRUE(h::append(registry, child, parent));
    ASSERT_TRUE(h::append(registry, grand, child));

    h::extract(registry, child);
    EXPECT_FALSE(registry.valid(child));
    EXPECT_FALSE(h::hasParent(registry, grand));
    EXPECT_EQ(h::depth(registry, grand), 0u);
    EXPECT_TRUE(registry.valid(parent));
    EXPECT_EQ(h::childCount(registry, parent), 0u);
}

TEST(HierarchyTest, PathAndPathNodeRoundTrip)
{
    Registry registry;
    const Entity root = registry.create();
    const Entity a    = registry.create();
    const Entity a0   = registry.create();
    ASSERT_TRUE(h::append(registry, a, root));
    ASSERT_TRUE(h::append(registry, a0, a));

    const auto p = h::path(registry, a0);
    ASSERT_EQ(p.size(), 2u);
    EXPECT_EQ(h::pathNode(registry, root, p), a0);
    EXPECT_FALSE(h::pathString(p).empty());
}

TEST(HierarchyTest, RootsCollectsTopLevelNodes)
{
    Registry registry;
    const Entity r1    = registry.create();
    const Entity r2    = registry.create();
    const Entity child = registry.create();
    // 显式挂 Hierarchy 成为根
    ASSERT_TRUE(h::append(registry, child, r1));

    std::vector<Entity> list;
    h::roots(registry, list);
    // r1、r2 都带 Hierarchy（append 会 ensure 父），child 有 parent
    EXPECT_NE(std::find(list.begin(), list.end(), r1), list.end());
    // r2 若从未 ensure Hierarchy，可能不在列表——ensure 仅在参与链接时发生
    // 再对 r2 做一次无子的 ensure 路径：attach 一个临时再 detach 会留下 Hierarchy
    const Entity tmp = registry.create();
    ASSERT_TRUE(h::append(registry, tmp, r2));
    h::detach(registry, tmp);
    registry.destroy(tmp);

    list.clear();
    h::roots(registry, list);
    EXPECT_NE(std::find(list.begin(), list.end(), r1), list.end());
    EXPECT_NE(std::find(list.begin(), list.end(), r2), list.end());
    EXPECT_EQ(std::find(list.begin(), list.end(), child), list.end());
}
