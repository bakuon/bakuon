#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QEventLoop>
#include <QFile>
#include <QProcess>
#include <QTemporaryDir>
#include <QTimer>

#include <sandbox/b_tabsandboxmanager.h>

using namespace bakuon::sandbox;

namespace {

// 见 tabsandboxmanager_test.cpp / tabsandboxmanager_orphan_test.cpp 里对这两个
// helper 的详细注释，本文件沿用同样的写法。
QCoreApplication &app()
{
    static int argc     = 1;
    static char argv0[] = "test_sandbox_tabsandboxmanager_session";
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

// 端到端验证"会话持久化 -> 崩溃 -> 原地恢复成同一个 TabId"这条完整链路，
// 不是拿 mock 数据摆样子——真实起一个子进程、真实写盘、真实 SIGKILL、真实用
// 第二个进程读盘 + 重新发现孤儿。链路的"重新发现孤儿"这一半已经在
// tabsandboxmanager_orphan_test.cpp 里单独验证过，本测试的重点是"发现之后
// 是不是真的接回了同一个 tabId、pluginFilePath 有没有一并恢复"。
TEST(TabSandboxManagerSessionTest, RestoreSameTabIdAfterCrashUsingPersistedSession)
{
    Q_UNUSED(app());

    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());
    const QString sessionFilePath = tempDir.filePath(QStringLiteral("tab-sessions.json"));

    QProcess helper;
    helper.setProgram(orphanHelperPath());
    helper.setArguments({sandboxRuntimePath(), sandboxedExamplePluginPath(), sessionFilePath});

    QString helperOutput;
    QObject::connect(&helper, &QProcess::readyReadStandardOutput, [&] {
        helperOutput += QString::fromLocal8Bit(helper.readAllStandardOutput());
    });
    helper.start();
    ASSERT_TRUE(helper.waitForStarted(5000)) << "orphan_test_helper 启动失败";

    ASSERT_TRUE(waitUntil([&] { return helperOutput.contains(QStringLiteral("READY")); }, 15000))
        << "等待 orphan_test_helper 打印 READY 超时";

    // 从 "READY <tabId>\n" 里把崩溃前的 tabId 解析出来，后面用来断言"原地恢复"
    // 真的恢复出了同一个 tabId，而不是巧合碰到了同一个值。
    const QStringList parts = helperOutput.trimmed().split(QLatin1Char(' '));
    ASSERT_EQ(parts.size(), 2) << "helper 输出格式不符合预期: " << helperOutput.toStdString();
    bool tabIdOk                 = false;
    const uint64_t originalTabId = parts[1].toULongLong(&tabIdOk);
    ASSERT_TRUE(tabIdOk);
    ASSERT_NE(originalTabId, 0u);

    // 崩溃之前，session 文件应该已经落盘了这个 tabId（persist() 在 Running 阶段
    // 同步触发，不是等进程退出才写）——顺带验证一下持久化确实在生效，而不是等到
    // 后面 restoreSession() 返回 0 才发现白测了。
    ASSERT_TRUE(QFile::exists(sessionFilePath)) << "崩溃前会话文件应该已经存在";

    // 模拟崩溃：SIGKILL，不给任何析构/清理代码运行的机会。
    helper.kill();
    ASSERT_TRUE(helper.waitForFinished(5000)) << "等待 orphan_test_helper 被 kill 后退出超时";
    ASSERT_EQ(helper.exitStatus(), QProcess::CrashExit);

    // "第二代 Host"：先设置会话文件路径、连接信号，再显式 restoreSession()——
    // 和类文档里描述的推荐调用顺序一致。
    TabSandboxManager manager(sandboxRuntimePath());
    manager.setSessionFilePath(sessionFilePath);

    uint64_t restoringTabId = 0;
    QObject::connect(&manager, &TabSandboxManager::tabRestoring, &manager, [&](uint64_t tabId) {
        restoringTabId = tabId;
    });

    const int restoredCount = manager.restoreSession();
    ASSERT_EQ(restoredCount, 1);
    ASSERT_EQ(restoringTabId, originalTabId) << "restoreSession() 放回来的 tabId 和崩溃前不一致";
    ASSERT_EQ(manager.tabState(originalTabId), TabState::Restoring);
    ASSERT_EQ(manager.pendingRestoreCount(), 1u);

    // restoreSession() 恢复出来的记录应该已经带着 pluginFilePath——这正是相对纯
    // "陌生孤儿收编"（tabAdopted，pluginFilePath 恒为空）的关键区别。
    {
        const auto session = manager.sessionForTab(originalTabId);
        ASSERT_TRUE(session.has_value());
        EXPECT_EQ(session->pluginFilePath, sandboxedExamplePluginPath());
        EXPECT_FALSE(session->sandboxId.isEmpty());
    }

    QString discoveredOrphanId;
    QObject::connect(&manager,
                     &TabSandboxManager::orphanSandboxAvailable,
                     &manager,
                     [&](const QString &sandboxId) { discoveredOrphanId = sandboxId; });

    ASSERT_TRUE(waitUntil([&] { return !discoveredOrphanId.isEmpty(); }, 15000))
        << "等待孤儿沙箱被重新发现超时";

    uint64_t restoredTabId = 0;
    QString restoredSandboxId;
    QObject::connect(&manager,
                     &TabSandboxManager::tabRestored,
                     &manager,
                     [&](uint64_t tabId, const QString &sandboxId) {
                         restoredTabId     = tabId;
                         restoredSandboxId = sandboxId;
                     });
    bool tabAdoptedFired = false;
    QObject::connect(&manager,
                     &TabSandboxManager::tabAdopted,
                     &manager,
                     [&](uint64_t, const QString &) { tabAdoptedFired = true; });

    const QVector<uint64_t> adopted = manager.tryAdoptOrphanedSandboxes();
    ASSERT_EQ(adopted.size(), 1);

    // 核心断言：原地恢复，tabId 前后完全一致，走的是 tabRestored 而不是 tabAdopted。
    EXPECT_EQ(adopted[0], originalTabId);
    EXPECT_EQ(restoredTabId, originalTabId);
    EXPECT_EQ(restoredSandboxId, discoveredOrphanId);
    EXPECT_FALSE(tabAdoptedFired) << "原地恢复不应该触发 tabAdopted（那是给陌生孤儿用的）";
    EXPECT_EQ(manager.pendingRestoreCount(), 0u);

    ASSERT_TRUE(waitUntil([&] { return manager.tabState(originalTabId) == TabState::Running; }))
        << "原地恢复回来的 Tab 没有正确同步到 Running 状态";

    manager.closeAll();
    ASSERT_TRUE(waitUntil([&] { return manager.count() == 0; })) << "等待 closeAll 收尾超时";

    // 关闭之后，持久化文件里也不应该再留着这条已经关闭的记录——不然下次启动会
    // 平白多出一个永远等不到匹配孤儿的 Restoring 条目。
    QFile file(sessionFilePath);
    ASSERT_TRUE(file.open(QIODevice::ReadOnly));
    const QString remaining = QString::fromUtf8(file.readAll());
    EXPECT_FALSE(remaining.contains(QString::number(originalTabId)))
        << "会话文件在 Tab 关闭后应该已经不再包含它的记录了";
}

// 覆盖"没有等到匹配孤儿，调用方主动放弃、respawnRestoredTab() 重新 spawn()"这条
// 降级路径——不需要真实崩溃场景，直接手工构造一份会话文件，指向一个必然找不到的
// sandboxId（模拟"上次退出时 Tab 其实是 Queued 状态、根本没有 sandboxId"或者
// "对应的孤儿已经彻底死透了、不会再出现"这两种情况）。
TEST(TabSandboxManagerSessionTest, RespawnRestoredTabWhenNoOrphanShowsUp)
{
    Q_UNUSED(app());

    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());
    const QString sessionFilePath = tempDir.filePath(QStringLiteral("tab-sessions.json"));

    {
        QFile file(sessionFilePath);
        ASSERT_TRUE(file.open(QIODevice::WriteOnly));
        const QString json = QStringLiteral(
                                 R"([{"tabId":"42","sandboxId":"","pluginFilePath":"%1",)"
                                 R"("sandboxRuntimeExecutable":"","pluginArguments":{}}])")
                                 .arg(sandboxedExamplePluginPath());
        file.write(json.toUtf8());
    }

    TabSandboxManager manager(sandboxRuntimePath());
    manager.setSessionFilePath(sessionFilePath);

    ASSERT_EQ(manager.restoreSession(), 1);
    ASSERT_EQ(manager.tabState(42), TabState::Restoring);
    ASSERT_EQ(manager.pendingRestoreCount(), 1u);

    ASSERT_TRUE(manager.respawnRestoredTab(42));
    EXPECT_EQ(manager.pendingRestoreCount(), 0u);
    ASSERT_TRUE(waitUntil([&] { return manager.tabState(42) == TabState::Running; }))
        << "respawnRestoredTab() 之后没能正常跑到 Running";
    EXPECT_FALSE(manager.sandboxIdForTab(42).isEmpty());

    manager.closeAll();
    ASSERT_TRUE(waitUntil([&] { return manager.count() == 0; })) << "等待 closeAll 收尾超时";
}
