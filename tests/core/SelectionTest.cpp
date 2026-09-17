#include <gtest/gtest.h>

#include <bakuon/core/Entity.h>
#include <bakuon/core/Selection.h>

using namespace bakuon::core;
namespace sel = bakuon::core::selection;
using bakuon::core::components::Selected;

TEST(SelectionTest, FreeFunctionsRoundTrip)
{
    Registry reg;
    const Entity a = reg.create();
    const Entity b = reg.create();

    sel::add(reg, a);
    sel::add(reg, b);
    EXPECT_TRUE(reg.all_of<Selected>(a));
    EXPECT_TRUE(reg.all_of<Selected>(b));
    EXPECT_EQ(sel::all(reg).size(), 2u);

    sel::remove(reg, a);
    EXPECT_FALSE(reg.all_of<Selected>(a));
    EXPECT_TRUE(reg.all_of<Selected>(b));

    sel::clear(reg);
    EXPECT_TRUE(sel::all(reg).empty());
    EXPECT_FALSE(reg.all_of<Selected>(b));
}

TEST(SelectionTest, FreeFunctionExclusive)
{
    Registry reg;
    const Entity a = reg.create();
    const Entity b = reg.create();
    sel::add(reg, a);
    sel::add(reg, b);

    sel::exclusive(reg, a);
    EXPECT_TRUE(reg.all_of<Selected>(a));
    EXPECT_FALSE(reg.all_of<Selected>(b));
    EXPECT_EQ(sel::all(reg).size(), 1u);
}

TEST(SelectionTest, OrderedAndPrimary)
{
    Registry reg;
    sel::Selection selection(reg);
    const Entity a = reg.create();
    const Entity b = reg.create();
    const Entity c = reg.create();

    selection.add(a);
    selection.add(b);
    selection.add(c);
    ASSERT_EQ(selection.count(), 3u);
    EXPECT_EQ(selection.primary(), c);
    EXPECT_EQ(selection.ordered()[0], a);
    EXPECT_EQ(selection.ordered()[1], b);
    EXPECT_EQ(selection.ordered()[2], c);

    // 已选中再 add → 移到末尾成为 primary
    selection.add(a);
    EXPECT_EQ(selection.count(), 3u);
    EXPECT_EQ(selection.primary(), a);
    EXPECT_EQ(selection.ordered().back(), a);
}

TEST(SelectionTest, SetPrimary)
{
    Registry reg;
    sel::Selection selection(reg);
    const Entity a = reg.create();
    const Entity b = reg.create();
    selection.add(a);
    selection.add(b);
    EXPECT_EQ(selection.primary(), b);

    selection.setPrimary(a);
    EXPECT_EQ(selection.primary(), a);
    EXPECT_EQ(selection.ordered().back(), a);
    EXPECT_TRUE(selection.contains(b));
}

TEST(SelectionTest, ToggleAndExclusive)
{
    Registry reg;
    sel::Selection selection(reg);
    const Entity a = reg.create();

    selection.toggle(a);
    EXPECT_TRUE(selection.contains(a));
    EXPECT_TRUE(reg.all_of<Selected>(a));

    selection.toggle(a);
    EXPECT_FALSE(selection.contains(a));
    EXPECT_FALSE(reg.all_of<Selected>(a));

    selection.exclusive(a);
    EXPECT_EQ(selection.count(), 1u);
    EXPECT_EQ(selection.primary(), a);
}

TEST(SelectionTest, SetReplacesEntireSelection)
{
    Registry reg;
    sel::Selection selection(reg);
    const Entity a = reg.create();
    const Entity b = reg.create();
    const Entity c = reg.create();
    selection.add(a);
    selection.add(b);

    selection.set({c, a});
    EXPECT_EQ(selection.count(), 2u);
    EXPECT_EQ(selection.ordered()[0], c);
    EXPECT_EQ(selection.ordered()[1], a);
    EXPECT_EQ(selection.primary(), a);
    EXPECT_FALSE(selection.contains(b));
    EXPECT_FALSE(reg.all_of<Selected>(b));
    EXPECT_TRUE(reg.all_of<Selected>(c));
}

TEST(SelectionTest, PruneAfterDestroy)
{
    Registry reg;
    sel::Selection selection(reg);
    const Entity a = reg.create();
    const Entity b = reg.create();
    selection.add(a);
    selection.add(b);

    reg.destroy(a);
    // onDestroy 已从有序列表剔除；再 prune 应幂等
    selection.prune();
    EXPECT_FALSE(selection.contains(a));
    EXPECT_TRUE(selection.contains(b));
    EXPECT_EQ(selection.count(), 1u);
    EXPECT_EQ(selection.primary(), b);
}

TEST(SelectionTest, OnChangedFires)
{
    Registry reg;
    sel::Selection selection(reg);
    int fires = 0;
    auto conn = selection.onChanged([&](const sel::Selection&) { ++fires; });

    selection.add(reg.create());
    selection.clear();
    EXPECT_GE(fires, 2);

    conn.disconnect();
    const int before = fires;
    selection.add(reg.create());
    EXPECT_EQ(fires, before) << "disconnect 后不应再触发";
}

TEST(SelectionTest, ClearIsIdempotentOnEmpty)
{
    Registry reg;
    sel::Selection selection(reg);
    int fires = 0;
    auto conn = selection.onChanged([&](const sel::Selection&) { ++fires; });
    selection.clear();
    EXPECT_EQ(fires, 0);
}
