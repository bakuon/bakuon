#include <gtest/gtest.h>

#include <bakuon/core/command/Command.h>
#include <bakuon/core/command/CommandContext.h>
#include <bakuon/core/Entity.h>

using namespace bakuon::core;
using namespace bakuon::core::command;

namespace {

struct Fixture : ::testing::Test
{
    Registry registry;
    CommandRegistry commands{registry};
    ContextRegistry contexts{registry, commands};
};

} // namespace

TEST_F(Fixture, RegisterCommandIsIdempotentAndIndexed)
{
    const Entity a = commands.registerCommand("edit.delete", "Delete");
    const Entity b = commands.registerCommand("edit.delete", "Delete (renamed attempt)");

    EXPECT_EQ(a, b) << "重复注册应返回同一个 Entity，且不覆盖已有配置";
    EXPECT_EQ(registry.get<CommandName>(a).name, "Delete");
    EXPECT_EQ(commands.find("edit.delete"), a);
    EXPECT_EQ(commands.size(), 1u);
}

TEST_F(Fixture, UnregisterRemovesFromIndex)
{
    commands.registerCommand("edit.paste", "Paste");
    ASSERT_TRUE(commands.contains("edit.paste"));

    commands.unregisterCommand("edit.paste");
    EXPECT_FALSE(commands.contains("edit.paste"));
    EXPECT_TRUE(commands.find("edit.paste") == nullentity);
}

TEST_F(Fixture, NoActiveContextMeansNoAuthority)
{
    const Entity cmd = commands.registerCommand("view.zoom", "Zoom");
    contexts.registerContext("editor.image", "plugin.image", "");

    EXPECT_TRUE(contexts.findActiveContext("view.zoom") == nullentity)
        << "命令未被任何上下文登记时不应该有权威源";
    EXPECT_FALSE(registry.all_of<ActiveContext>(cmd))
        << "从未仲裁过的命令不应该被动挂上 ActiveContext 组件";
}

TEST_F(Fixture, PushContextActivatesBoundCommand)
{
    const Entity cmd = commands.registerCommand("edit.delete", "Delete");
    const Entity ctx = contexts.registerContext("editor.image.focused", "plugin.image", "");
    contexts.bindCommand(ctx, "edit.delete");

    int source = 0;
    contexts.pushContext(ctx, &source);

    EXPECT_TRUE(contexts.isActive(ctx));
    EXPECT_EQ(registry.get<ActiveContext>(cmd).context, ctx);
    EXPECT_EQ(contexts.findActiveContext("edit.delete"), ctx);

    contexts.popContext(ctx, &source);
    EXPECT_FALSE(contexts.isActive(ctx));
    EXPECT_TRUE(registry.get<ActiveContext>(cmd).context == nullentity)
        << "上下文失活后仲裁应重新运行，权威源回落为空";
}

TEST_F(Fixture, ForegroundBeatsBackgroundRegardlessOfActivationOrder)
{
    // 直接对应 tests/gui/CommandSystemTest.cpp 的
    // BackgroundTierDoesNotBeatForeground 用例，验证迁移后仲裁结果一致。
    const Entity cmd = commands.registerCommand("tier.cmd", "X");
    const Entity fg  = contexts.registerContext("tier.fg", "test", "");
    const Entity bg  = contexts.registerContext("tier.bg", "test", "");
    contexts.bindCommand(fg, "tier.cmd");
    contexts.bindCommand(bg, "tier.cmd");

    int fgSrc = 0;
    int bgSrc = 0;
    contexts.pushContext(fg, &fgSrc, ContextTier::Foreground);
    contexts.pushContext(bg, &bgSrc, ContextTier::Background);
    EXPECT_EQ(contexts.findActiveContext("tier.cmd"), fg)
        << "Background 即使更晚激活也不能压过 Foreground";
    EXPECT_EQ(registry.get<ActiveContext>(cmd).context, fg);

    contexts.popContext(fg, &fgSrc, ContextTier::Foreground);
    EXPECT_EQ(contexts.findActiveContext("tier.cmd"), bg)
        << "Foreground 退出后 Background 仍可作为权威源";
}

TEST_F(Fixture, HigherPrioritySameTierWins)
{
    const Entity cmd  = commands.registerCommand("cmd.x", "X");
    const Entity low  = contexts.registerContext("ctx.low", "test", "", /*priority=*/0);
    const Entity high = contexts.registerContext("ctx.high", "test", "", /*priority=*/10);
    contexts.bindCommand(low, "cmd.x");
    contexts.bindCommand(high, "cmd.x");

    int lowSrc = 0, highSrc = 0;
    contexts.pushContext(low, &lowSrc);
    contexts.pushContext(high, &highSrc);

    EXPECT_EQ(contexts.findActiveContext("cmd.x"), high);
    EXPECT_EQ(registry.get<ActiveContext>(cmd).context, high);
}

TEST_F(Fixture, ReleaseSourceClearsAllContextsHeldByThatSource)
{
    const Entity cmd  = commands.registerCommand("cmd.y", "Y");
    const Entity ctxA = contexts.registerContext("ctx.a", "test", "");
    const Entity ctxB = contexts.registerContext("ctx.b", "test", "");
    contexts.bindCommand(ctxA, "cmd.y");
    contexts.bindCommand(ctxB, "cmd.y");

    int source = 0;
    contexts.pushContext(ctxA, &source);
    contexts.pushContext(ctxB, &source);
    ASSERT_TRUE(contexts.isActive(ctxA));
    ASSERT_TRUE(contexts.isActive(ctxB));

    contexts.releaseSource(&source);

    EXPECT_FALSE(contexts.isActive(ctxA));
    EXPECT_FALSE(contexts.isActive(ctxB));
    EXPECT_TRUE(registry.get<ActiveContext>(cmd).context == nullentity);
}

TEST_F(Fixture, UnbindCommandRemovesItFromArbitration)
{
    const Entity cmd = commands.registerCommand("cmd.z", "Z");
    const Entity ctx = contexts.registerContext("ctx.z", "test", "test z context");
    contexts.bindCommand(ctx, "cmd.z");

    int source = 0;
    contexts.pushContext(ctx, &source);
    ASSERT_EQ(contexts.findActiveContext("cmd.z"), ctx);

    contexts.unbindCommand(ctx, "cmd.z");
    // 解绑后 ctx 即使仍然激活，也不再参与 cmd.z 的仲裁。
    EXPECT_TRUE(contexts.findActiveContext("cmd.z") == nullentity);
    EXPECT_TRUE(registry.get<ContextCommands>(ctx).commandIds.empty());
}
