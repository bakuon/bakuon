#include <gtest/gtest.h>

#include <span>
#include <string>

#include <bakuon/core/Cloner.h>
#include <bakuon/core/Entity.h>
#include <bakuon/core/Identifier.h>

namespace {

struct Position
{
    float x = 0.f;
    float y = 0.f;
};

struct TransformComponent
{
    float x{0};
    float y{0};
    float z{0};
};

struct HealthComponent
{
    int hp{0};
};

struct TagComponent
{
};

struct HierarchyComponent
{
    bakuon::core::StableId parent{};
};

struct LabelComponent
{
    std::string text;
};

struct LinkComponent
{
    bakuon::core::StableId targetId;
};

} // namespace

using namespace bakuon::core;

TEST(EntityCloner, ClonesComponentsAndMintsNewStableId)
{
    Registry registry;
    SnowflakeGenerator generator{11};
    Identifier ids{registry, &generator};

    Cloner cloner(registry, ids, registry, ids);
    cloner.addComponent<TransformComponent>();
    cloner.addComponent<HealthComponent>();
    cloner.addComponent<TagComponent>();

    const auto source = registry.create();
    registry.emplace<TransformComponent>(source, TransformComponent{1.5f, 2.5f, 3.5f});
    registry.emplace<HealthComponent>(source, HealthComponent{87});
    registry.emplace<TagComponent>(source);

    const auto source_id = ids.ensure(source);
    const auto result    = cloner.clone(source);

    EXPECT_NE(result.entity, source);
    EXPECT_NE(result.cloned, source_id);
    EXPECT_EQ(result.source, source_id);
    EXPECT_TRUE(result.cloned.isValid());
    EXPECT_EQ(ids.get(result.entity), result.cloned);
    EXPECT_EQ(ids.find(source_id), source);
    EXPECT_EQ(ids.find(result.cloned), result.entity);

    ASSERT_TRUE((registry.all_of<TransformComponent, HealthComponent, TagComponent>(result.entity)));
    const auto& xf = registry.get<TransformComponent>(result.entity);
    EXPECT_EQ(xf.x, 1.5f);
    EXPECT_EQ(xf.y, 2.5f);
    EXPECT_EQ(xf.z, 3.5f);
    EXPECT_EQ(registry.get<HealthComponent>(result.entity).hp, 87);

    EXPECT_EQ(registry.get<StableId>(source), source_id);
    EXPECT_NE(registry.get<StableId>(result.entity), source_id);
}

TEST(EntityCloner, BatchCloneRemapsParentStableIds)
{
    Registry registry;
    SnowflakeGenerator generator{12};
    Identifier ids{registry, &generator};

    Cloner cloner{registry, ids, registry, ids};
    cloner.addComponent<HierarchyComponent>();
    cloner.setRemapper<HierarchyComponent>(
        [](HierarchyComponent& hierarchy, const StableIdRemap& remap) {
            if (const auto it = remap.find(hierarchy.parent); it != remap.end()) {
                hierarchy.parent = it->second;
            }
        });

    const auto parent    = registry.create();
    const auto child     = registry.create();
    const auto parent_id = ids.ensure(parent);
    ids.ensure(child);
    registry.emplace<HierarchyComponent>(child, HierarchyComponent{parent_id});

    const Entity sources[] = {parent, child};
    const auto batch       = cloner.cloneBatch(std::span<const Entity>{sources});

    EXPECT_EQ(batch.clones.size(), 2u);
    EXPECT_EQ(batch.remap.size(), 2u);
    EXPECT_NE(batch.remap.at(parent_id), parent_id);

    const auto cloned_child    = batch.clones[1].entity;
    const auto remapped_parent = registry.get<HierarchyComponent>(cloned_child).parent;
    EXPECT_EQ(remapped_parent, batch.remap.at(parent_id));
    EXPECT_NE(remapped_parent, parent_id);
    EXPECT_EQ(registry.get<HierarchyComponent>(child).parent, parent_id);
}

TEST(BatchClonerTest, InBatchReferenceIsRemappedToTheClone)
{
    Registry registry;
    SnowflakeGenerator generator(1);
    Identifier ids(registry, &generator);

    const Entity a = registry.create();
    const Entity b = registry.create();
    registry.emplace<Position>(a, 1.f, 2.f);
    registry.emplace<Position>(b, 3.f, 4.f);

    const StableId idA = ids.ensure(a);
    const StableId idB = ids.ensure(b);
    registry.emplace<LinkComponent>(a, LinkComponent{idB}); // A 引用 B

    Cloner cloner(registry, ids, registry, ids);
    cloner.addComponent<Position>();
    cloner.addComponent<LinkComponent>();
    cloner.setRemapper<LinkComponent>([](LinkComponent& hierarchy, const StableIdRemap& remap) {
        if (const auto it = remap.find(hierarchy.targetId); it != remap.end()) {
            hierarchy.targetId = it->second;
        }
    });

    const StableId sourceIds[] = {idA, idB};
    const auto results         = cloner.cloneBatch(std::span<const StableId>{sourceIds});

    EXPECT_EQ(results.clones.size(), 2u);
    EXPECT_EQ(results.remap.size(), 2u);
    ASSERT_TRUE(results.remap.contains(idA));
    ASSERT_TRUE(results.remap.contains(idB));
    const StableId newIdA = results.remap.at(idA);
    const StableId newIdB = results.remap.at(idB);
    EXPECT_NE(newIdA, idA);
    EXPECT_NE(newIdB, idB);

    const Entity newA = ids.find(newIdA);
    const Entity newB = ids.find(newIdB);
    ASSERT_TRUE(registry.valid(newA));
    ASSERT_TRUE(registry.valid(newB));

    // 核心断言: 克隆出来的 A' 里的 LinkComponent 应该指向新克隆的 B'，
    // 而不是原始的 B。
    const LinkComponent* clonedLink = registry.try_get<LinkComponent>(newA);
    ASSERT_NE(clonedLink, nullptr);
    EXPECT_EQ(clonedLink->targetId, newIdB);
    EXPECT_NE(clonedLink->targetId, idB);

    // 数据字段(Position)应该被原样拷贝，不受 fixup 影响。
    const Position* clonedPos = registry.try_get<Position>(newA);
    ASSERT_NE(clonedPos, nullptr);
    EXPECT_FLOAT_EQ(clonedPos->x, 1.f);
    EXPECT_FLOAT_EQ(clonedPos->y, 2.f);

    // 原始实体不受影响。
    EXPECT_EQ(registry.try_get<LinkComponent>(a)->targetId, idB);
}

TEST(BatchClonerTest, OutOfBatchReferenceIsLeftUnchanged)
{
    Registry registry;
    SnowflakeGenerator generator(1);
    Identifier ids(registry, &generator);

    const Entity a     = registry.create(); // 将被克隆
    const Entity c     = registry.create(); // 不在本批次内
    const StableId idA = ids.ensure(a);
    const StableId idC = ids.ensure(c);
    registry.emplace<LinkComponent>(a, LinkComponent{idC});

    Cloner cloner(registry, ids, registry, ids);
    cloner.addComponent<LinkComponent>();
    const StableId sourceIds[] = {idA};
    const auto batch = cloner.cloneBatch(std::span<const StableId>{sourceIds}); // 只克隆 A，不含 C

    const Entity newA = ids.find(batch.remap.at(idA));
    ASSERT_TRUE(registry.valid(newA));
    EXPECT_EQ(registry.try_get<LinkComponent>(newA)->targetId, idC)
        << "批次外的引用应该原样保留，继续指向原始的共享对象 C";
}
