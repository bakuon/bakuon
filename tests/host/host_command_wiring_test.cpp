#include <gtest/gtest.h>

#include <QApplication>
#include <QEventLoop>
#include <QFile>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTimer>

#include <gui/b_commandsystem.h>

#include "../../host/Constants.h"
#include "../../host/MainWindow.h"

using namespace bakuon;
using namespace bakuon::host;

namespace {

// QApplication（不是 QCoreApplication）：MainWindow 是真实的 QMainWindow，
// 需要完整的 QtWidgets 支持。offscreen 平台由外部统一通过 QT_QPA_PLATFORM 环境
// 变量指定（见 CI/本地跑法），这里不写死。
QApplication &app()
{
    // 在 Linux 系统下使用 QApplication 必须使用配置 QT_QPA_PLATFORM=offscreen
#ifdef Q_OS_LINUX
    qputenv("QT_QPA_PLATFORM", "offscreen");
#endif

    static int argc     = 1;
    static char argv0[] = "test_host_command_wiring";
    static char *argv[] = {argv0, nullptr};
    static QApplication instance(argc, argv);
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

// 跑一小段时间、什么都不等待，纯粹用来给"会不会持续冒出新东西"这类断言留出
// 观察窗口——和 waitUntil() 不同，这里没有成功条件，只是老实地把事件循环晾一会。
void spinFor(int ms)
{
    QEventLoop loop;
    QTimer::singleShot(ms, &loop, &QEventLoop::quit);
    loop.exec();
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

/**
 * @brief 回归测试：触发一次"新建标签"命令，只应该真正打开一个标签，不应该失控
 *        连续打开一串。
 *
 * 这条测试的存在直接对应一个手工验证时抓到的真实 bug：MainWindow::registerCommands()
 * 曾经把 Command::action()（代理 QAction）错当成"真实 QAction"直接注册进
 * ContextArbiter——Command::setRealAction() 因此把代理设成了自己的权威源，
 * syncCurrentState() 连出 `connect(代理, triggered, 代理, [代理]{ 代理->trigger(); })`
 * 这样一条自反馈连接，代理一触发就自己再触发一次，同步递归、一次点击拉出十几个
 * sandbox_runtime 子进程。这类 bug 因为"编译期完全看不出来、且只在真正触发一次
 * 命令后才会暴露"，非常容易在未来的某次重构里被无意间重新引入（比如又手滑把
 * action() 的返回值存进了本该保存"真实 action"的成员变量），所以专门写一个自动化
 * 回归测试钉住，而不是只在手工验证时抓一次就算了。
 */
TEST(HostCommandWiringTest, TriggeringNewTabOnceOpensExactlyOneTab)
{
    QApplication &a = app();
    Q_UNUSED(a);

    QTemporaryDir pluginsDir;
    ASSERT_TRUE(pluginsDir.isValid());
    // 只放一个候选插件文件，绕开 QInputDialog::getItem()——那是个模态对话框，
    // 在无显示环境下会阻塞事件循环，测试无从继续。
    const QString pluginCopyPath = pluginsDir.filePath(
        QStringLiteral("sandboxed_example_plugin.so"));
    ASSERT_TRUE(QFile::copy(sandboxedExamplePluginPath(), pluginCopyPath));

    MainWindow window(pluginsDir.path(), sandboxRuntimePath(), QString());

    auto *cmd = gui::CommandSystem::command(kCmdNewTab);
    ASSERT_NE(cmd, nullptr);

    cmd->action()->trigger();

    auto *tabs = qobject_cast<QTabWidget *>(window.centralWidget());
    ASSERT_NE(tabs, nullptr);

    ASSERT_TRUE(waitUntil([&] { return tabs->count() >= 1; }))
        << "触发一次新建标签之后，等不到任何标签页出现";
    // 给失控循环留出充分的观察窗口——真实 bug 复现时，1 秒内能看到十几个标签，
    // 这里 800ms 完全足够让失控现象暴露出来。
    spinFor(800);
    EXPECT_EQ(tabs->count(), 1) << "一次命令触发不应该开出不止一个标签（失控循环回归）";
}
