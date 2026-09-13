#include <gtest/gtest.h>

#include <bakuon/core/Components.h>
#include <bakuon/core/Registry.h>
#include <bakuon/core/UndoStack.h>

using namespace bakuon::core;
using namespace bakuon::core::components;

TEST(ComponentsTest, DefaultsMatchDocumentedSemantics)
{
    Registry registry;
    const Handle node = registry.create();

    registry.emplace<Visible>(node);
    registry.emplace<Locked>(node);
    registry.emplace<Enabled>(node);

    EXPECT_TRUE(registry.tryGet<Visible>(node)->value) << "默认应该可见";
    EXPECT_FALSE(registry.tryGet<Locked>(node)->value) << "默认不应该锁定";
    EXPECT_TRUE(registry.tryGet<Enabled>(node)->value) << "默认应该启用";
}

TEST(ComponentsTest, VisibleAndEnabledAreIndependentAxes)
{
    Registry registry;
    const Handle node = registry.create();
    registry.emplace<Visible>(node, false);
    registry.emplace<Enabled>(node, true);

    EXPECT_FALSE(registry.tryGet<Visible>(node)->value);
    EXPECT_TRUE(registry.tryGet<Enabled>(node)->value)
        << "可见性和启用状态应该是两个独立维度，互不影响";
}

TEST(ComponentsTest, SelectedIsATagWithNoPayload)
{
    Registry registry;
    const Handle node = registry.create();
    registry.emplace<Selected>(node);
    EXPECT_TRUE(registry.has<Selected>(node));
    registry.remove<Selected>(node);
    EXPECT_FALSE(registry.has<Selected>(node));
}

TEST(ComponentsTest, TriviallyCopyableComponentsWorkWithUndoStack)
{
    Registry registry;
    const Handle node = registry.create();
    registry.emplace<Visible>(node, true);
    registry.emplace<Locked>(node, false);
    registry.emplace<Enabled>(node, true);

    UndoStack<Visible, Locked, Enabled> undo(registry);

    registry.patch<Visible>(node, [](Visible& v) { v.value = false; });
    registry.patch<Locked>(node, [](Locked& l) { l.value = true; });
    undo.snapshot();

    ASSERT_TRUE(undo.undo());
    EXPECT_TRUE(registry.tryGet<Visible>(node)->value);
    EXPECT_FALSE(registry.tryGet<Locked>(node)->value);

    ASSERT_TRUE(undo.redo());
    EXPECT_FALSE(registry.tryGet<Visible>(node)->value);
    EXPECT_TRUE(registry.tryGet<Locked>(node)->value);
}

TEST(ComponentsTest, NameAndTagRoundTripThroughDocumentSerializer)
{
    // Name/Tag 持有 std::string，不满足 UndoStack 要求的可平凡拷贝约束
    // （见 b_components.h 的说明），因此只用 DocumentSerializer 验证。
    Registry registry;
    const Handle node = registry.create();
    registry.emplace<Name>(node, Name{"图层 1"});
    registry.emplace<Tag>(node, Tag{"背景"});

    DocumentSerializer<Name, Tag> serializer(registry);
    const nlohmann::json doc = serializer.save();

    Registry other;
    DocumentSerializer<Name, Tag> otherSerializer(other);
    ASSERT_TRUE(otherSerializer.load(doc).success());

    ASSERT_TRUE(other.valid(node));
    EXPECT_EQ(other.tryGet<Name>(node)->value, "图层 1");
    EXPECT_EQ(other.tryGet<Tag>(node)->value, "背景");
}

TEST(ComponentsTest, AllComponentsRoundTripTogetherThroughDocumentSerializer)
{
    Registry registry;
    const Handle node = registry.create();
    registry.emplace<Name>(node, Name{"节点"});
    registry.emplace<Visible>(node, false);
    registry.emplace<Locked>(node, true);
    registry.emplace<Enabled>(node, false);
    registry.emplace<Selected>(node);
    registry.emplace<Tag>(node, Tag{"分组A"});

    DocumentSerializer<Name, Visible, Locked, Enabled, Selected, Tag> serializer(registry);
    const nlohmann::json doc = serializer.save();

    Registry other;
    DocumentSerializer<Name, Visible, Locked, Enabled, Selected, Tag> otherSerializer(other);
    ASSERT_TRUE(otherSerializer.load(doc).success());

    EXPECT_EQ(other.tryGet<Name>(node)->value, "节点");
    EXPECT_FALSE(other.tryGet<Visible>(node)->value);
    EXPECT_TRUE(other.tryGet<Locked>(node)->value);
    EXPECT_FALSE(other.tryGet<Enabled>(node)->value);
    EXPECT_TRUE(other.has<Selected>(node));
    EXPECT_EQ(other.tryGet<Tag>(node)->value, "分组A");
}
