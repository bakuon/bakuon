#include <gtest/gtest.h>

#include <cstring>
#include <vector>

#include <QByteArray>
#include <QCoreApplication>
#include <QEventLoop>
#include <QTimer>

#include <sandbox/b_sandboxsupervisor.h>

using namespace bakuon::sandbox;

namespace {

/// gtest 默认的 main()（bakuon_add_test 链接的 gtest_main）不会构造 QCoreApplication，
/// 但本文件里的测试要跑真正的子进程通信（QProcess/QRemoteObjectNode/本地 socket），
/// 都需要一个活的 Qt 事件循环——用函数局部 static 在首次用到时惰性构造一次，
/// 进程生命周期内只构造一次，规避 QCoreApplication 不能重复构造的限制。
QCoreApplication &app()
{
    static int argc      = 1;
    static char argv0[]  = "sandbox_integration_test";
    static char *argv[]  = {argv0, nullptr};
    static QCoreApplication instance(argc, argv);
    return instance;
}

/// 反复 processEvents() 直到 predicate 为真或超时；比裸的 QEventLoop + quit() 更方便在
/// gtest 断言里表达"等某个信号触发的副作用发生"，且不需要每个测试都手写 QTimer 兜底退出。
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

} // namespace

// BAKUON_TEST_SANDBOX_RUNTIME_PATH / BAKUON_TEST_SANDBOXED_EXAMPLE_PLUGIN_PATH
// 由 tests/CMakeLists.txt 通过 $<TARGET_FILE:...> 注入，指向真实构建产物路径。

TEST(SandboxSupervisorIntegrationTest, FullLifecycleAndSharedMemoryCommandRoundTrip)
{
    Q_UNUSED(app()); // 确保 QCoreApplication 存在

    SandboxSupervisor supervisor(QStringLiteral("it-sandbox-1"),
                                 QString::fromLatin1(BAKUON_TEST_SANDBOXED_EXAMPLE_PLUGIN_PATH));

    std::vector<SandboxPhase> observedPhases;
    QObject::connect(&supervisor, &SandboxSupervisor::phaseChanged, &supervisor,
                     [&](SandboxPhase p) { observedPhases.push_back(p); });

    QString faultReason;
    QObject::connect(&supervisor, &SandboxSupervisor::faulted, &supervisor,
                     [&](const QString &reason) { faultReason = reason; });

    supervisor.start(QString::fromLatin1(BAKUON_TEST_SANDBOX_RUNTIME_PATH));

    // 1) loadPlugin() 是 Replica 变为 Valid 后 SandboxSupervisor 自动发起的，
    //    等到阶段推进到 Ready（即真实子进程里的 PluginPipeline 已经跑完 Initializing）。
    ASSERT_TRUE(waitUntil([&] { return supervisor.phase() == SandboxPhase::Ready ||
                                        supervisor.phase() == SandboxPhase::Faulted; }))
        << "等待沙箱进入 Ready 阶段超时";
    ASSERT_EQ(supervisor.phase(), SandboxPhase::Ready) << "faultReason=" << faultReason.toStdString();
    EXPECT_GT(supervisor.processId(), 0) << "子进程应该已经真正启动";

    // 2) run()
    supervisor.run();
    ASSERT_TRUE(waitUntil([&] { return supervisor.phase() == SandboxPhase::Running; }))
        << "等待沙箱进入 Running 阶段超时";

    // 3) 通过共享内存执行一次 sumFloats 命令：3 个 float 相加。
    const std::vector<float> inputValues = {1.5f, 2.5f, 4.0f};
    QByteArray inputBytes(reinterpret_cast<const char *>(inputValues.data()),
                         static_cast<qsizetype>(inputValues.size() * sizeof(float)));

    bool commandDone = false;
    bool commandOk    = false;
    QByteArray commandResult;
    QString commandError;
    QObject::connect(&supervisor, &SandboxSupervisor::commandFinished, &supervisor,
                     [&](const QString &, bool ok, const QByteArray &result, const QString &error) {
                         commandDone   = true;
                         commandOk     = ok;
                         commandResult = result;
                         commandError  = error;
                     });

    const QString requestId =
        supervisor.beginCommand(QStringLiteral("com.bakuon.example.sumFloats"), inputBytes,
                                /*resultCapacity=*/sizeof(float));
    ASSERT_FALSE(requestId.isEmpty()) << "beginCommand 应该成功分配共享内存并返回 requestId";

    ASSERT_TRUE(waitUntil([&] { return commandDone; })) << "等待 commandFinished 信号超时";
    ASSERT_TRUE(commandOk) << "命令执行失败：" << commandError.toStdString();
    ASSERT_EQ(commandResult.size(), static_cast<qsizetype>(sizeof(float)));

    float resultValue = 0.0f;
    std::memcpy(&resultValue, commandResult.constData(), sizeof(float));
    EXPECT_FLOAT_EQ(resultValue, 8.0f);

    // 4) 优雅关闭：shutdown() 之后子进程应当真正退出（processFinished 触发）。
    bool processExited = false;
    QObject::connect(&supervisor, &SandboxSupervisor::processFinished, &supervisor,
                     [&](int) { processExited = true; });
    supervisor.shutdown();
    ASSERT_TRUE(waitUntil([&] { return processExited; }, 5000)) << "等待子进程退出超时";
}

TEST(SandboxSupervisorIntegrationTest, UnknownCommandIdReportsFailureNotCrash)
{
    Q_UNUSED(app());

    SandboxSupervisor supervisor(QStringLiteral("it-sandbox-2"),
                                 QString::fromLatin1(BAKUON_TEST_SANDBOXED_EXAMPLE_PLUGIN_PATH));
    supervisor.start(QString::fromLatin1(BAKUON_TEST_SANDBOX_RUNTIME_PATH));

    ASSERT_TRUE(waitUntil([&] { return supervisor.phase() == SandboxPhase::Ready; }));
    supervisor.run();
    ASSERT_TRUE(waitUntil([&] { return supervisor.phase() == SandboxPhase::Running; }));

    bool commandDone = false;
    bool commandOk    = true;
    QObject::connect(&supervisor, &SandboxSupervisor::commandFinished, &supervisor,
                     [&](const QString &, bool ok, const QByteArray &, const QString &) {
                         commandDone = true;
                         commandOk   = ok;
                     });

    const QString requestId =
        supervisor.beginCommand(QStringLiteral("com.bakuon.does.not.exist"), QByteArray(), 4);
    ASSERT_FALSE(requestId.isEmpty());

    ASSERT_TRUE(waitUntil([&] { return commandDone; }));
    EXPECT_FALSE(commandOk) << "未注册的 commandId 应该报告失败，而不是让沙箱子进程崩溃";

    // shutdown() 本身是异步、不阻塞的（见 SandboxSupervisor::shutdown() 文档），
    // 真正权威的"沙箱已经退出"信号是 processFinished，不是 phase() 属性同步——
    // 子进程在把最后一次 phase=Stopped 属性变更真正刷到 socket 之前就可能已经调用
    // QCoreApplication::quit() 退出，phase() 不保证能在 Host 侧观察到 Stopped，
    // 但 processFinished 是 QProcess 自己的退出事件，一定会触发。
    bool processExited = false;
    QObject::connect(&supervisor, &SandboxSupervisor::processFinished, &supervisor,
                     [&](int) { processExited = true; });
    supervisor.shutdown();
    ASSERT_TRUE(waitUntil([&] { return processExited; }, 3000)) << "等待子进程退出超时";
}
