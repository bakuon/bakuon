#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QItemSelectionModel>

#include <bakuon/core/Components.h>
#include <bakuon/core/Hierarchy.h>

#include "gui/b_documentsession.h"
#include "gui/b_hierarchymodel.h"

using namespace bakuon;
using bakuon::core::Handle;
namespace h = bakuon::core::hierarchy;

namespace {

Handle makeNamed(core::Registry& reg, const std::string& name)
{
    const Handle e = reg.create();
    reg.emplace<core::components::Name>(e, core::components::Name{name});
    return e;
}

} // namespace

TEST(HierarchyModelTest, EmptySessionHasZeroRows)
{
    gui::DocumentSession session;
    gui::HierarchyModel model(session);
    EXPECT_EQ(model.rowCount(), 0);
}

TEST(HierarchyModelTest, RootsAppearAsTopLevelRows)
{
    gui::DocumentSession session;
    auto& reg = session.registry();
    const Handle root = makeNamed(reg, "R");
    const Handle a    = makeNamed(reg, "A");
    const Handle b    = makeNamed(reg, "B");
    ASSERT_TRUE(h::append(reg, a, root));
    ASSERT_TRUE(h::append(reg, b, root));

    gui::HierarchyModel model(session);
    // roots() 只收集带 Hierarchy 且无 parent 的节点 → root
    ASSERT_EQ(model.rowCount(), 1);
    const QModelIndex rootIdx = model.index(0, 0);
    ASSERT_TRUE(rootIdx.isValid());
    EXPECT_EQ(model.data(rootIdx, Qt::DisplayRole).toString(), QStringLiteral("R"));
    EXPECT_EQ(model.rowCount(rootIdx), 2);

    const QModelIndex aIdx = model.index(0, 0, rootIdx);
    const QModelIndex bIdx = model.index(1, 0, rootIdx);
    EXPECT_EQ(model.data(aIdx).toString(), QStringLiteral("A"));
    EXPECT_EQ(model.data(bIdx).toString(), QStringLiteral("B"));
    EXPECT_EQ(model.handleForIndex(aIdx), a);
}

TEST(HierarchyModelTest, IndexForHandleRoundTrip)
{
    gui::DocumentSession session;
    auto& reg = session.registry();
    const Handle root = makeNamed(reg, "R");
    const Handle child = makeNamed(reg, "C");
    ASSERT_TRUE(h::append(reg, child, root));

    gui::HierarchyModel model(session);
    const QModelIndex idx = model.indexForHandle(child);
    ASSERT_TRUE(idx.isValid());
    EXPECT_EQ(model.handleForIndex(idx), child);
    EXPECT_EQ(model.parent(idx), model.indexForHandle(root));
}

TEST(HierarchyModelTest, BindSelectionSyncsBothWays)
{
    gui::DocumentSession session;
    auto& reg = session.registry();
    const Handle root = makeNamed(reg, "R");
    const Handle a    = makeNamed(reg, "A");
    ASSERT_TRUE(h::append(reg, a, root));

    gui::HierarchyModel model(session);
    QItemSelectionModel selModel(&model);
    model.bindSelection(&selModel);

    // session → view
    session.selection().exclusive(a);
    const QModelIndex aIdx = model.indexForHandle(a);
    ASSERT_TRUE(aIdx.isValid());
    EXPECT_TRUE(selModel.isSelected(aIdx));

    // view → session：清掉再选 root
    const QModelIndex rootIdx = model.indexForHandle(root);
    selModel.select(rootIdx, QItemSelectionModel::ClearAndSelect);
    EXPECT_TRUE(session.selection().contains(root));
    EXPECT_FALSE(session.selection().contains(a));
}

TEST(HierarchyModelTest, ReloadAfterStructuralChange)
{
    gui::DocumentSession session;
    auto& reg = session.registry();
    const Handle root = makeNamed(reg, "R");
    const Handle a    = makeNamed(reg, "A");
    ASSERT_TRUE(h::append(reg, a, root));

    gui::HierarchyModel model(session);
    EXPECT_EQ(model.rowCount(model.index(0, 0)), 1);

    const Handle b = makeNamed(reg, "B");
    ASSERT_TRUE(h::append(reg, b, root));
    // Hierarchy construct 会触发 model reload
    EXPECT_EQ(model.rowCount(model.index(0, 0)), 2);
}
