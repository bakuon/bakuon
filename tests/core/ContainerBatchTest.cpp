#include <gtest/gtest.h>

#include <algorithm>
#include <vector>

#include <bakuon/core/Container.h>

using namespace bakuon::core;

namespace {

struct Position
{
    float x = 0.f;
    float y = 0.f;
};

struct Rotation
{
    float angle = 0.f;
};

} // namespace

TEST(ContainerBatchTest, WithoutBatchingEveryPatchFiresImmediately)
{
    Container container;
    const Entity id = container.create();
    container.emplace<Position>(id, 0.f, 0.f);

    int updateCount = 0;
    Connection conn = container.onUpdate<Position>([&](Container&, Entity) { ++updateCount; });

    container.patch<Position>(id, [](Position& p) { p.x = 1.f; });
    container.patch<Position>(id, [](Position& p) { p.x = 2.f; });

    EXPECT_EQ(updateCount, 2) << "没有开启批次时，每次 patch() 都应该立即各自触发一次";
}

TEST(ContainerBatchTest, BatchCoalescesRepeatedUpdatesIntoOne)
{
    Container container;
    const Entity id = container.create();
    container.emplace<Position>(id, 0.f, 0.f);

    int updateCount = 0;
    Connection conn = container.onUpdate<Position>([&](Container&, Entity) { ++updateCount; });

    container.beginBatch();
    container.patch<Position>(id, [](Position& p) { p.x = 1.f; });
    container.patch<Position>(id, [](Position& p) { p.x = 2.f; });
    container.patch<Position>(id, [](Position& p) { p.x = 3.f; });
    EXPECT_EQ(updateCount, 0) << "批次期间不应该有任何通知被投递出去";
    container.endBatch();

    EXPECT_EQ(updateCount, 1) << "三次 patch() 应该被合并成一次通知";
    EXPECT_EQ(container.tryGet<Position>(id)->x, 3.f) << "数据本身应当是最后一次写入的值";
}

TEST(ContainerBatchTest, BatchGuardEndsOnScopeExit)
{
    Container container;
    const Entity id = container.create();
    container.emplace<Position>(id, 0.f, 0.f);

    int updateCount = 0;
    Connection conn = container.onUpdate<Position>([&](Container&, Entity) { ++updateCount; });

    {
        Container::BatchGuard batch(container);
        container.patch<Position>(id, [](Position& p) { p.x = 1.f; });
        EXPECT_EQ(updateCount, 0);
    }
    EXPECT_EQ(updateCount, 1) << "BatchGuard 离开作用域应该自动结束批次并 flush";
}

TEST(ContainerBatchTest, DifferentComponentTypesEachGetTheirOwnCoalescedNotification)
{
    Container container;
    const Entity id = container.create();
    container.emplace<Position>(id, 0.f, 0.f);
    container.emplace<Rotation>(id, 0.f);

    int positionUpdates = 0;
    int rotationUpdates = 0;
    Connection posConn  = container.onUpdate<Position>(
        [&](Container&, Entity) { ++positionUpdates; });
    Connection rotConn = container.onUpdate<Rotation>(
        [&](Container&, Entity) { ++rotationUpdates; });

    {
        Container::BatchGuard batch(container);
        container.patch<Position>(id, [](Position& p) { p.x = 1.f; });
        container.patch<Position>(id, [](Position& p) { p.x = 2.f; });
        container.patch<Rotation>(id, [](Rotation& r) { r.angle = 90.f; });
    }

    EXPECT_EQ(positionUpdates, 1);
    EXPECT_EQ(rotationUpdates, 1);
}

TEST(ContainerBatchTest, MultipleEntitiesEachNotifiedOnceEvenWithInterleavedPatches)
{
    Container container;
    const Entity a = container.create();
    const Entity b = container.create();
    container.emplace<Position>(a, 0.f, 0.f);
    container.emplace<Position>(b, 0.f, 0.f);

    std::vector<Entity> notified;
    Connection conn = container.onUpdate<Position>(
        [&](Container&, Entity id) { notified.push_back(id); });

    {
        Container::BatchGuard batch(container);
        container.patch<Position>(a, [](Position& p) { p.x = 1.f; });
        container.patch<Position>(b, [](Position& p) { p.x = 1.f; });
        container.patch<Position>(a, [](Position& p) { p.x = 2.f; }); // a 再次变化，仍然只应通知一次
    }

    ASSERT_EQ(notified.size(), 2u);
    EXPECT_NE(std::find(notified.begin(), notified.end(), a), notified.end());
    EXPECT_NE(std::find(notified.begin(), notified.end(), b), notified.end());
}

TEST(ContainerBatchTest, NestedBatchesOnlyFlushWhenOutermostEnds)
{
    Container container;
    const Entity id = container.create();
    container.emplace<Position>(id, 0.f, 0.f);

    int updateCount = 0;
    Connection conn = container.onUpdate<Position>([&](Container&, Entity) { ++updateCount; });

    container.beginBatch();
    container.beginBatch();
    container.patch<Position>(id, [](Position& p) { p.x = 1.f; });
    container.endBatch();
    EXPECT_EQ(updateCount, 0) << "内层 endBatch() 不应该提前 flush";
    container.endBatch();
    EXPECT_EQ(updateCount, 1) << "只有最外层 endBatch() 才应该触发 flush";
}

TEST(ContainerBatchTest, UnmatchedEndBatchIsSafeNoop)
{
    Container container;
    container.endBatch(); // 没有对应的 beginBatch()，应当安全忽略
    EXPECT_FALSE(container.isBatching());
}

TEST(ContainerBatchTest, ConstructAndDestroyNotificationsAlsoCoalesce)
{
    Container container;
    const Entity a = container.create();
    const Entity b = container.create();

    int constructCount = 0;
    Connection conn = container.onConstruct<Position>([&](Container&, Entity) { ++constructCount; });

    {
        Container::BatchGuard batch(container);
        container.emplace<Position>(a, 0.f, 0.f);
        container.emplace<Position>(b, 0.f, 0.f);
    }

    EXPECT_EQ(constructCount, 2) << "不同实体各自只应该收到一次 construct 通知";
}

TEST(ContainerBatchTest, DisconnectDuringPendingBatchDoesNotCrashAndSuppressesNotification)
{
    // 回归测试：如果在批次尚未结束、已经有待冲洗的通知登记在 Container 上时，
    // 调用方提前 disconnect() 了这次订阅——Slot 可能在 disconnect() 内部就被
    // 同步销毁，稍后 endBatch() 触发的 flush 决不能悬空访问它。
    Container container;
    const Entity id = container.create();
    container.emplace<Position>(id, 0.f, 0.f);

    int updateCount = 0;
    Connection conn = container.onUpdate<Position>([&](Container&, Entity) { ++updateCount; });

    container.beginBatch();
    container.patch<Position>(id, [](Position& p) { p.x = 1.f; });
    conn.disconnect(); // 提前断开，此时 flush 还没有被调用过
    container.endBatch();

    EXPECT_EQ(updateCount, 0) << "断开之后不应该再收到任何通知（也不应该崩溃）";
}
