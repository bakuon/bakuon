#include <gtest/gtest.h>

#include <string>

#include <core/b_bimap.h>

using bakuon::core::detail::Bimap;

TEST(BimapTest, InsertFindContains)
{
    Bimap<std::string, int> bm;
    EXPECT_TRUE(bm.insert("one", 1));
    EXPECT_TRUE(bm.insert("two", 2));
    EXPECT_EQ(bm.size(), 2u);

    EXPECT_TRUE(bm.left().contains("one"));
    ASSERT_TRUE(bm.left().find("one").has_value());
    EXPECT_EQ(*bm.left().find("one"), 1);

    EXPECT_TRUE(bm.right().contains(2));
    ASSERT_TRUE(bm.right().find(2).has_value());
    EXPECT_EQ(*bm.right().find(2), "two");

    EXPECT_FALSE(bm.left().contains("three"));
}

TEST(BimapTest, RejectsConflictingInsert)
{
    Bimap<std::string, int> bm;
    ASSERT_TRUE(bm.insert("a", 1));

    // 左键已存在
    EXPECT_FALSE(bm.insert("a", 2));
    // 右键已被占用
    EXPECT_FALSE(bm.insert("b", 1));
    EXPECT_EQ(bm.size(), 1u);
}

TEST(BimapTest, InsertOrReplaceClearsBothStaleSides)
{
    Bimap<std::string, int> bm;
    bm.insert("a", 1);
    bm.insert("b", 2);

    // "a" 原本对应 1，"b" 原本对应 2；把 "a" 重新绑定到 2 应该同时清掉
    // "a"->1 和 "b"->2 这两条旧映射，只留下唯一的 "a"->2。
    bm.insertOrReplace("a", 2);

    EXPECT_EQ(bm.size(), 1u);
    EXPECT_EQ(bm.left().find("a"), 2);
    EXPECT_FALSE(bm.left().contains("b"));
    EXPECT_FALSE(bm.right().contains(1));
}

TEST(BimapTest, EraseLeftAndRightAreSymmetric)
{
    Bimap<std::string, int> bm;
    bm.insert("a", 1);
    bm.insert("b", 2);

    EXPECT_TRUE(bm.eraseLeft("a"));
    EXPECT_FALSE(bm.left().contains("a"));
    EXPECT_FALSE(bm.right().contains(1));
    EXPECT_FALSE(bm.eraseLeft("a")) << "重复删除应返回 false";

    EXPECT_TRUE(bm.eraseRight(2));
    EXPECT_TRUE(bm.empty());
}

TEST(BimapTest, LeftViewErase)
{
    Bimap<std::string, int> bm;
    bm.insert("a", 1);
    EXPECT_TRUE(bm.left().erase("a"));
    EXPECT_TRUE(bm.empty());
}

TEST(BimapTest, IterationVisitsAllPairs)
{
    Bimap<std::string, int> bm;
    bm.insert("a", 1);
    bm.insert("b", 2);
    bm.insert("c", 3);

    int sum = 0;
    for (const auto& [left, right] : bm) {
        (void) left;
        sum += right;
    }
    EXPECT_EQ(sum, 6);
}

TEST(BimapTest, ConstBimapOnlyExposesReadOnlyViews)
{
    Bimap<std::string, int> bm;
    bm.insert("a", 1);

    const auto& cbm = bm;
    EXPECT_TRUE(cbm.left().contains("a"));
    EXPECT_EQ(cbm.left().at("a"), 1);
    EXPECT_EQ(cbm.right().at(1), "a");
    // cbm.left().erase("a"); // 应当无法编译：const 视图不提供 erase()
}

TEST(BimapTest, AtThrowsOnMissingKey)
{
    Bimap<std::string, int> bm;
    EXPECT_THROW(bm.left().at("missing"), std::out_of_range);
}

TEST(BimapTest, ClearAndSwap)
{
    Bimap<std::string, int> a;
    Bimap<std::string, int> b;
    a.insert("x", 1);
    b.insert("y", 2);

    swap(a, b);
    EXPECT_TRUE(a.left().contains("y"));
    EXPECT_TRUE(b.left().contains("x"));

    a.clear();
    EXPECT_TRUE(a.empty());
}

TEST(BimapTest, InitializerListConstruction)
{
    Bimap<std::string, int> bm{{"a", 1}, {"b", 2}};
    EXPECT_EQ(bm.size(), 2u);
    EXPECT_EQ(bm.left().find("b"), 2);
}
