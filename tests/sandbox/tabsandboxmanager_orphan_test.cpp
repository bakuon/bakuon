#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QEventLoop>
#include <QProcess>
#include <QTimer>

#include <sandbox/b_tabsandboxmanager.h>

using namespace bakuon::sandbox;

namespace {

// 见 tests/sandbox/sandbox_integration_test.cpp / tabsandboxmanager_test.cpp
// 里对这两个 helper 的详细注释，本文件沿用同样的写法。
QCoreApplication &app()
{
    static int argc     = 1;
    static char argv0[] = "test_sandbox_tabsandboxmanager_orphan";
    static char *argv[] = {argv0, nullptr};
    static QCoreApplication instance(argc, argv);
    return instance;
}

template<typename Predicate>
bool waitUntil(Predicate predicate, int timeoutMs = 10000)
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

QString orphanHelperPath()
{
    return QString::fromLatin1(BAKUON_TEST_ORPHAN_HELPER_PATH);
}

} // namespace

// BAKUON_TEST_SANDBOX_RUNTIME_PATH / BAKUON_TEST_SANDBOXED_EXAMPLE_PLUGIN_PATH /
// BAKUON_TEST_ORPHAN_HELPER_PATH 由 tests/CMakeLists.txt 通过 $<TARGET_FILE:...> 注入。

// 真实模拟"Host 崩溃 -> 重启 -> 重新发现并收编孤儿沙箱"整条链路：
//  1. 启动 orphan_test_helper 子进程，它扮演"第一代 Host"：自己起一个
//     TabSandboxManager、打开一个 Tab、等到 Running 后打印 READY。
//  2. 收到 READY 后直接 kill()（SIGKILL）这个子进程——不给它任何优雅关闭的机会，
//     它自己 spawn() 出来的 sandbox_runtime 孙进程因此真正变成孤儿（不会被
//     SandboxSupervisor 析构函数里的优雅关闭逻辑一并带走）。
//  3. 本测试进程创建自己的 TabSandboxManager（"第二代 Host"），验证它能通过
//     QRemoteObjectRegistryHost 的自动重连机制重新发现那个孤儿，并成功收编。
//
// 这条链路依赖的核心 QtRO 行为（孤儿重连到同地址新注册中心）已经用一个独立的
// throwaway 实验单独验证过，这里是在真实产品代码路径上（SandboxSystem::adopt() /
// SandboxSupervisor::attach()）的端到端确认，不是重复验证同一件事。
TEST(TabSandboxManagerOrphanTest, RediscoverAndAdoptOrphanAfterHostCrash)
{
    Q_UNUSED(app())

    QProcess helper;
    helper.setProgram(orphanHelperPath());
    helper.setArguments({sandboxRuntimePath(), sandboxedExamplePluginPath()});

    QString helperOutput;
    QObject::connect(&helper, &QProcess::readyReadStandardOutput, [&] {
        helperOutput += QString::fromLocal8Bit(helper.readAllStandardOutput());
    });
    helper.start();
    ASSERT_TRUE(helper.waitForStarted(5000)) << "orphan_test_helper 启动失败";

    ASSERT_TRUE(waitUntil([&] { return helperOutput.contains(QStringLiteral("READY")); }, 15000))
        << "等待 orphan_test_helper 打印 READY 超时（它内部要真正跑起来一个沙箱实例）";

    // 模拟崩溃：SIGKILL，不给任何析构/清理代码运行的机会。
    helper.kill();
    ASSERT_TRUE(helper.waitForFinished(5000)) << "等待 orphan_test_helper 被 kill 后退出超时";
    ASSERT_EQ(helper.exitStatus(), QProcess::CrashExit);

    // "第二代 Host"：这里才第一次在本测试进程里创建 SandboxSystem/注册中心——
    // 必须等上面那个 helper 进程（连同它持有的注册中心）真正退出之后才能创建，
    // 否则会因为地址已被占用而失败（同一时刻只能有一个进程监听 registryUrl()）。
    TabSandboxManager manager(sandboxRuntimePath());

    QString discoveredOrphanId;
    QObject::connect(&manager,
                     &TabSandboxManager::orphanSandboxAvailable,
                     &manager,
                     [&](const QString &sandboxId) { discoveredOrphanId = sandboxId; });

    // 孤儿沙箱子进程需要先感知到旧注册中心连接断开、再自动重连到新起的同地址注册中心，
    // 这个过程有内部重试退避，因此这里给了比其他测试更宽松的超时。
    ASSERT_TRUE(waitUntil([&] { return !discoveredOrphanId.isEmpty(); }, 15000))
        << "等待孤儿沙箱被重新发现超时——orphanSandboxAvailable 一直没有触发";

    uint64_t adoptedTabId;
    QString adoptedSandboxId;
    QObject::connect(&manager,
                     &TabSandboxManager::tabAdopted,
                     &manager,
                     [&](uint64_t tabId, const QString &sandboxId) {
                         adoptedTabId     = tabId;
                         adoptedSandboxId = sandboxId;
                     });

    const QVector<uint64_t> adopted = manager.tryAdoptOrphanedSandboxes();
    ASSERT_EQ(adopted.size(), 1);
    EXPECT_EQ(adopted[0], adoptedTabId);
    EXPECT_EQ(adoptedSandboxId, discoveredOrphanId);
    EXPECT_EQ(manager.sandboxIdForTab(adoptedTabId), discoveredOrphanId);

    // 孤儿在被杀之前已经跑到 Running 了（helper 打印 READY 的前提就是它自己的
    // TabSandboxManager 观察到了 tabRunning）；attach() 不会重新 loadPlugin()，
    // Replica 变 Valid 的那一刻应该立即同步出它当前的真实阶段——应该直接是 Running，
    // 不需要重新走一遍 Connecting/Loading/Initializing/Ready。
    ASSERT_TRUE(waitUntil([&] { return manager.tabState(adoptedTabId) == TabState::Running; }))
        << "收编回来的孤儿没有正确同步到 Running 状态";

    // 收编来的 Tab 没有 pluginFilePath（本进程从未见过它，见类文档），这是刻意的
    // 已知限制，这里顺带确认这一点符合文档描述，而不是意外地拿到了某个值。
    const auto session = manager.sessionForTab(adoptedTabId);
    ASSERT_TRUE(session.has_value());
    EXPECT_TRUE(session->pluginFilePath.isEmpty());

    manager.closeAll();
    ASSERT_TRUE(waitUntil([&] { return manager.count() == 0; })) << "等待 closeAll 收尾超时";
}
