#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QEventLoop>
#include <QTimer>

#include <sandbox/b_tabsandboxmanager.h>

using namespace bakuon::sandbox;

namespace {

/// 见 tests/sandbox/sandbox_integration_test.cpp 里对这两个 helper 的详细注释；
/// 本文件同样需要真实子进程通信，故采用同样的模式（本项目里 QCoreApplication
/// 惰性单例 + 轮询 waitUntil 是既定的测试基础设施写法，暂无共享 test-utils 头文件）。
QCoreApplication &app()
{
    static int argc     = 1;
    static char argv0[] = "test_sandbox_tabsandboxmanager";
    static char *argv[] = {argv0, nullptr};
    static QCoreApplication instance(argc, argv);
    return instance;
}

template<typename Predicate>
bool waitUntil(Predicate predicate, int timeoutMs = 5000)
{
    QEventLoop loop;
    QTimer timeoutTimer;
    timeoutTimer.setSingleShot(true);
    QObject::connect(&timeoutTimer, &QTimer::timeout, &loop, &QEventLoop::quit);
    timeoutTimer.start(timeoutMs);

    QTimer pollTimer;
    QObject::connect(&pollTimer, &QTimer::timeout, [&] {
        if (predicate()) {
            loop.quit();
        }
    });
    pollTimer.start(10);

    if (predicate()) {
        return true;
    }
    loop.exec();
    return predicate();
}

QString sandboxRuntimePath()
{
    return QString::fromLatin1(BAKUON_TEST_SANDBOX_RUNTIME_PATH);
}

QString sandboxedExamplePluginPath()
{
    return QString::fromLatin1(BAKUON_TEST_SANDBOXED_EXAMPLE_PLUGIN_PATH);
}

} // namespace

// BAKUON_TEST_SANDBOX_RUNTIME_PATH / BAKUON_TEST_SANDBOXED_EXAMPLE_PLUGIN_PATH
// 由 tests/CMakeLists.txt 通过 $<TARGET_FILE:...> 注入，指向真实构建产物路径。

TEST(TabSandboxManagerTest, OpenRunCloseLifecycle)
{
    Q_UNUSED(app())

    TabSandboxManager manager(sandboxRuntimePath());

    std::vector<TabState> observedForTab;
    uint64_t tabId;
    QObject::connect(&manager, &TabSandboxManager::tabLaunching, &manager, [&](uint64_t id) {
        // 注意：tabLaunching 是 openTab() 内部同步 emit 的（同线程直连），
        // 在 openTab() 返回、给外层 tabId 赋值之前就已经触发——不能在这里
        // 用 `id == tabId` 过滤（此时外层 tabId 还是默认构造的无效值），
        // 本测试全程只开一个 Tab，直接记录即可。
        Q_UNUSED(id)
        observedForTab.push_back(TabState::Launching);
    });
    QObject::connect(&manager, &TabSandboxManager::tabRunning, &manager, [&](uint64_t id) {
        if (id == tabId) {
            observedForTab.push_back(TabState::Running);
        }
    });
    bool closed = false;
    QObject::connect(&manager, &TabSandboxManager::tabClosed, &manager, [&](uint64_t id) {
        if (id == tabId) {
            closed = true;
        }
    });

    tabId = manager.openTab(sandboxedExamplePluginPath());
    ASSERT_TRUE(tabId != 0);
    EXPECT_EQ(manager.tabState(tabId), TabState::Launching);
    EXPECT_FALSE(manager.sandboxIdForTab(tabId).isEmpty());
    EXPECT_EQ(manager.tabForSandboxId(manager.sandboxIdForTab(tabId)), tabId);

    ASSERT_TRUE(waitUntil([&] { return manager.tabState(tabId) == TabState::Running; }))
        << "等待 Tab 进入 Running 超时";
    ASSERT_EQ(observedForTab.size(), 2u);
    EXPECT_EQ(observedForTab[0], TabState::Launching);
    EXPECT_EQ(observedForTab[1], TabState::Running);

    ASSERT_TRUE(manager.closeTab(tabId));
    EXPECT_EQ(manager.tabState(tabId), TabState::Closing);
    ASSERT_TRUE(waitUntil([&] { return closed; })) << "等待 tabClosed 信号超时";
    EXPECT_EQ(manager.count(), 0u);
}

TEST(TabSandboxManagerTest, ConcurrencyThrottlingBackfillsQueue)
{
    Q_UNUSED(app())

    TabSandboxManager manager(sandboxRuntimePath());
    manager.setMaxConcurrentSandboxes(1);

    std::vector<uint64_t> queuedIds;
    QObject::connect(&manager, &TabSandboxManager::tabQueued, &manager, [&](uint64_t id) {
        queuedIds.push_back(id);
    });

    const auto first  = manager.openTab(sandboxedExamplePluginPath());
    const auto second = manager.openTab(sandboxedExamplePluginPath());
    ASSERT_TRUE(first != 0);
    ASSERT_TRUE(second != 0);

    // 上限为 1：第一个 Tab 立即 spawn()，第二个应该进入排队，而不是也立刻 spawn()。
    EXPECT_EQ(manager.tabState(first), TabState::Launching);
    EXPECT_EQ(manager.tabState(second), TabState::Queued);
    ASSERT_EQ(queuedIds.size(), 1u);
    EXPECT_EQ(queuedIds[0], second);

    ASSERT_TRUE(waitUntil([&] { return manager.tabState(first) == TabState::Running; }))
        << "等待第一个 Tab 进入 Running 超时";

    // 关闭第一个 Tab 之后，排队中的第二个应该自动补上名额、开始 spawn()。
    ASSERT_TRUE(manager.closeTab(first));
    ASSERT_TRUE(waitUntil(
        [&] {
            return manager.tabState(second) == TabState::Launching
                   || manager.tabState(second) == TabState::Running;
        },
        5000))
        << "等待排队中的第二个 Tab 被补上超时";
    ASSERT_TRUE(waitUntil([&] { return manager.tabState(second) == TabState::Running; }))
        << "等待第二个 Tab 进入 Running 超时";

    manager.closeAll();
    ASSERT_TRUE(waitUntil([&] { return manager.count() == 0; })) << "等待 closeAll 收尾超时";
}

TEST(TabSandboxManagerTest, FaultedTabDoesNotAffectOtherTabs)
{
    Q_UNUSED(app())

    TabSandboxManager manager(sandboxRuntimePath());

    QString faultReason;
    uint64_t faultedTabId;
    QObject::connect(&manager,
                     &TabSandboxManager::tabFaulted,
                     &manager,
                     [&](uint64_t id, const QString &reason) {
                         faultedTabId = id;
                         faultReason  = reason;
                     });

    // 一个指向不存在文件的"坏"插件路径，触发 loadPlugin() 失败 -> Faulted；
    // 一个正常的插件路径，验证它完全不受影响，照常跑到 Running。
    const auto badTab  = manager.openTab(QStringLiteral("/nonexistent/path/does_not_exist.so"));
    const auto goodTab = manager.openTab(sandboxedExamplePluginPath());
    ASSERT_TRUE(badTab);
    ASSERT_TRUE(goodTab);

    ASSERT_TRUE(waitUntil([&] { return manager.tabState(badTab) == TabState::Faulted; }))
        << "等待坏插件的 Tab 进入 Faulted 超时";
    EXPECT_EQ(faultedTabId, badTab);
    EXPECT_FALSE(faultReason.isEmpty());

    ASSERT_TRUE(waitUntil([&] { return manager.tabState(goodTab) == TabState::Running; }))
        << "坏 Tab 不应该影响好 Tab 正常启动";

    manager.closeAll();
    ASSERT_TRUE(waitUntil([&] { return manager.count() == 0; })) << "等待 closeAll 收尾超时";
}

TEST(TabSandboxManagerTest, RestartTabReplacesSandboxId)
{
    Q_UNUSED(app())

    TabSandboxManager manager(sandboxRuntimePath());

    const auto tabId = manager.openTab(sandboxedExamplePluginPath());
    ASSERT_TRUE(tabId != 0);
    ASSERT_TRUE(waitUntil([&] { return manager.tabState(tabId) == TabState::Running; }));

    const QString oldSandboxId = manager.sandboxIdForTab(tabId);
    ASSERT_FALSE(oldSandboxId.isEmpty());

    ASSERT_TRUE(manager.restartTab(tabId));
    // restartTab() 立即重新 spawn()，新 sandboxId 应该马上可见（即使还在 Launching）。
    const QString newSandboxId = manager.sandboxIdForTab(tabId);
    EXPECT_NE(newSandboxId, oldSandboxId);
    EXPECT_FALSE(newSandboxId.isEmpty());
    // 旧的 sandboxId 不应该再映射回这个 Tab（restartTab 已经主动摘掉旧映射）。
    EXPECT_FALSE(manager.tabForSandboxId(oldSandboxId).has_value());

    ASSERT_TRUE(waitUntil([&] { return manager.tabState(tabId) == TabState::Running; }))
        << "等待重启后的 Tab 重新进入 Running 超时";
    EXPECT_EQ(manager.sandboxIdForTab(tabId), newSandboxId);

    manager.closeAll();
    ASSERT_TRUE(waitUntil([&] { return manager.count() == 0; })) << "等待 closeAll 收尾超时";
}
