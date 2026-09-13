#include <gtest/gtest.h>

#include <bakuon/core/Identity.h>
#include <bakuon/core/Registry.h>

using namespace bakuon::core;

TEST(IdentityTest, EnsureIsStableAcrossCalls)
{
    Registry registry;
    const Handle a = registry.create();

    const StableId first  = identity::ensure(registry, a);
    const StableId second = identity::ensure(registry, a);

    EXPECT_TRUE(first.isValid());
    EXPECT_EQ(first, second);
    EXPECT_EQ(identity::get(registry, a), first);
    EXPECT_EQ(identity::find(registry, first), a);
}

TEST(IdentityTest, DifferentEntitiesGetDifferentIds)
{
    Registry registry;
    const Handle a = registry.create();
    const Handle b = registry.create();

    const StableId idA = identity::ensure(registry, a);
    const StableId idB = identity::ensure(registry, b);

    EXPECT_NE(idA, idB);
    EXPECT_EQ(identity::find(registry, idA), a);
    EXPECT_EQ(identity::find(registry, idB), b);
}

TEST(IdentityTest, DestroyRemovesIndexAndDoesNotAliasRecycledHandle)
{
    Registry registry;
    const Handle original = registry.create();
    const StableId id     = identity::ensure(registry, original);

    registry.destroy(original);
    EXPECT_FALSE(identity::find(registry, id).isValid())
        << "实体销毁后，旧 StableId 不应再解出任何 Handle";
    EXPECT_FALSE(registry.valid(original));

    // entt 会回收 entity id。新实体即使拿到同一个底层值，也必须是新的 StableId。
    const Handle recycled = registry.create();
    const StableId id2    = identity::ensure(registry, recycled);
    EXPECT_NE(id, id2);
    EXPECT_EQ(identity::find(registry, id2), recycled);
    EXPECT_FALSE(identity::find(registry, id).isValid());
}

TEST(IdentityTest, FindOnUnknownIdIsInvalid)
{
    Registry registry;
    EXPECT_FALSE(identity::find(registry, StableId{42}).isValid());
    EXPECT_FALSE(identity::ensure(registry, Handle{}).isValid());
    EXPECT_FALSE(identity::get(registry, Handle{}).isValid());
}

TEST(IdentityTest, ConstFindAfterEnsure)
{
    Registry registry;
    const Handle a     = registry.create();
    const StableId id  = identity::ensure(registry, a);
    const Registry& cr = registry;
    EXPECT_EQ(identity::find(cr, id), a);
    EXPECT_EQ(identity::get(cr, a), id);
}
