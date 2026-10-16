#include <gtest/gtest.h>

// 注：下面的 ATraits/BTraits 是测试内自建的最小种类；真实种类见
// source/gui/b_contextsystem.h、b_commandsystem.h。
#include "core/b_ref.h"

#include <bakuon/core/Handle.h>

using namespace bakuon::core;

struct AHandleTag;
struct BHandleTag;
using AHandle = BasicHandle<AHandleTag>;
using BHandle = BasicHandle<BHandleTag>;
struct CompA
{
    int x = 0;
};
struct CompB
{
    int y = 0;
};
struct ATraits
{
    using tag      = AHandleTag;
    using required = entt::type_list<CompA>;
};
struct BTraits
{
    using tag      = BHandleTag;
    using required = entt::type_list<CompB>;
};

template<typename Reg>
class BasicARef : public RefBase<ATraits, Reg>
{
    using Base = RefBase<ATraits, Reg>;

public:
    using Base::Base;
    BasicARef() = default;
    int x() const { return this->template get<CompA>().x; }
};
template<typename Reg>
class BasicBRef : public RefBase<BTraits, Reg>
{
    using Base = RefBase<BTraits, Reg>;

public:
    using Base::Base;
    BasicBRef() = default;
};
using ARef      = BasicARef<Registry>;
using ConstARef = BasicARef<const Registry>;
using BRef      = BasicBRef<Registry>;

struct Sys : RefResolver
{
    explicit Sys(Registry& r)
        : RefResolver(r)
    {
    }
    int readX(AHandle h) const
    {
        auto a = resolve<ConstARef>(h);
        return a ? a.x() : -1;
    }
    bool bump(AHandle h)
    {
        auto a = resolve<ARef>(h);
        if (!a)
            return false;
        a.patch<CompA>([](CompA& c) { ++c.x; });
        return true;
    }
};

TEST(RefTest, ResolveChecksValidityAndRequiredComponents)
{
    Registry reg;
    Sys sys(reg);
    const Entity e = reg.create();
    reg.emplace<CompA>(e, CompA{7});
    const AHandle h = handleOf<AHandle>(e);
    EXPECT_EQ(sys.readX(h), 7);
    EXPECT_TRUE(sys.bump(h));
    EXPECT_EQ(sys.readX(h), 8);
    EXPECT_EQ(sys.readX(AHandle{}), -1) << "无效句柄";
}

TEST(RefTest, WrongKindHandleIsRejectedByRequiredComponents)
{
    Registry reg;
    const Entity b = reg.create();
    reg.emplace<CompB>(b);
    // 把"B 种类实体"的原始值硬塞进 A 句柄（版本号碰撞/误用场景）：
    AHandle forged{handleOf<BHandle>(b).native()};
    EXPECT_FALSE(resolve<ARef>(reg, forged)) << "缺少 CompA → 不是 A 种类";
    EXPECT_TRUE(resolve<BRef>(reg, handleOf<BHandle>(b)));
}

TEST(RefTest, StaleHandleAfterDestroyIsInvalidEvenIfIndexRecycled)
{
    Registry reg;
    const Entity e1 = reg.create();
    reg.emplace<CompA>(e1);
    const AHandle old = handleOf<AHandle>(e1);
    reg.destroy(e1);
    const Entity e2 = reg.create();
    reg.emplace<CompA>(e2); // 回收同一个下标，版本号不同
    ASSERT_EQ(entt::to_entity(e1), entt::to_entity(e2));
    EXPECT_FALSE(resolve<ARef>(reg, old));
    EXPECT_TRUE(resolve<ARef>(reg, handleOf<AHandle>(e2)));
}

TEST(RefTest, AliveIsReevaluatedAndOpaqueRoundTrips)
{
    Registry reg;
    const Entity e = reg.create();
    reg.emplace<CompA>(e);
    ARef ref = resolve<ARef>(reg, e);
    ASSERT_TRUE(ref);
    EXPECT_EQ(ref.opaque(), handleOf<AHandle>(e));
    reg.remove<CompA>(e);
    EXPECT_FALSE(ref) << "持有期间组件被摘掉，alive() 重新判定";
    EXPECT_FALSE(ARef{}.opaque().isNull());
    EXPECT_FALSE(static_cast<bool>(ARef{}));
}

TEST(RefTest, WrapDoesNotValidateButAliveDoes)
{
    Registry reg;
    const Entity e = reg.create();
    auto ref       = wrap<ARef>(reg, e);
    EXPECT_FALSE(ref);
    reg.emplace<CompA>(e);
    EXPECT_TRUE(ref);
}
