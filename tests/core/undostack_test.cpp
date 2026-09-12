#include <gtest/gtest.h>

#include <bakuon/core/Registry.h>
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

struct Selected
{
};

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
    const Handle node = registry.create();
    registry.emplace<Position>(node, 1.f, 2.f);

    UndoStack<Position, Selected> undo(registry);

    registry.patch<Position>(node, [](Position& p) { p.x = 99.f; });
    undo.snapshot();
    EXPECT_EQ(undo.historyDepth(), 2u);
    ASSERT_TRUE(undo.canUndo());

    undo.undo();
    ASSERT_TRUE(registry.valid(node)) << "撤销前后实体标识符应当保持稳定";
    EXPECT_EQ(*registry.tryGet<Position>(node), (Position{1.f, 2.f}));
    EXPECT_FALSE(undo.canUndo());
    EXPECT_TRUE(undo.canRedo());
}

TEST(UndoStackTest, RedoReappliesTheUndoneChange)
{
    Registry registry;
    const Handle node = registry.create();
    registry.emplace<Position>(node, 0.f, 0.f);
    UndoStack<Position, Selected> undo(registry);

    registry.patch<Position>(node, [](Position& p) { p.x = 5.f; });
    undo.snapshot();
    undo.undo();
    ASSERT_TRUE(undo.canRedo());

    undo.redo();
    EXPECT_EQ(registry.tryGet<Position>(node)->x, 5.f);
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
    const Handle node = registry.create();
    registry.emplace<Position>(node, 0.f, 0.f);
    UndoStack<Position, Selected> undo(registry);

    registry.patch<Position>(node, [](Position& p) { p.x = 1.f; });
    undo.snapshot(); // frame 1: x=1
    registry.patch<Position>(node, [](Position& p) { p.x = 2.f; });
    undo.snapshot(); // frame 2: x=2

    undo.undo(); // back to frame 1 (x=1)
    ASSERT_EQ(registry.tryGet<Position>(node)->x, 1.f);
    ASSERT_TRUE(undo.canRedo());

    // 在旧状态上产生新的改动，应该丢弃原来能 redo() 到达的 "x=2" 分支。
    registry.patch<Position>(node, [](Position& p) { p.x = 42.f; });
    undo.snapshot();

    EXPECT_FALSE(undo.canRedo());
    undo.undo();
    EXPECT_EQ(registry.tryGet<Position>(node)->x, 1.f);
}

TEST(UndoStackTest, EntityCreationAndDestructionAreUndoable)
{
    Registry registry;
    UndoStack<Position, Selected> undo(registry); // frame 0: 空

    const Handle node = registry.create();
    registry.emplace<Position>(node, 3.f, 4.f);
    undo.snapshot(); // frame 1: node 存在

    undo.undo();
    EXPECT_FALSE(registry.valid(node)) << "撤销到创建之前，实体应当不再存在";

    undo.redo();
    ASSERT_TRUE(registry.valid(node));
    EXPECT_EQ(registry.tryGet<Position>(node)->x, 3.f);
}

TEST(UndoStackTest, ObserversRegisteredBeforeUndoStillFireAfterRestore)
{
    // 这是本类最关键的正确性保证：反复 clear()+reload 不应该让调用方之前建立的
    // Connection 失效，见 b_undostack.h 类文档"关键的正确性依据"一节。
    Registry registry;
    const Handle node = registry.create();
    registry.emplace<Position>(node, 0.f, 0.f);

    int constructCount     = 0;
    int destroyCount       = 0;
    Connection onConstruct = registry.onConstruct<Position>(
        [&](Registry&, Handle) { ++constructCount; });
    Connection onDestroy = registry.onDestroy<Position>([&](Registry&, Handle) { ++destroyCount; });

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
    const Handle node = registry.create();
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
    EXPECT_EQ(registry.tryGet<Position>(node)->x, 1.f);
    EXPECT_FALSE(undo.canUndo());
}

TEST(UndoStackTest, ClearHistoryKeepsOnlyCurrentState)
{
    Registry registry;
    const Handle node = registry.create();
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
    EXPECT_EQ(registry.tryGet<Position>(node)->x, 1.f);
}

TEST(UndoStackTest, TagComponentParticipatesInSnapshot)
{
    Registry registry;
    const Handle node = registry.create();
    registry.emplace<Position>(node, 0.f, 0.f);
    UndoStack<Position, Selected> undo(registry);

    registry.emplace<Selected>(node);
    undo.snapshot();
    ASSERT_TRUE(registry.has<Selected>(node));

    undo.undo();
    EXPECT_FALSE(registry.has<Selected>(node)) << "标签组件的挂接/摘除也应该能被撤销";
}
