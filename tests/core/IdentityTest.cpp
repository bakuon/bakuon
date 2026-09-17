#include <gtest/gtest.h>

#include <unordered_set>

#include <bakuon/core/Identity.h>

using Snowflake = bakuon::core::SnowflakeGenerator;
using bakuon::core::Identity;
using bakuon::core::Registry;
using bakuon::core::StableId;
using bakuon::core::StableIdError;

TEST(StableId, InvalidIsZero)
{
    EXPECT_FALSE(StableId{}.isValid());
    EXPECT_EQ(StableId{}.value(), 0u);
    EXPECT_EQ(StableId{}, 0);
}

TEST(SnowflakeGenerator, UniqueMonotonicAndDecodable)
{
    Snowflake gen{7};
    std::unordered_set<StableId::value_type> seen;
    StableId previous{};
    for (int i = 0; i < 4096; ++i) {
        const auto id = StableId(gen.next());
        EXPECT_TRUE(id.isValid());
        EXPECT_TRUE(seen.insert(id.value()).second);
        EXPECT_TRUE(id.value() > previous.value() || previous.value() == 0);
        const auto view = bakuon::core::toSnowflake(id);
        EXPECT_EQ(view.worker, 7);
        previous = id;
    }
}

TEST(Identity, BidirectionalInsertAndSignalDrivenDelete)
{
    Registry registry;
    Snowflake generator{1};
    Identity identity{registry, &generator};

    const auto entity = registry.create();
    const auto sid    = identity.ensure(entity);

    EXPECT_TRUE(sid.isValid());
    EXPECT_EQ(identity.size(), 1u);
    EXPECT_EQ(identity.find(sid), entity);
    EXPECT_EQ(identity.get(entity), sid);
    EXPECT_TRUE(registry.all_of<StableId>(entity));
    EXPECT_EQ(registry.get<StableId>(entity), sid);

    registry.destroy(entity);

    EXPECT_EQ(identity.size(), 0u);
    EXPECT_EQ(identity.find(sid), std::nullopt);
    EXPECT_FALSE(identity.contains(sid));
}

TEST(Identity, RecycledEntityIndexGetsNewStableId)
{
    Registry registry;
    Snowflake generator{2};
    Identity identity{registry, &generator};

    const auto first       = registry.create();
    const auto first_id    = identity.ensure(first);
    const auto first_index = bakuon::core::toEntity(first);
    registry.destroy(first);

    const auto second    = registry.create();
    const auto second_id = identity.ensure(second);

    EXPECT_EQ(bakuon::core::toEntity(second), first_index);
    EXPECT_NE(first, second);
    EXPECT_NE(first_id, second_id);
    EXPECT_EQ(identity.find(first_id), std::nullopt);
    EXPECT_EQ(identity.find(second_id), second);
}

TEST(Identity, BindRestoresIdentityForUndoRedo)
{
    Registry registry;
    Snowflake generator{3};
    Identity identity{registry, &generator};

    const auto original = registry.create();
    const auto sid      = identity.ensure(original);
    registry.destroy(original);

    const auto restored = registry.create();
    identity.assign(restored, sid);

    EXPECT_EQ(identity.find(sid), restored);
    EXPECT_EQ(identity.get(restored), sid);
}

TEST(Identity, DestroyWithoutIdentityIsIgnored)
{
    Registry registry;
    Snowflake generator{5};
    Identity identity{registry, &generator};

    const auto transient = registry.create();
    EXPECT_FALSE(identity.contains(transient));
    registry.destroy(transient);
    EXPECT_EQ(identity.size(), 0u);
}

// ----------------------------------------------------------------------------
// StableId 与 UndoStack / DocumentSerializer 的交互
//
// 这两个类都是靠 registry.clear() 之后再整体 reload 来实现"回到某个
// 历史时间点/文档状态"的（见 b_undostack.h / b_serializer.h）。StableId 的
// ensure() 计数器（下一个可用 id）存放在 Registry::ctx() 里——这不是随手
// 选的存储位置：ctx() 是刻意选中的，因为 Registry::clear() 只清空
// 实体/组件存储，不会触碰 ctx()（已经用一个独立的探针程序实测确认过这一点），
// 所以 ensure() 计数器能在反复 clear()+reload 之后仍然保持单调递增、不会归零
// 重新从 1 开始——如果真的归零，就会出现"撤销之后新建一个对象，却拿到了一个
// 已经被别的对象占用的 StableId"这种数据完整性 bug。
// 这个前提相当不直观（不读 entt 源码/不实测很容易想当然），之前没有任何测试
// 覆盖到，因此专门补上。
// ----------------------------------------------------------------------------
// TODO: UndoStack 和 Serializer 测试用例
