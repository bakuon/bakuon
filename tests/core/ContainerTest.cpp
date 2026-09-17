#include <gtest/gtest.h>

#include <string>
#include <vector>

#include <bakuon/core/Container.h>

using namespace bakuon::core;

namespace {

struct Position
{
    float x = 0.f;
    float y = 0.f;
};

/// 空结构体："标签组件"（tag component），只表达"该实体是否具有某种状态"，
/// 不携带任何数据——ECS 里非常常见的模式，比如 Selected/Locked/Dirty。
struct Selected
{
};

struct Name
{
    std::string value;
};

} // namespace

TEST(ContainerTest, CreateDestroyAndValid)
{
    Container container;

    EXPECT_FALSE(container.valid(Entity{})) << "默认构造的 Entity 永远无效";

    const Entity id = container.create();
    EXPECT_TRUE(container.valid(id));
    EXPECT_TRUE(container.valid(id));

    container.destroy(id);
    EXPECT_FALSE(container.valid(id));

    // 对已经失效的 id 重复 destroy() 应该是安全的空操作，不应该崩溃。
    container.destroy(id);
}

TEST(ContainerTest, EmplaceHasTryGetRemove)
{
    Container container;
    const Entity id = container.create();

    EXPECT_FALSE(container.has<Position>(id));
    EXPECT_EQ(container.tryGet<Position>(id), nullptr);

    Position& pos = container.emplace<Position>(id, 1.f, 2.f);
    EXPECT_EQ(pos.x, 1.f);
    EXPECT_TRUE(container.has<Position>(id));

    Position* fetched = container.tryGet<Position>(id);
    ASSERT_NE(fetched, nullptr);
    EXPECT_EQ(fetched->y, 2.f);
    fetched->y = 99.f; // tryGet 返回的是内部存储的引用，可以直接原地修改
    EXPECT_EQ(container.tryGet<Position>(id)->y, 99.f);

    container.remove<Position>(id);
    EXPECT_FALSE(container.has<Position>(id));

    // 组件本就不存在时，remove() 应当是安全的空操作。
    container.remove<Position>(id);
}

TEST(ContainerTest, EmplaceOrReplaceOverwritesWholeComponent)
{
    Container container;
    const Entity id = container.create();

    container.emplace<Name>(id, Name{"first"});
    container.emplaceOrReplace<Name>(id, Name{"second"});

    ASSERT_TRUE(container.has<Name>(id));
    EXPECT_EQ(container.tryGet<Name>(id)->value, "second");
}

TEST(ContainerTest, TagComponentCarriesNoData)
{
    Container container;
    const Entity a = container.create();
    const Entity b = container.create();

    container.emplace<Selected>(a);
    EXPECT_TRUE(container.has<Selected>(a));
    EXPECT_FALSE(container.has<Selected>(b));
}

TEST(ContainerTest, EachIteratesOnlyMatchingEntities)
{
    Container container;
    const Entity withBoth     = container.create();
    const Entity onlyPosition = container.create();

    container.emplace<Position>(withBoth, 3.f, 4.f);
    container.emplace<Selected>(withBoth);
    container.emplace<Position>(onlyPosition, 5.f, 6.f);

    std::vector<Entity> visited;
    container.each<Position, Selected>([&](Entity id, Position& pos) {
        // 注意：Selected 是空结构体标签组件，entt 不会为它传出引用形参，
        // 即使它作为筛选条件之一出现在 each<Position, Selected>() 里，
        // 见 b_registry.h::each() 的说明。
        visited.push_back(id);
        pos.x += 100.f; // each() 传出的是内部存储的引用，允许原地修改
    });

    ASSERT_EQ(visited.size(), 1u);
    EXPECT_EQ(visited.front(), withBoth);
    EXPECT_EQ(container.tryGet<Position>(withBoth)->x, 103.f);
    EXPECT_EQ(container.tryGet<Position>(onlyPosition)->x, 5.f) << "未匹配的实体不应被访问/修改";
}

TEST(ContainerTest, OnConstructFiresWithCorrectEntityAndValue)
{
    Container container;

    int callCount = 0;
    Entity observedId;
    Connection conn = container.onConstruct<Position>([&](Container& reg, Entity id) {
        ++callCount;
        observedId = id;
        // 回调触发时组件已经挂载完毕，可以安全读取。
        EXPECT_EQ(reg.tryGet<Position>(id)->x, 7.f);
    });

    const Entity id = container.create();
    container.emplace<Position>(id, 7.f, 8.f);

    EXPECT_EQ(callCount, 1);
    EXPECT_EQ(observedId, id);
}

TEST(ContainerTest, OnUpdateFiresOnPatchAndEmplaceOrReplace)
{
    Container container;
    const Entity id = container.create();
    container.emplace<Position>(id, 0.f, 0.f);

    int updateCount = 0;
    Connection conn = container.onUpdate<Position>([&](Container&, Entity) { ++updateCount; });

    container.patch<Position>(id, [](Position& p) { p.x = 42.f; });
    EXPECT_EQ(updateCount, 1);
    EXPECT_EQ(container.tryGet<Position>(id)->x, 42.f);

    // 对已存在的组件调用 emplaceOrReplace() 内部走的是 patch 路径，同样触发 onUpdate
    // （而不是 onConstruct）——见 b_registry.h / EnTT emplace_or_replace() 的实现说明。
    container.emplaceOrReplace<Position>(id, Position{1.f, 2.f});
    EXPECT_EQ(updateCount, 2);
}

TEST(ContainerTest, OnDestroyFiresBeforeComponentIsGone)
{
    Container container;
    const Entity id = container.create();
    container.emplace<Position>(id, 9.f, 9.f);

    bool sawComponentStillPresent = false;
    Connection conn               = container.onDestroy<Position>([&](Container& reg, Entity e) {
        sawComponentStillPresent = reg.tryGet<Position>(e) != nullptr;
    });

    container.remove<Position>(id);
    EXPECT_TRUE(sawComponentStillPresent) << "onDestroy 触发时组件应仍然存在，之后才被真正摘除";
    EXPECT_FALSE(container.has<Position>(id));
}

TEST(ContainerTest, ConnectionDisconnectsOnScopeExit)
{
    Container container;
    int callCount = 0;
    {
        Connection conn = container.onConstruct<Position>([&](Container&, Entity) { ++callCount; });
        EXPECT_TRUE(conn.isConnected());
        container.emplace<Position>(container.create(), 0.f, 0.f);
        EXPECT_EQ(callCount, 1);
    } // conn 离开作用域析构，自动断开

    container.emplace<Position>(container.create(), 0.f, 0.f);
    EXPECT_EQ(callCount, 1) << "Connection 析构后不应该再收到通知";
}

TEST(ContainerTest, ConnectionExplicitDisconnectIsIdempotent)
{
    Container container;
    int callCount   = 0;
    Connection conn = container.onConstruct<Position>([&](Container&, Entity) { ++callCount; });

    conn.disconnect();
    EXPECT_FALSE(conn.isConnected());
    conn.disconnect(); // 重复调用应当安全

    container.emplace<Position>(container.create(), 0.f, 0.f);
    EXPECT_EQ(callCount, 0);
}

TEST(ContainerTest, ConnectionDismissKeepsSubscriptionAliveBeyondGuardLifetime)
{
    Container container;
    int callCount = 0;
    {
        Connection conn = container.onConstruct<Position>([&](Container&, Entity) { ++callCount; });
        conn.dismiss(); // 放弃自动断开：guard 析构后订阅仍然有效
    }

    container.emplace<Position>(container.create(), 0.f, 0.f);
    EXPECT_EQ(callCount, 1) << "dismiss() 之后订阅应当继续存活，不随 guard 一起断开";
}

TEST(ContainerTest, MultipleEntitiesIndependentComponents)
{
    Container container;
    const Entity a = container.create();
    const Entity b = container.create();
    ASSERT_NE(a, b);

    container.emplace<Name>(a, Name{"a"});
    container.emplace<Name>(b, Name{"b"});

    EXPECT_EQ(container.tryGet<Name>(a)->value, "a");
    EXPECT_EQ(container.tryGet<Name>(b)->value, "b");

    container.destroy(a);
    EXPECT_FALSE(container.valid(a));
    EXPECT_TRUE(container.valid(b));
    EXPECT_EQ(container.tryGet<Name>(b)->value, "b") << "销毁一个实体不应该影响其它实体";
}
