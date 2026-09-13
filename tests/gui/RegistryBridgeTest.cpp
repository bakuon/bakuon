#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QSignalSpy>
#include <QString>

#include <bakuon/core/Registry.h>

#include "gui/b_registrybridge.h"

using namespace bakuon;

namespace {

struct Position
{
    float x = 0.f;
    float y = 0.f;
};

struct Selected
{
};

} // namespace

TEST(RegistryBridgeTest, ConstructEmitsEntityConstructedWithMatchingTag)
{
    core::Registry registry;
    gui::RegistryBridge bridge(registry);
    bridge.watch<Position>(QStringLiteral("Position"));

    QSignalSpy spy(&bridge, &gui::RegistryBridge::entityConstructed);
    const core::Handle node = registry.create();
    registry.emplace<Position>(node, 1.f, 2.f);

    ASSERT_EQ(spy.count(), 1);
    const QList<QVariant> args = spy.takeFirst();
    EXPECT_EQ(args.at(0).toString(), QStringLiteral("Position"));
    EXPECT_EQ(args.at(1).value<core::Handle>(), node);
}

TEST(RegistryBridgeTest, PatchEmitsEntityUpdated)
{
    core::Registry registry;
    gui::RegistryBridge bridge(registry);
    bridge.watch<Position>(QStringLiteral("Position"));

    const core::Handle node = registry.create();
    registry.emplace<Position>(node, 0.f, 0.f);

    QSignalSpy spy(&bridge, &gui::RegistryBridge::entityUpdated);
    registry.patch<Position>(node, [](Position& p) { p.x = 42.f; });

    ASSERT_EQ(spy.count(), 1);
    EXPECT_EQ(spy.takeFirst().at(0).toString(), QStringLiteral("Position"));
    // 信号本身不携带数值：槽函数应该自己回头查 Registry。
    EXPECT_EQ(registry.tryGet<Position>(node)->x, 42.f);
}

TEST(RegistryBridgeTest, RemoveEmitsEntityDestroyed)
{
    core::Registry registry;
    gui::RegistryBridge bridge(registry);
    bridge.watch<Position>(QStringLiteral("Position"));

    const core::Handle node = registry.create();
    registry.emplace<Position>(node, 0.f, 0.f);

    QSignalSpy spy(&bridge, &gui::RegistryBridge::entityDestroyed);
    registry.remove<Position>(node);

    ASSERT_EQ(spy.count(), 1);
    EXPECT_EQ(spy.takeFirst().at(0).toString(), QStringLiteral("Position"));
}

TEST(RegistryBridgeTest, MultipleWatchedTypesUseDistinctTags)
{
    core::Registry registry;
    gui::RegistryBridge bridge(registry);
    bridge.watch<Position>(QStringLiteral("Position"));
    bridge.watch<Selected>(QStringLiteral("Selected"));

    QSignalSpy spy(&bridge, &gui::RegistryBridge::entityConstructed);
    const core::Handle node = registry.create();
    registry.emplace<Position>(node, 0.f, 0.f);
    registry.emplace<Selected>(node);

    ASSERT_EQ(spy.count(), 2);
    EXPECT_EQ(spy.at(0).at(0).toString(), QStringLiteral("Position"));
    EXPECT_EQ(spy.at(1).at(0).toString(), QStringLiteral("Selected"));
}

TEST(RegistryBridgeTest, UnwatchedComponentTypesProduceNoSignal)
{
    core::Registry registry;
    gui::RegistryBridge bridge(registry);
    bridge.watch<Position>(QStringLiteral("Position"));

    QSignalSpy spy(&bridge, &gui::RegistryBridge::entityConstructed);
    const core::Handle node = registry.create();
    registry.emplace<Selected>(node); // 没有 watch<Selected>()，不应该产生任何信号

    EXPECT_EQ(spy.count(), 0);
}

TEST(RegistryBridgeTest, DestroyingBridgeDisconnectsFromRegistry)
{
    core::Registry registry;
    const core::Handle node = registry.create();
    registry.emplace<Position>(node, 0.f, 0.f);

    {
        gui::RegistryBridge bridge(registry);
        bridge.watch<Position>(QStringLiteral("Position"));
    } // bridge 析构：内部的 core::Connection 应该自动断开

    // bridge 已经不存在了，但 registry 应该仍然可以正常工作，不应该崩溃。
    EXPECT_NO_THROW(registry.patch<Position>(node, [](Position& p) { p.x = 1.f; }));
    EXPECT_EQ(registry.tryGet<Position>(node)->x, 1.f);
}

TEST(RegistryBridgeTest, BatchedUpdatesStillProduceOneSignalPerCoalescedNotification)
{
    // RegistryBridge 只是把 core::Registry 已经产生的通知转成信号，Registry 自身
    // 的批量合并语义（beginBatch()/endBatch()，见 b_registry.h）应该原样透传。
    core::Registry registry;
    gui::RegistryBridge bridge(registry);
    bridge.watch<Position>(QStringLiteral("Position"));

    const core::Handle node = registry.create();
    registry.emplace<Position>(node, 0.f, 0.f);

    QSignalSpy spy(&bridge, &gui::RegistryBridge::entityUpdated);
    {
        core::Registry::BatchGuard batch(registry);
        registry.patch<Position>(node, [](Position& p) { p.x = 1.f; });
        registry.patch<Position>(node, [](Position& p) { p.x = 2.f; });
        registry.patch<Position>(node, [](Position& p) { p.x = 3.f; });
    }

    EXPECT_EQ(spy.count(), 1);
}
