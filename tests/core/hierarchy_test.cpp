#include <gtest/gtest.h>

#include <algorithm>
#include <vector>

#include <bakuon/core/Hierarchy.h>
#include <bakuon/core/Registry.h>

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
    const Handle node = registry.create();

    EXPECT_FALSE(h::hasParent(registry, node));
    EXPECT_FALSE(h::parent(registry, node).isValid());
    EXPECT_EQ(h::childCount(registry, node), 0u);
}

TEST(HierarchyTest, AttachChildAppendsInOrder)
{
    Registry registry;
    const Handle parent = registry.create();
    const Handle a      = registry.create();
    const Handle b      = registry.create();
    const Handle c      = registry.create();

    EXPECT_TRUE(h::attach(registry, parent, a));
    EXPECT_TRUE(h::attach(registry, parent, b));
    EXPECT_TRUE(h::attach(registry, parent, c));

    EXPECT_EQ(h::childCount(registry, parent), 3u);
    EXPECT_EQ(h::parent(registry, a), parent);
    EXPECT_EQ(h::parent(registry, b), parent);
    EXPECT_EQ(h::parent(registry, c), parent);

    std::vector<Handle> order;
    h::eachChild(registry, parent, [&](Handle child) { order.push_back(child); });
    ASSERT_EQ(order.size(), 3u);
    EXPECT_EQ(order[0], a);
    EXPECT_EQ(order[1], b);
    EXPECT_EQ(order[2], c);
}

TEST(HierarchyTest, AttachChildBeforeSiblingInsertsAtCorrectPosition)
{
    Registry registry;
    const Handle parent   = registry.create();
    const Handle a        = registry.create();
    const Handle b        = registry.create();
    const Handle inserted = registry.create();

    ASSERT_TRUE(h::attach(registry, parent, a));
    ASSERT_TRUE(h::attach(registry, parent, b));

    EXPECT_TRUE(h::attach(registry, parent, inserted, /*beforeSibling=*/b));

    std::vector<Handle> order;
    h::eachChild(registry, parent, [&](Handle child) { order.push_back(child); });
    ASSERT_EQ(order.size(), 3u);
    EXPECT_EQ(order[0], a);
    EXPECT_EQ(order[1], inserted);
    EXPECT_EQ(order[2], b);
}

TEST(HierarchyTest, AttachChildRejectsSelfParenting)
{
    Registry registry;
    const Handle node = registry.create();
    EXPECT_FALSE(h::attach(registry, node, node));
}

TEST(HierarchyTest, AttachChildRejectsCycles)
{
    Registry registry;
    const Handle grandparent = registry.create();
    const Handle parent      = registry.create();
    const Handle child       = registry.create();

    ASSERT_TRUE(h::attach(registry, grandparent, parent));
    ASSERT_TRUE(h::attach(registry, parent, child));

    // 试图把 grandparent 挂到它自己的后代（child）下面——必须被拒绝，且不应该
    // 破坏现有的任何层级关系。
    EXPECT_FALSE(h::attach(registry, child, grandparent));
    EXPECT_EQ(h::parent(registry, parent), grandparent);
    EXPECT_EQ(h::parent(registry, child), parent);
}

TEST(HierarchyTest, AttachChildRejectsInvalidBeforeSibling)
{
    Registry registry;
    const Handle parent  = registry.create();
    const Handle a       = registry.create();
    const Handle strayer = registry.create(); // 不属于 parent 的子节点
    ASSERT_TRUE(h::attach(registry, parent, a));

    EXPECT_FALSE(h::attach(registry, parent, registry.create(), strayer));
    EXPECT_EQ(h::childCount(registry, parent), 1u) << "拒绝时不应该产生任何副作用";
}

TEST(HierarchyTest, ReattachingMovesChildToNewParent)
{
    Registry registry;
    const Handle parentA = registry.create();
    const Handle parentB = registry.create();
    const Handle child   = registry.create();

    ASSERT_TRUE(h::attach(registry, parentA, child));
    ASSERT_EQ(h::childCount(registry, parentA), 1u);

    // 再次 attach 到不同的父节点：应视为"移动"，而不是报错或产生重复挂接。
    EXPECT_TRUE(h::attach(registry, parentB, child));
    EXPECT_EQ(h::parent(registry, child), parentB);
    EXPECT_EQ(h::childCount(registry, parentA), 0u);
    EXPECT_EQ(h::childCount(registry, parentB), 1u);
}

TEST(HierarchyTest, DetachMakesNodeFreeWithoutAffectingItsOwnChildren)
{
    Registry registry;
    const Handle parent = registry.create();
    const Handle child  = registry.create();
    const Handle grand  = registry.create();

    ASSERT_TRUE(h::attach(registry, parent, child));
    ASSERT_TRUE(h::attach(registry, child, grand));

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
    const Handle parent = registry.create();
    const Handle a      = registry.create();
    const Handle b      = registry.create();
    const Handle c      = registry.create();
    ASSERT_TRUE(h::attach(registry, parent, a));
    ASSERT_TRUE(h::attach(registry, parent, b));
    ASSERT_TRUE(h::attach(registry, parent, c));

    h::detach(registry, b);

    std::vector<Handle> order;
    h::eachChild(registry, parent, [&](Handle child) { order.push_back(child); });
    ASSERT_EQ(order.size(), 2u);
    EXPECT_EQ(order[0], a);
    EXPECT_EQ(order[1], c);
    EXPECT_EQ(h::childCount(registry, parent), 2u);
}

TEST(HierarchyTest, DetachOnFreeEntityIsSafeNoop)
{
    Registry registry;
    const Handle node = registry.create();
    h::detach(registry, node); // 从未挂接过，不应该崩溃
    EXPECT_FALSE(h::hasParent(registry, node));
}

TEST(HierarchyTest, ForEachChildSnapshotSurvivesMutationDuringIteration)
{
    Registry registry;
    const Handle parent = registry.create();
    const Handle a      = registry.create();
    const Handle b      = registry.create();
    const Handle c      = registry.create();
    ASSERT_TRUE(h::attach(registry, parent, a));
    ASSERT_TRUE(h::attach(registry, parent, b));
    ASSERT_TRUE(h::attach(registry, parent, c));

    std::vector<Handle> visited;
    // 回调内部对 b 做 detach()——如果没有先做快照，遍历到 a 之后读取
    // a->nextSibling 应该还是 b，但 b 已经被摘除、其 nextSibling 字段被清空，
    // 若实现不安全会在这里直接漏掉 c 或者崩溃。
    h::eachChild(registry, parent, [&](Handle child) {
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
    const Handle root  = registry.create();
    const Handle mid   = registry.create();
    const Handle leaf  = registry.create();
    const Handle stray = registry.create();
    ASSERT_TRUE(h::attach(registry, root, mid));
    ASSERT_TRUE(h::attach(registry, mid, leaf));

    EXPECT_TRUE(h::isDescendant(registry, leaf, root));
    EXPECT_TRUE(h::isDescendant(registry, leaf, mid));
    EXPECT_TRUE(h::isDescendant(registry, mid, root));
    EXPECT_FALSE(h::isDescendant(registry, root, leaf));
    EXPECT_FALSE(h::isDescendant(registry, stray, root));
    EXPECT_FALSE(h::isDescendant(registry, leaf, Handle{}));
}

TEST(HierarchyTest, ForEachDescendantPreOrderVisitsParentBeforeChildren)
{
    Registry registry;
    const Handle root = registry.create();
    const Handle a    = registry.create();
    const Handle a1   = registry.create();
    const Handle b    = registry.create();
    ASSERT_TRUE(h::attach(registry, root, a));
    ASSERT_TRUE(h::attach(registry, a, a1));
    ASSERT_TRUE(h::attach(registry, root, b));

    std::vector<Handle> order;
    h::eachDescendant(registry, root, [&](Handle node) { order.push_back(node); });

    ASSERT_EQ(order.size(), 3u);
    EXPECT_EQ(order[0], a);
    EXPECT_EQ(order[1], a1);
    EXPECT_EQ(order[2], b);
}

TEST(HierarchyTest, ForEachDescendantPostOrderVisitsChildrenBeforeAncestors)
{
    Registry registry;
    const Handle root = registry.create();
    const Handle a    = registry.create();
    const Handle a1   = registry.create();
    const Handle b    = registry.create();
    ASSERT_TRUE(h::attach(registry, root, a));
    ASSERT_TRUE(h::attach(registry, a, a1));
    ASSERT_TRUE(h::attach(registry, root, b));

    std::vector<Handle> order;
    h::eachDescendant(
        registry, root, [&](Handle node) { order.push_back(node); }, h::TraversalOrder::PostOrder);

    ASSERT_EQ(order.size(), 3u);
    // 核心不变量：任何节点必须排在它的全部祖先之前（a1 在 a 之前，a/b 都在
    // 隐含的 root 之前——root 本身不在结果里）。
    auto indexOf = [&](Handle target) {
        return static_cast<std::size_t>(std::find(order.begin(), order.end(), target)
                                        - order.begin());
    };
    EXPECT_LT(indexOf(a1), indexOf(a));
}

TEST(HierarchyTest, DestroySubtreeRemovesEntireBranchAndItsComponents)
{
    Registry registry;
    const Handle root = registry.create();
    const Handle a    = registry.create();
    const Handle a1   = registry.create();
    const Handle b    = registry.create(); // 兄弟分支，不应受影响
    ASSERT_TRUE(h::attach(registry, root, a));
    ASSERT_TRUE(h::attach(registry, a, a1));
    ASSERT_TRUE(h::attach(registry, root, b));
    registry.emplace<Name>(a1, Name{"a1"});

    h::destroy(registry, a);

    EXPECT_FALSE(registry.valid(a));
    EXPECT_FALSE(registry.valid(a1));
    EXPECT_TRUE(registry.valid(root));
    EXPECT_TRUE(registry.valid(b));
    EXPECT_EQ(h::childCount(registry, root), 1u);
    std::vector<Handle> remaining;
    h::eachChild(registry, root, [&](Handle c) { remaining.push_back(c); });
    ASSERT_EQ(remaining.size(), 1u);
    EXPECT_EQ(remaining[0], b);
}

TEST(HierarchyTest, DestroySubtreeOnLeafJustDestroysItself)
{
    Registry registry;
    const Handle parent = registry.create();
    const Handle leaf   = registry.create();
    ASSERT_TRUE(h::attach(registry, parent, leaf));

    h::destroy(registry, leaf);

    EXPECT_FALSE(registry.valid(leaf));
    EXPECT_TRUE(registry.valid(parent));
    EXPECT_EQ(h::childCount(registry, parent), 0u);
}
