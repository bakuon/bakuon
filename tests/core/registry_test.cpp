#include <gtest/gtest.h>

#include <string>
#include <vector>

#include <bakuon/core/Registry.h>

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

TEST(RegistryTest, CreateDestroyAndValid)
{
    Registry registry;

    EXPECT_FALSE(registry.valid(Handle{})) << "默认构造的 Handle 永远无效";

    const Handle id = registry.create();
    EXPECT_TRUE(id.isValid());
    EXPECT_TRUE(registry.valid(id));

    registry.destroy(id);
    EXPECT_FALSE(registry.valid(id));

    // 对已经失效的 id 重复 destroy() 应该是安全的空操作，不应该崩溃。
    registry.destroy(id);
}

TEST(RegistryTest, EmplaceHasTryGetRemove)
{
    Registry registry;
    const Handle id = registry.create();

    EXPECT_FALSE(registry.has<Position>(id));
    EXPECT_EQ(registry.tryGet<Position>(id), nullptr);

    Position& pos = registry.emplace<Position>(id, 1.f, 2.f);
    EXPECT_EQ(pos.x, 1.f);
    EXPECT_TRUE(registry.has<Position>(id));

    Position* fetched = registry.tryGet<Position>(id);
    ASSERT_NE(fetched, nullptr);
    EXPECT_EQ(fetched->y, 2.f);
    fetched->y = 99.f; // tryGet 返回的是内部存储的引用，可以直接原地修改
    EXPECT_EQ(registry.tryGet<Position>(id)->y, 99.f);

    registry.remove<Position>(id);
    EXPECT_FALSE(registry.has<Position>(id));

    // 组件本就不存在时，remove() 应当是安全的空操作。
    registry.remove<Position>(id);
}

TEST(RegistryTest, EmplaceOrReplaceOverwritesWholeComponent)
{
    Registry registry;
    const Handle id = registry.create();

    registry.emplace<Name>(id, Name{"first"});
    registry.emplaceOrReplace<Name>(id, Name{"second"});

    ASSERT_TRUE(registry.has<Name>(id));
    EXPECT_EQ(registry.tryGet<Name>(id)->value, "second");
}

TEST(RegistryTest, TagComponentCarriesNoData)
{
    Registry registry;
    const Handle a = registry.create();
    const Handle b = registry.create();

    registry.emplace<Selected>(a);
    EXPECT_TRUE(registry.has<Selected>(a));
    EXPECT_FALSE(registry.has<Selected>(b));
}

TEST(RegistryTest, EachIteratesOnlyMatchingEntities)
{
    Registry registry;
    const Handle withBoth     = registry.create();
    const Handle onlyPosition = registry.create();

    registry.emplace<Position>(withBoth, 3.f, 4.f);
    registry.emplace<Selected>(withBoth);
    registry.emplace<Position>(onlyPosition, 5.f, 6.f);

    std::vector<Handle> visited;
    registry.each<Position, Selected>([&](Handle id, Position& pos) {
        // 注意：Selected 是空结构体标签组件，entt 不会为它传出引用形参，
        // 即使它作为筛选条件之一出现在 each<Position, Selected>() 里，
        // 见 b_registry.h::each() 的说明。
        visited.push_back(id);
        pos.x += 100.f; // each() 传出的是内部存储的引用，允许原地修改
    });

    ASSERT_EQ(visited.size(), 1u);
    EXPECT_EQ(visited.front(), withBoth);
    EXPECT_EQ(registry.tryGet<Position>(withBoth)->x, 103.f);
    EXPECT_EQ(registry.tryGet<Position>(onlyPosition)->x, 5.f) << "未匹配的实体不应被访问/修改";
}

TEST(RegistryTest, OnConstructFiresWithCorrectEntityAndValue)
{
    Registry registry;

    int callCount = 0;
    Handle observedId;
    Connection conn = registry.onConstruct<Position>([&](Registry& reg, Handle id) {
        ++callCount;
        observedId = id;
        // 回调触发时组件已经挂载完毕，可以安全读取。
        EXPECT_EQ(reg.tryGet<Position>(id)->x, 7.f);
    });

    const Handle id = registry.create();
    registry.emplace<Position>(id, 7.f, 8.f);

    EXPECT_EQ(callCount, 1);
    EXPECT_EQ(observedId, id);
}

TEST(RegistryTest, OnUpdateFiresOnPatchAndEmplaceOrReplace)
{
    Registry registry;
    const Handle id = registry.create();
    registry.emplace<Position>(id, 0.f, 0.f);

    int updateCount = 0;
    Connection conn = registry.onUpdate<Position>([&](Registry&, Handle) { ++updateCount; });

    registry.patch<Position>(id, [](Position& p) { p.x = 42.f; });
    EXPECT_EQ(updateCount, 1);
    EXPECT_EQ(registry.tryGet<Position>(id)->x, 42.f);

    // 对已存在的组件调用 emplaceOrReplace() 内部走的是 patch 路径，同样触发 onUpdate
    // （而不是 onConstruct）——见 b_registry.h / EnTT emplace_or_replace() 的实现说明。
    registry.emplaceOrReplace<Position>(id, Position{1.f, 2.f});
    EXPECT_EQ(updateCount, 2);
}

TEST(RegistryTest, OnDestroyFiresBeforeComponentIsGone)
{
    Registry registry;
    const Handle id = registry.create();
    registry.emplace<Position>(id, 9.f, 9.f);

    bool sawComponentStillPresent = false;
    Connection conn               = registry.onDestroy<Position>([&](Registry& reg, Handle e) {
        sawComponentStillPresent = reg.tryGet<Position>(e) != nullptr;
    });

    registry.remove<Position>(id);
    EXPECT_TRUE(sawComponentStillPresent) << "onDestroy 触发时组件应仍然存在，之后才被真正摘除";
    EXPECT_FALSE(registry.has<Position>(id));
}

TEST(RegistryTest, ConnectionDisconnectsOnScopeExit)
{
    Registry registry;
    int callCount = 0;
    {
        Connection conn = registry.onConstruct<Position>([&](Registry&, Handle) { ++callCount; });
        EXPECT_TRUE(conn.isConnected());
        registry.emplace<Position>(registry.create(), 0.f, 0.f);
        EXPECT_EQ(callCount, 1);
    } // conn 离开作用域析构，自动断开

    registry.emplace<Position>(registry.create(), 0.f, 0.f);
    EXPECT_EQ(callCount, 1) << "Connection 析构后不应该再收到通知";
}

TEST(RegistryTest, ConnectionExplicitDisconnectIsIdempotent)
{
    Registry registry;
    int callCount   = 0;
    Connection conn = registry.onConstruct<Position>([&](Registry&, Handle) { ++callCount; });

    conn.disconnect();
    EXPECT_FALSE(conn.isConnected());
    conn.disconnect(); // 重复调用应当安全

    registry.emplace<Position>(registry.create(), 0.f, 0.f);
    EXPECT_EQ(callCount, 0);
}

TEST(RegistryTest, ConnectionDismissKeepsSubscriptionAliveBeyondGuardLifetime)
{
    Registry registry;
    int callCount = 0;
    {
        Connection conn = registry.onConstruct<Position>([&](Registry&, Handle) { ++callCount; });
        conn.dismiss(); // 放弃自动断开：guard 析构后订阅仍然有效
    }

    registry.emplace<Position>(registry.create(), 0.f, 0.f);
    EXPECT_EQ(callCount, 1) << "dismiss() 之后订阅应当继续存活，不随 guard 一起断开";
}

TEST(RegistryTest, MultipleEntitiesIndependentComponents)
{
    Registry registry;
    const Handle a = registry.create();
    const Handle b = registry.create();
    ASSERT_NE(a, b);

    registry.emplace<Name>(a, Name{"a"});
    registry.emplace<Name>(b, Name{"b"});

    EXPECT_EQ(registry.tryGet<Name>(a)->value, "a");
    EXPECT_EQ(registry.tryGet<Name>(b)->value, "b");

    registry.destroy(a);
    EXPECT_FALSE(registry.valid(a));
    EXPECT_TRUE(registry.valid(b));
    EXPECT_EQ(registry.tryGet<Name>(b)->value, "b") << "销毁一个实体不应该影响其它实体";
}
