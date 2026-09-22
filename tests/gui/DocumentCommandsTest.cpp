#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QSignalSpy>

#include <bakuon/core/Components.h>
#include <bakuon/core/Hierarchy.h>

#include "gui/b_commandsystem.h"
#include "gui/b_documentcommands.h"
#include "gui/b_documentsession.h"

using namespace bakuon;
using bakuon::core::Handle;
namespace h = bakuon::core::hierarchy;
using bakuon::core::components::Locked;
using bakuon::core::components::Name;

namespace {

Handle makeNamed(core::Registry& reg, const std::string& name)
{
    const Handle e = reg.create();
    reg.emplace<Name>(e, Name{name});
    return e;
}

} // namespace

TEST(DocumentCommandsTest, DeleteDestroysTopLevelSelection)
{
    gui::DocumentSession session;
    gui::DocumentCommands cmds(session);
    auto& reg = session.registry();

    const Handle root = makeNamed(reg, "R");
    const Handle a    = makeNamed(reg, "A");
    const Handle a1   = makeNamed(reg, "A1");
    ASSERT_TRUE(h::append(reg, a, root));
    ASSERT_TRUE(h::append(reg, a1, a));

    session.selection().exclusive(a);
    cmds.executeDelete();

    EXPECT_FALSE(reg.valid(a));
    EXPECT_FALSE(reg.valid(a1));
    EXPECT_TRUE(reg.valid(root));
    EXPECT_TRUE(session.selection().empty());
}

TEST(DocumentCommandsTest, DeleteSkipsLocked)
{
    gui::DocumentSession session;
    gui::DocumentCommands cmds(session);
    auto& reg = session.registry();

    const Handle a = makeNamed(reg, "A");
    reg.emplace<Locked>(a, Locked{true});
    // ensure hierarchy root
    reg.getOrEmplace<h::Hierarchy>(a);

    session.selection().exclusive(a);
    cmds.executeDelete();
    EXPECT_TRUE(reg.valid(a)) << "锁定实体不应被删除";
}

TEST(DocumentCommandsTest, DeleteParentAndChildOnlyDestroysParentOnce)
{
    gui::DocumentSession session;
    gui::DocumentCommands cmds(session);
    auto& reg = session.registry();

    const Handle parent = makeNamed(reg, "P");
    const Handle child  = makeNamed(reg, "C");
    ASSERT_TRUE(h::append(reg, child, parent));

    session.selection().set({parent, child});
    cmds.executeDelete();

    EXPECT_FALSE(reg.valid(parent));
    EXPECT_FALSE(reg.valid(child));
}

TEST(DocumentCommandsTest, DuplicateCreatesSiblingWithCopiedName)
{
    gui::DocumentSession session;
    gui::DocumentCommands cmds(session);
    auto& reg = session.registry();

    const Handle parent = makeNamed(reg, "P");
    const Handle a      = makeNamed(reg, "A");
    ASSERT_TRUE(h::append(reg, a, parent));

    session.selection().exclusive(a);
    cmds.executeDuplicate();

    EXPECT_EQ(h::childCount(reg, parent), 2u);
    EXPECT_EQ(session.selection().count(), 1u);
    const Handle dup = session.selection().primary();
    ASSERT_TRUE(dup.isValid());
    EXPECT_NE(dup, a);
    ASSERT_NE(reg.tryGet<Name>(dup), nullptr);
    EXPECT_EQ(reg.tryGet<Name>(dup)->value, "A Copy");
    EXPECT_EQ(h::parent(reg, dup), parent);
}

TEST(DocumentCommandsTest, RenameApplyAndSignal)
{
    gui::DocumentSession session;
    gui::DocumentCommands cmds(session);
    auto& reg = session.registry();

    const Handle a = makeNamed(reg, "Old");
    reg.getOrEmplace<h::Hierarchy>(a);
    session.selection().exclusive(a);

    QSignalSpy spy(&cmds, &gui::DocumentCommands::renameRequested);
    cmds.requestRename();
    ASSERT_EQ(spy.count(), 1);
    EXPECT_EQ(spy.takeFirst().at(0).value<Handle>(), a);

    EXPECT_TRUE(cmds.applyRename(a, QStringLiteral("New")));
    EXPECT_EQ(reg.tryGet<Name>(a)->value, "New");
}

TEST(DocumentCommandsTest, ActionsEnabledOnlyWhenSelectionAllows)
{
    gui::DocumentSession session;
    gui::DocumentCommands cmds(session);

    EXPECT_FALSE(cmds.deleteAction()->isEnabled());
    EXPECT_FALSE(cmds.renameAction()->isEnabled());
    EXPECT_FALSE(cmds.duplicateAction()->isEnabled());

    const Handle a = makeNamed(session.registry(), "A");
    session.registry().getOrEmplace<h::Hierarchy>(a);
    session.selection().exclusive(a);

    // selection push 上下文后 action enabled 由 updateActionEnabled 刷新
    EXPECT_TRUE(cmds.deleteAction()->isEnabled());
    EXPECT_TRUE(cmds.renameAction()->isEnabled());
    EXPECT_TRUE(cmds.duplicateAction()->isEnabled());

    session.selection().clear();
    EXPECT_FALSE(cmds.deleteAction()->isEnabled());
}

TEST(DocumentCommandsTest, ProxyTriggersRealActionWhenContextActive)
{
    gui::DocumentSession session;
    gui::DocumentCommands cmds(session);
    auto& reg = session.registry();

    const Handle a = makeNamed(reg, "A");
    reg.getOrEmplace<h::Hierarchy>(a);
    session.selection().exclusive(a);
    ASSERT_TRUE(session.isSelectionContextActive());

    auto* cmd = gui::CommandSystem::command(gui::DocumentCommands::idDelete());
    ASSERT_NE(cmd, nullptr);
    QAction* proxy = cmd->action();
    ASSERT_NE(proxy, nullptr);
    // 上下文激活时 proxy 应已绑定到 deleteAction
    EXPECT_TRUE(proxy->isEnabled());

    proxy->trigger();
    EXPECT_FALSE(reg.valid(a));
}
