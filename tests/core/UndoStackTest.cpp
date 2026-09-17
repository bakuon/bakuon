#include <gtest/gtest.h>

#include <string>

#include <bakuon/core/Container.h>
#include <bakuon/core/Entity.h>
#include <bakuon/core/UndoStack.h>

using namespace bakuon::core;

namespace {

struct Position
{
    float x = 0.f;
    float y = 0.f;

    friend bool operator==(const Position& a, const Position& b) noexcept
    {
        return a.x == b.x && a.y == b.y;
    }
};
void to_json(nlohmann::json& j, const Position& p)
{
    j = {{"x", p.x}, {"y", p.y}};
}
void from_json(const nlohmann::json& j, Position& p)
{
    j.at("x").get_to(p.x);
    j.at("y").get_to(p.y);
}

struct Selected
{
};
// Selected 是空结构体标签组件：entt 的空类型优化意味着序列化时根本不会有
// "值"需要写入/读出（同一现象在 SerializerTest.cpp 里也有说明），这两个
// 重载因此实际不会被调用，用 [[maybe_unused]] 显式承认这一点。
[[maybe_unused]] void to_json(nlohmann::json& j, const Selected&)
{
    j = nlohmann::json::object();
}
[[maybe_unused]] void from_json(const nlohmann::json&, Selected&)
{
}

// Name 持有 std::string——不是可平凡拷贝类型，早期基于逐字节内存拷贝的
// UndoStack 实现完全用不了这种字段（见 b_undostack.h 类文档"归档器"一节的
// 历史说明）。现在换成复用 DocumentSerializer 的 JSON 归档器之后，这类字段
// 可以直接参与撤销追踪，用它验证这一点确实生效。
struct Name
{
    std::string value;

    friend bool operator==(const Name& a, const Name& b) noexcept { return a.value == b.value; }
};
void to_json(nlohmann::json& j, const Name& n)
{
    j = {{"value", n.value}};
}
void from_json(const nlohmann::json& j, Name& n)
{
    j.at("value").get_to(n.value);
}

} // namespace

TEST(UndoStackTest, ConstructionCapturesInitialFrame)
{
    Registry registry;
    UndoStack<Position, Selected> undo(registry);

    EXPECT_EQ(undo.historyDepth(), 1u);
    EXPECT_FALSE(undo.canUndo());
    EXPECT_FALSE(undo.canRedo());
}

TEST(UndoStackTest, SnapshotThenUndoRestoresPreviousValue)
{
    Registry registry;
    const Entity node = registry.create();
    registry.emplace<Position>(node, 1.f, 2.f);

    UndoStack<Position, Selected> undo(registry);

    registry.patch<Position>(node, [](Position& p) { p.x = 99.f; });
    undo.snapshot();
    EXPECT_EQ(undo.historyDepth(), 2u);
    ASSERT_TRUE(undo.canUndo());

    undo.undo();
    ASSERT_TRUE(registry.valid(node)) << "撤销前后实体标识符应当保持稳定";
    EXPECT_EQ(registry.get<Position>(node), (Position{1.f, 2.f}));
    EXPECT_FALSE(undo.canUndo());
    EXPECT_TRUE(undo.canRedo());
}

TEST(UndoStackTest, RedoReappliesTheUndoneChange)
{
    Registry registry;
    const Entity node = registry.create();
    registry.emplace<Position>(node, 0.f, 0.f);
    UndoStack<Position, Selected> undo(registry);

    registry.patch<Position>(node, [](Position& p) { p.x = 5.f; });
    undo.snapshot();
    undo.undo();
    ASSERT_TRUE(undo.canRedo());

    undo.redo();
    EXPECT_EQ(registry.get<Position>(node).x, 5.f);
    EXPECT_FALSE(undo.canRedo());
}

TEST(UndoStackTest, UndoWithNoHistoryIsSafeNoop)
{
    Registry registry;
    UndoStack<Position, Selected> undo(registry);

    EXPECT_FALSE(undo.undo());
    EXPECT_FALSE(undo.redo());
}

TEST(UndoStackTest, NewSnapshotAfterUndoDiscardsRedoBranch)
{
    Registry registry;
    const Entity node = registry.create();
    registry.emplace<Position>(node, 0.f, 0.f);
    UndoStack<Position, Selected> undo(registry);

    registry.patch<Position>(node, [](Position& p) { p.x = 1.f; });
    undo.snapshot(); // frame 1: x=1
    registry.patch<Position>(node, [](Position& p) { p.x = 2.f; });
    undo.snapshot(); // frame 2: x=2

    undo.undo(); // back to frame 1 (x=1)
    ASSERT_EQ(registry.get<Position>(node).x, 1.f);
    ASSERT_TRUE(undo.canRedo());

    // 在旧状态上产生新的改动，应该丢弃原来能 redo() 到达的 "x=2" 分支。
    registry.patch<Position>(node, [](Position& p) { p.x = 42.f; });
    undo.snapshot();

    EXPECT_FALSE(undo.canRedo());
    undo.undo();
    EXPECT_EQ(registry.get<Position>(node).x, 1.f);
}

TEST(UndoStackTest, EntityCreationAndDestructionAreUndoable)
{
    Registry registry;
    UndoStack<Position, Selected> undo(registry); // frame 0: 空

    const Entity node = registry.create();
    registry.emplace<Position>(node, 3.f, 4.f);
    undo.snapshot(); // frame 1: node 存在

    undo.undo();
    EXPECT_FALSE(registry.valid(node)) << "撤销到创建之前，实体应当不再存在";

    undo.redo();
    ASSERT_TRUE(registry.valid(node));
    EXPECT_EQ(registry.get<Position>(node).x, 3.f);
}

TEST(UndoStackTest, ObserversRegisteredBeforeUndoStillFireAfterRestore)
{
    // 这是本类最关键的正确性保证：反复 clear()+reload 不应该让调用方之前建立的
    // Connection 失效，见 b_undostack.h 类文档"关键的正确性依据"一节。
    Container container;
    Registry& registry = container.registry();
    const Entity node  = registry.create();
    registry.emplace<Position>(node, 0.f, 0.f);

    int constructCount     = 0;
    int destroyCount       = 0;
    Connection onConstruct = container.onConstruct<Position>(
        [&](Container&, Entity) { ++constructCount; });
    Connection onDestroy = container.onDestroy<Position>(
        [&](Container&, Entity) { ++destroyCount; });

    UndoStack<Position, Selected> undo(registry);

    registry.patch<Position>(node, [](Position& p) { p.x = 7.f; });
    undo.snapshot();

    const int constructBefore = constructCount;
    const int destroyBefore   = destroyCount;

    undo.undo();

    // undo() 内部会 clear() 再 reload：应当能观察到旧 Position 被销毁、
    // 新 Position 被重新构造——而不是观察者从此哑掉。
    EXPECT_GT(destroyCount, destroyBefore) << "clear() 应当照常触发 onDestroy";
    EXPECT_GT(constructCount, constructBefore) << "reload 应当照常触发 onConstruct";
}

TEST(UndoStackTest, CapacityEvictsOldestFrame)
{
    Registry registry;
    const Entity node = registry.create();
    registry.emplace<Position>(node, 0.f, 0.f);

    UndoStack<Position, Selected> undo(registry, /*capacity=*/2);
    EXPECT_EQ(undo.historyDepth(), 1u);

    registry.patch<Position>(node, [](Position& p) { p.x = 1.f; });
    undo.snapshot();
    EXPECT_EQ(undo.historyDepth(), 2u);

    registry.patch<Position>(node, [](Position& p) { p.x = 2.f; });
    undo.snapshot();
    // 容量为 2：最旧的一帧（初始的 x=0）应该已经被丢弃。
    EXPECT_EQ(undo.historyDepth(), 2u);

    // 现在应该只能撤销到 x=1，不能再往前追溯到 x=0。
    ASSERT_TRUE(undo.undo());
    EXPECT_EQ(registry.get<Position>(node).x, 1.f);
    EXPECT_FALSE(undo.canUndo());
}

TEST(UndoStackTest, ClearHistoryKeepsOnlyCurrentState)
{
    Registry registry;
    const Entity node = registry.create();
    registry.emplace<Position>(node, 0.f, 0.f);
    UndoStack<Position, Selected> undo(registry);

    registry.patch<Position>(node, [](Position& p) { p.x = 1.f; });
    undo.snapshot();
    ASSERT_TRUE(undo.canUndo());

    undo.clearHistory();
    EXPECT_EQ(undo.historyDepth(), 1u);
    EXPECT_FALSE(undo.canUndo());
    EXPECT_FALSE(undo.canRedo());
    // clearHistory() 之后的这一帧应该是"当前"状态（x=1），不是回退到最初的 x=0。
    EXPECT_EQ(registry.get<Position>(node).x, 1.f);
}

TEST(UndoStackTest, TagComponentParticipatesInSnapshot)
{
    Registry registry;
    const Entity node = registry.create();
    registry.emplace<Position>(node, 0.f, 0.f);
    UndoStack<Position, Selected> undo(registry);

    registry.emplace<Selected>(node);
    undo.snapshot();
    ASSERT_TRUE(registry.all_of<Selected>(node));

    undo.undo();
    EXPECT_FALSE(registry.all_of<Selected>(node)) << "标签组件的挂接/摘除也应该能被撤销";
}

TEST(UndoStackTest, NonTriviallyCopyableComponentWithStdStringIsUndoable)
{
    // 这是本文件最重要的一条回归测试：早期基于逐字节内存拷贝的实现要求
    // Components... 全部可平凡拷贝，std::string 字段完全没法用；现在换成
    // 复用 DocumentSerializer 的 JSON 归档器之后应该可以正常参与撤销/重做。
    Registry registry;
    const Entity node = registry.create();
    registry.emplace<Name>(node, Name{"first"});

    UndoStack<Name> undo(registry);

    registry.patch<Name>(node, [](Name& n) { n.value = "second"; });
    undo.snapshot();
    ASSERT_EQ(registry.get<Name>(node).value, "second");

    ASSERT_TRUE(undo.undo());
    EXPECT_EQ(registry.get<Name>(node).value, "first");

    ASSERT_TRUE(undo.redo());
    EXPECT_EQ(registry.get<Name>(node).value, "second");
}

TEST(UndoStackTest, MixOfTriviallyAndNonTriviallyCopyableComponents)
{
    Registry registry;
    const Entity node = registry.create();
    registry.emplace<Position>(node, 0.f, 0.f);
    registry.emplace<Name>(node, Name{"node-1"});

    UndoStack<Position, Name> undo(registry);

    registry.patch<Position>(node, [](Position& p) { p.x = 9.f; });
    registry.patch<Name>(node, [](Name& n) { n.value = "node-1-renamed"; });
    undo.snapshot();

    ASSERT_TRUE(undo.undo());
    EXPECT_EQ(registry.get<Position>(node).x, 0.f);
    EXPECT_EQ(registry.get<Name>(node).value, "node-1");

    ASSERT_TRUE(undo.redo());
    EXPECT_EQ(registry.get<Position>(node).x, 9.f);
    EXPECT_EQ(registry.get<Name>(node).value, "node-1-renamed");
}
