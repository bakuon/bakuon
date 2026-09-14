#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QSignalSpy>

#include <bakuon/core/Components.h>
#include <bakuon/core/Hierarchy.h>

#include "gui/b_commandsystem.h"
#include "gui/b_documentsession.h"

using namespace bakuon;
using bakuon::core::Handle;
namespace h = bakuon::core::hierarchy;

TEST(DocumentSessionTest, OwnsRegistryAndSelection)
{
    gui::DocumentSession session;
    const Handle a = session.registry().create();
    session.selection().add(a);
    EXPECT_TRUE(session.selection().contains(a));
    EXPECT_TRUE(session.registry().has<core::components::Selected>(a));
}

TEST(DocumentSessionTest, SelectionActivatesContext)
{
    gui::DocumentSession session(gui::ContextId{"editor.selection.test"});
    const gui::ContextId ctx = session.selectionContextId();

    EXPECT_FALSE(session.isSelectionContextActive());
    EXPECT_FALSE(gui::CommandSystem::isActiveContext(ctx));

    const Handle a = session.registry().create();
    session.selection().add(a);

    EXPECT_TRUE(session.isSelectionContextActive());
    EXPECT_TRUE(gui::CommandSystem::isActiveContext(ctx));

    session.selection().clear();
    EXPECT_FALSE(session.isSelectionContextActive());
    EXPECT_FALSE(gui::CommandSystem::isActiveContext(ctx));
}

TEST(DocumentSessionTest, SelectionChangedSignal)
{
    gui::DocumentSession session;
    QSignalSpy spy(&session, &gui::DocumentSession::selectionChanged);

    const Handle a = session.registry().create();
    session.selection().add(a);
    ASSERT_GE(spy.count(), 1);

    session.selection().clear();
    ASSERT_GE(spy.count(), 2);
}

TEST(DocumentSessionTest, DestructorPopsSelectionContext)
{
    gui::ContextId ctx{"editor.selection.dtor"};
    {
        gui::DocumentSession session(ctx);
        session.selection().add(session.registry().create());
        EXPECT_TRUE(gui::CommandSystem::isActiveContext(ctx));
    }
    EXPECT_FALSE(gui::CommandSystem::isActiveContext(ctx));
}

TEST(DocumentSessionTest, HierarchyAttachVisibleViaRegistry)
{
    gui::DocumentSession session;
    auto& reg = session.registry();
    const Handle parent = reg.create();
    const Handle child  = reg.create();
    reg.emplace<core::components::Name>(parent, core::components::Name{"root"});
    reg.emplace<core::components::Name>(child, core::components::Name{"leaf"});

    ASSERT_TRUE(h::append(reg, child, parent));
    EXPECT_EQ(h::childCount(reg, parent), 1u);
    EXPECT_EQ(h::parent(reg, child), parent);
}
