#include <QtCore/QCommandLineOption>
#include <QtCore/QCommandLineParser>
#include <QtCore/QCoreApplication>
#include <QtCore/QDebug>
#include <QtCore/QUrl>

#include "sandbox/b_sandboxconstants.h"
#include "sandbox/b_sandboxruntime.h"

#ifdef _WIN32
#include <windows.h>
#endif

// ============================================================================
// sandbox_runtime —— "插件沙箱"子进程的可执行文件入口。
//
// 本进程完全由 SandboxSupervisor（宿主主程序侧）spawn 出来，通过命令行参数
// 接收"应该在哪个本地地址上监听"（--sandbox-listen）——本进程构造
// QRemoteObjectHost 监听该地址、发布 PluginSandboxControl 契约的 Source
// 实现，宿主随后 connectToNode() 过来 acquire Replica 驱动它。
//
// 用 QCoreApplication 而不是 QApplication：沙箱进程本身不需要任何 GUI
// （即使被加载的插件本身是 GUI 插件，"渲染"发生在宿主进程侧，沙箱只负责
// 计算/数据处理——真要支持沙箱内插件也带界面，需要另外设计跨进程 GUI 呈现
// 方案，不在本次骨架范围内，留待"血肉"阶段按需扩展）。
// ============================================================================

int main(int argc, char *argv[])
{
#ifdef _WIN32
    // 如果是控制台应用，或者包含了控制台输出，强制控制台流为 UTF-8
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif

    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("bakuon_sandbox_runtime"));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("bakuon 插件沙箱子进程"));
    parser.addHelpOption();

    QCommandLineOption listenOption(QString::fromLatin1(bakuon::sandbox::cli::kListen
                                                        + 2), // 去掉前导 "--"
                                    QStringLiteral("本进程应监听的本地 QtRO 地址（local: scheme）"),
                                    QStringLiteral("url"));
    QCommandLineOption registryOption(
        QString::fromLatin1(bakuon::sandbox::cli::kRegistry + 2),
        QStringLiteral("注册中心（QRemoteObjectRegistryHost）地址，见 registryUrl()"),
        QStringLiteral("url"),
        bakuon::sandbox::registryUrl()); // 默认值：与 Host 主程序里创建注册中心时用的地址一致
    QCommandLineOption
        sandboxIdOption(QString::fromLatin1(bakuon::sandbox::cli::kSandboxId + 2),
                        QStringLiteral(
                            "宿主分配的沙箱实例 id（同时也是注册中心里对象名的一部分，必需）"),
                        QStringLiteral("id"));
    parser.addOption(listenOption);
    parser.addOption(registryOption);
    parser.addOption(sandboxIdOption);
    parser.process(app);

    if (!parser.isSet(listenOption)) {
        qCritical() << "缺少必需参数" << bakuon::sandbox::cli::kListen;
        return 1;
    }
    if (!parser.isSet(sandboxIdOption)) {
        qCritical() << "缺少必需参数" << bakuon::sandbox::cli::kSandboxId
                    << "（现在也是注册中心对象名的一部分，不再是可选诊断信息）";
        return 1;
    }

    const QUrl listenUrl(parser.value(listenOption));
    const QUrl registryUrl(parser.value(registryOption));
    const QString sandboxId = parser.value(sandboxIdOption);

    bakuon::sandbox::SandboxRuntime runtime(sandboxId, &app);
    QObject::connect(&runtime,
                     &bakuon::sandbox::SandboxRuntime::shutdownRequested,
                     &app,
                     &QCoreApplication::quit);

    if (auto err = runtime.start(listenUrl, registryUrl)) {
        qCritical() << "SandboxRuntime::start() 失败：" << *err;
        return 1;
    }

    return app.exec();
}
