#include <QtCore/QCommandLineOption>
#include <QtCore/QCommandLineParser>
#include <QtCore/QDebug>
#include <QtCore/QUrl>
#include <QtGui/QFontDatabase>
#include <QtWidgets/QApplication>

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
// 用 QApplication（-platform offscreen）而不是 QCoreApplication：跨进程 GUI
// 合成方案落地后，沙箱进程需要能够构造/绘制真正的 QWidget（IGuiSurfaceHandler
// 返回的那个），只是永远不 show() 它——SandboxRuntime 定期把它 grab() 成像素、
// 写进共享内存，"真正显示"发生在 Host 进程侧。QApplication 是能创建 QWidget
// 的最低要求（QCoreApplication 完全没有 QPA 平台插件，连字体系统都用不了）；
// `-platform offscreen` 让这一切在没有真实显示器/窗口系统的环境里也能正常
// 工作（Qt 官方专门为无头渲染场景提供的平台插件，截图测试/服务端缩略图生成
// 都是靠它）。不打算做 GUI 的沙箱化插件完全不受影响——offscreen 平台下不创建
// 任何 widget 和普通 QCoreApplication 场景在运行时行为上没有区别。
// ============================================================================

int main(int argc, char *argv[])
{
#ifdef _WIN32
    // 如果是控制台应用，或者包含了控制台输出，强制控制台流为 UTF-8
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif

    // 平台名必须在 QApplication 构造之前确定；qputenv 而不是 -platform 命令行参数，
    // 是为了不占用/冲突 sandbox_runtime 自己已经在用的 --sandbox-listen 等参数。
    qputenv("QT_QPA_PLATFORM", "offscreen");

#ifdef _WIN32
    // offscreen 需要指定字体库， Qt6 已经移除自带的字体库。
    qputenv("QT_QPA_FONTDIR", "C:\\Windows\\Fonts");
#endif

    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("bakuon_sandbox_runtime"));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("bakuon 插件沙箱子进程"));
    parser.addHelpOption();

    auto nameOf = [](const char *flag) {
        return QString::fromLatin1(flag + 2); // 去掉前导 "--"
    };
    QCommandLineOption listenOption(nameOf(bakuon::sandbox::cli::kListen),
                                    QStringLiteral("本进程应监听的本地 QtRO 地址（local: scheme）"),
                                    QStringLiteral("url"));
    QCommandLineOption registryOption(
        nameOf(bakuon::sandbox::cli::kRegistry),
        QStringLiteral("注册中心（QRemoteObjectRegistryHost）地址，见 registryUrl()"),
        QStringLiteral("url"),
        bakuon::sandbox::registryUrl()); // 默认值：与 Host 主程序里创建注册中心时用的地址一致
    QCommandLineOption
        sandboxIdOption(nameOf(bakuon::sandbox::cli::kSandboxId),
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
    if (!parser.isSet(registryOption)) {
        qCritical() << "缺少必需参数" << bakuon::sandbox::cli::kRegistry;
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
