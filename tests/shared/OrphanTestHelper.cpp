#include <QtCore/QCoreApplication>
#include <QtCore/QTextStream>

#include <sandbox/b_tabsandboxmanager.h>

// 专用于 tabsandboxmanager_orphan_test.cpp / tabsandboxmanager_session_test.cpp 的
// 一次性测试辅助进程，不是产品代码。
//
// 目的：真实模拟"Host 崩溃"——本进程扮演"第一代 Host"，打开一个 Tab、等它
// Running，打印 READY 后原地阻塞。测试用例用 QProcess::kill()（SIGKILL）杀掉
// 本进程，不给任何析构函数运行的机会，这样它 spawn() 出来的 sandbox_runtime
// 子进程会真正变成孤儿（不会被 SandboxSupervisor 析构函数里的优雅关闭逻辑带走），
// 之后测试进程自己创建的第二个 TabSandboxManager 才有机会去发现/收编这个孤儿。
//
// 用法：orphan_test_helper <sandboxRuntimeExecutable> <sandboxedExamplePluginPath> [sessionFilePath]
// 第四个参数可选：给了就会 setSessionFilePath() + restoreSession()，用于验证
// "会话持久化 + 崩溃 + 原地恢复"这条链路（见 tabsandboxmanager_session_test.cpp）；
// 不给就是原来纯孤儿收编测试用的模式，不涉及持久化。
int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    if (argc < 3) {
        return 1;
    }

    bakuon::sandbox::TabSandboxManager manager(QString::fromLocal8Bit(argv[1]));
    if (argc >= 4) {
        manager.setSessionFilePath(QString::fromLocal8Bit(argv[3]));
        manager.restoreSession();
    }
    const auto tabId = manager.openTab(QString::fromLocal8Bit(argv[2]));
    if (tabId == 0) {
        return 2; // openTab() 失败（0 是 openTab() 文档里明确的"无效 tabId"哨兵值）
    }

    QObject::connect(&manager,
                     &bakuon::sandbox::TabSandboxManager::tabRunning,
                     &manager,
                     [&](auto id) {
                         if (id == tabId) {
                             // 把 tabId 一并打印出来，方便验证"崩溃前后 tabId 是否一致"
                             // （tabsandboxmanager_session_test.cpp 需要这个）。
                             QTextStream(stdout) << "READY " << id << "\n";
                             std::fflush(stdout);
                         }
                     });

    return app.exec(); // 一直跑到被外部 kill()，故意不设超时退出
}
