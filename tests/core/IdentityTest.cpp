#include <gtest/gtest.h>

#include <bakuon/core/Identity.h>
#include <bakuon/core/Registry.h>
#include <bakuon/core/Serializer.h>
#include <bakuon/core/UndoStack.h>

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

// ----------------------------------------------------------------------------
// StableId 与 UndoStack / DocumentSerializer 的交互
//
// 这两个类都是靠 registry.native().clear() 之后再整体 reload 来实现"回到某个
// 历史时间点/文档状态"的（见 b_undostack.h / b_serializer.h）。StableId 的
// mint() 计数器（下一个可用 id）存放在 entt::registry::ctx() 里——这不是随手
// 选的存储位置：ctx() 是刻意选中的，因为 entt::registry::clear() 只清空
// 实体/组件存储，不会触碰 ctx()（已经用一个独立的探针程序实测确认过这一点），
// 所以 mint() 计数器能在反复 clear()+reload 之后仍然保持单调递增、不会归零
// 重新从 1 开始——如果真的归零，就会出现"撤销之后新建一个对象，却拿到了一个
// 已经被别的对象占用的 StableId"这种数据完整性 bug。
// 这个前提相当不直观（不读 entt 源码/不实测很容易想当然），之前没有任何测试
// 覆盖到，因此专门补上。
// ----------------------------------------------------------------------------

namespace bakuon::core {
void to_json(nlohmann::json& j, const StableId& id)
{
    j = {{"value", id.value}};
}
void from_json(const nlohmann::json& j, StableId& id)
{
    j.at("value").get_to(id.value);
}
} // namespace bakuon::core
BAKUON_DECLARE_COMPONENT_NAME(StableId, "StableId")

TEST(IdentityTest, SurvivesUndoRedoCycleWithSameValue)
{
    Registry registry;
    const Handle a  = registry.create();
    const StableId idA = identity::ensure(registry, a);

    UndoStack<StableId> undo(registry);

    const Handle b  = registry.create();
    const StableId idB = identity::ensure(registry, b);
    undo.snapshot();

    ASSERT_TRUE(undo.undo());
    // 撤销到"b 还不存在"的那一刻：a 的 StableId 应该原样还在，b 应该彻底消失。
    EXPECT_EQ(identity::find(registry, idA), a);
    EXPECT_FALSE(identity::find(registry, idB).isValid());

    ASSERT_TRUE(undo.redo());
    EXPECT_EQ(identity::find(registry, idA), a);
    EXPECT_EQ(identity::find(registry, idB), b);
}

TEST(IdentityTest, MintCounterDoesNotResetAfterUndoRedoCycle)
{
    // 核心断言：撤销/重做（内部会 clear() 整个 registry 再 reload）之后，
    // 再 mint 一个新的 StableId，绝不能和已经存在的 id 撞车。
    Registry registry;
    const Handle a     = registry.create();
    const StableId idA = identity::ensure(registry, a);

    UndoStack<StableId> undo(registry);
    ASSERT_FALSE(undo.canUndo()) << "还没有任何历史可以撤销";

    // 走一次撤销/重做循环（哪怕内容没变化，也会触发一次 clear()+reload）。
    undo.snapshot();
    ASSERT_TRUE(undo.undo());
    ASSERT_TRUE(undo.redo());

    const Handle c     = registry.create();
    const StableId idC = identity::ensure(registry, c);

    EXPECT_NE(idC, idA) << "clear()+reload 循环之后 mint() 计数器不应该归零重来，"
                          "否则新对象会和撤销栈里仍然存活的旧对象撞 StableId";
    EXPECT_EQ(identity::find(registry, idA), a);
    EXPECT_EQ(identity::find(registry, idC), c);
}

TEST(IdentityTest, ColdLoadIntoFreshRegistryRequiresExplicitSync)
{
    // 这条测试记录了 identity::sync() 存在的直接原因：一个从未被任何
    // identity:: 函数碰过的全新 Registry，钩子根本没有被安装过——
    // DocumentSerializer::load() 整体载入的 StableId 组件因此"悄悄"落地，
    // 没有任何机制知道它们的存在，find() 会一直查无此人，直到显式 sync()。
    Registry source;
    const Handle a      = source.create();
    const StableId idA  = identity::ensure(source, a);
    DocumentSerializer<StableId> saver(source);
    const nlohmann::json doc = saver.save();

    Registry destination;
    DocumentSerializer<StableId> loader(destination);
    ASSERT_TRUE(loader.load(doc).success());

    // 组件本身确实在——只是索引还不知道。
    ASSERT_TRUE(destination.has<StableId>(a));
    EXPECT_FALSE(identity::find(destination, idA).isValid())
        << "sync() 之前，索引应该还没有捕捉到 load() 落地的 StableId";

    identity::sync(destination);
    EXPECT_EQ(identity::find(destination, idA), a) << "sync() 之后应该能正确解回 Handle";
}

TEST(IdentityTest, SurvivesDocumentSaveAndLoadIntoADifferentRegistry)
{
    Registry source;
    const Handle a = source.create();
    const StableId idA = identity::ensure(source, a);

    DocumentSerializer<StableId> serializer(source);
    const nlohmann::json doc = serializer.save();

    Registry destination;
    DocumentSerializer<StableId> loader(destination);
    ASSERT_TRUE(loader.load(doc).success());
    identity::sync(destination); // 见 ColdLoadIntoFreshRegistryRequiresExplicitSync：
                                  // destination 此前从未调用过任何 identity:: 函数，
                                  // 必须显式 sync() 一次才能让索引追上刚载入的内容。

    EXPECT_EQ(identity::find(destination, idA), a);
}

TEST(IdentityTest, MintCounterDoesNotCollideAfterDocumentLoad)
{
    Registry registry;
    const Handle a     = registry.create();
    const StableId idA = identity::ensure(registry, a);

    DocumentSerializer<StableId> serializer(registry);
    const nlohmann::json doc = serializer.save();
    ASSERT_TRUE(serializer.load(doc).success()); // 原地 clear()+reload 一次，模拟"重新打开同一份文档"

    const Handle b     = registry.create();
    const StableId idB = identity::ensure(registry, b);

    EXPECT_NE(idB, idA) << "load() 之后 mint() 计数器不应该归零重来";
    EXPECT_EQ(identity::find(registry, idA), a);
    EXPECT_EQ(identity::find(registry, idB), b);
}
