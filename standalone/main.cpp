#include <QtCore/QCommandLineParser>
#include <QtCore/QCoreApplication>
#include <QtCore/QDebug>
#include <QtCore/QDir>
#include <QtCore/QFileInfo>
#include <QtCore/QLoggingCategory>
#include <QtCore/QTimer>

#include "gui/b_pluginmetadata.h"
#include "gui/b_pluginpipeline.h"
#include "gui/b_pluginsystem.h"

#if defined(BAKUON_STANDALONE_HAVE_SANDBOX)
#include "sandbox/b_sandboxsupervisor.h"
#include "sandbox/b_sandboxsystem.h"
#endif

namespace {

Q_LOGGING_CATEGORY(lcStandalone, "bakuon.standalone")

/**
 * @brief standalone 目前是"最小验证版本"：QCoreApplication + 无窗口，对应项目目标里的
 * "无窗口命令行启动"。它不创建任何 QWidget/QAction，只负责把宿主两条腿——
 * 进程内插件（gui::PluginSystem）和进程外沙箱插件（sandbox::SandboxSystem）——
 * 都跑起来一次，验证 gui 改成 SHARED 库之后插件/沙箱子进程能不能正确动态链接、
 * 加载、运行。真正的 GUI 外壳（菜单/工具栏/标签页窗口）留给后续版本。
 */

/// 在候选目录列表里找第一个存在的目录；候选目录本身来自构建/安装两种可能的产物布局。
QString firstExistingDir(const QStringList &candidates)
{
    for (const QString &candidate : candidates) {
        QFileInfo info(candidate);
        if (info.isDir()) {
            return info.absoluteFilePath();
        }
    }
    return {};
}

/// 在指定目录下，按候选文件名（不同平台的动态库后缀不同）找第一个存在的文件。
QString firstExistingFile(const QString &directory, const QStringList &baseNames)
{
    if (directory.isEmpty()) {
        return {};
    }
    QDir dir(directory);
    const QStringList suffixes = {QStringLiteral(""),
                                  QStringLiteral(".dll"),
                                  QStringLiteral(".so"),
                                  QStringLiteral(".dylib")};
    for (const QString &baseName : baseNames) {
        for (const QString &suffix : suffixes) {
            QFileInfo info(dir.filePath(baseName + suffix));
            if (info.isFile()) {
                return info.absoluteFilePath();
            }
        }
    }
    return {};
}

QString sandboxRuntimeExecutableName()
{
#if defined(Q_OS_WIN)
    return QStringLiteral("sandbox_runtime.exe");
#else
    return QStringLiteral("sandbox_runtime");
#endif
}

/// 把 gui::PluginSystem 的批量编排结果打成一份人类可读的诊断日志，逐个插件报告
/// 最终状态；这里只是诊断输出，不影响 startup() 本身的返回值判断。
void logPluginDiagnostics(const bakuon::gui::PluginSystem &pluginSystem)
{
    const auto pipelines = pluginSystem.pipelines();
    qCInfo(lcStandalone) << "共注册" << pipelines.size() << "个插件";
    for (const auto &pipeline : pipelines) {
        const bakuon::gui::PluginMetadata meta = pipeline->metadata();
        const QString displayName              = meta.id.isEmpty() ? pipeline->filePath() : meta.id;
        qCInfo(lcStandalone).noquote()
            << QStringLiteral("  - [%1] %2  状态=%3")
                   .arg(displayName,
                        meta.name.isEmpty() ? QStringLiteral("<未知>") : meta.name,
                        bakuon::gui::toString(pipeline->state()));
        if (pipeline->state() == bakuon::gui::PluginState::ResolveFailed
            || pipeline->state() == bakuon::gui::PluginState::LoadFailed
            || pipeline->state() == bakuon::gui::PluginState::InitializeFailed
            || pipeline->state() == bakuon::gui::PluginState::RunFailed) {
            qCWarning(lcStandalone).noquote()
                << QStringLiteral("    失败原因: %1").arg(pipeline->lastError());
        }
    }
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("bakuon-standalone"));
    QCoreApplication::setApplicationVersion(QStringLiteral("0.1.0"));

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("bakuon 无窗口主进程：加载进程内插件，并（若已构建沙箱子系统）"
                       "演示进程外沙箱插件的基础启动流程。"));
    parser.addHelpOption();
    parser.addVersionOption();

    QCommandLineOption
        pluginsDirOption(QStringList{QStringLiteral("plugins-dir")},
                         QStringLiteral(
                             "插件扫描目录（默认按构建树/安装布局自动探测 plugins/gui）"),
                         QStringLiteral("path"));
    parser.addOption(pluginsDirOption);

    QCommandLineOption keepAliveOption(QStringList{QStringLiteral("keep-alive")},
                                       QStringLiteral(
                                           "完成一次演示流程后不自动退出，保持事件循环运行"));
    parser.addOption(keepAliveOption);

    QCommandLineOption
        exitAfterOption(QStringList{QStringLiteral("exit-after-ms")},
                        QStringLiteral("自动退出前的等待毫秒数（默认 2000，配合冒烟测试/CI 使用）"),
                        QStringLiteral("ms"),
                        QStringLiteral("2000"));
    parser.addOption(exitAfterOption);

    parser.process(app);

    // ------------------------------------------------------------------
    // 第一条腿：进程内插件（gui::PluginSystem）
    // ------------------------------------------------------------------
    bakuon::gui::PluginSystem pluginSystem;
    QObject::connect(&pluginSystem,
                     &bakuon::gui::PluginSystem::pluginFailed,
                     &pluginSystem,
                     [](size_t id, bakuon::gui::PluginState failedState, const QString &reason) {
                         qCWarning(lcStandalone).noquote()
                             << QStringLiteral("插件 #%1 在 %2 阶段失败: %3")
                                    .arg(id)
                                    .arg(bakuon::gui::toString(failedState), reason);
                     });
    QObject::connect(&pluginSystem,
                     &bakuon::gui::PluginSystem::pluginRunning,
                     &pluginSystem,
                     [](size_t id) { qCInfo(lcStandalone) << "插件 #" << id << "已进入 Running"; });

    QString pluginsDir = parser.value(pluginsDirOption);
    if (pluginsDir.isEmpty()) {
        const QString appDir = QCoreApplication::applicationDirPath();
        // 两种候选布局：
        //   1) 构建树内运行：可执行文件在 <build>/bin/，插件在 <build>/plugins/gui/
        //      （见根 CMakeLists.txt 里对 CMAKE_RUNTIME_OUTPUT_DIRECTORY 的说明，以及
        //      bakuon_add_plugin() 按 CATEGORY 输出到 <build>/plugins/<CATEGORY>/）。
        //   2) 假想的安装布局：插件和可执行文件在同一目录下的 plugins/gui 子目录。
        pluginsDir = firstExistingDir({QDir(appDir).filePath(QStringLiteral("../plugins/gui")),
                                       QDir(appDir).filePath(QStringLiteral("plugins/gui"))});
    }

    if (pluginsDir.isEmpty()) {
        qCInfo(lcStandalone) << "未找到插件目录（可能 BAKUON_BUILD_PLUGINS 未开启），"
                                "跳过插件加载，可用 --plugins-dir 显式指定。";
    } else {
        qCInfo(lcStandalone).noquote() << QStringLiteral("插件扫描目录: %1").arg(pluginsDir);
        pluginSystem.registerDirectory(pluginsDir, /*recursive=*/false);
        pluginSystem.startup(); // launchAll() + runAll()
        logPluginDiagnostics(pluginSystem);
    }

#if defined(BAKUON_STANDALONE_HAVE_SANDBOX)
    // ------------------------------------------------------------------
    // 第二条腿：进程外沙箱插件（sandbox::SandboxSystem）—— 基础启动流程演示
    // ------------------------------------------------------------------
    bakuon::sandbox::SandboxSystem sandboxSystem;
    QObject::connect(&sandboxSystem,
                     &bakuon::sandbox::SandboxSystem::sandboxPhaseChanged,
                     &sandboxSystem,
                     [&sandboxSystem](const QString &sandboxId,
                                      bakuon::sandbox::SandboxPhase phase) {
                         qCInfo(lcStandalone).noquote()
                             << QStringLiteral("沙箱[%1] 阶段变化 -> %2")
                                    .arg(sandboxId, bakuon::sandbox::toString(phase));
                         // 演示流程：一旦沙箱子进程准备就绪（插件已加载完成），立即触发一次 run()，
                         // 让插件从 Initialized 进入 Running——这一步是"基础流程"里唯一需要 Host
                         // 主动驱动的动作，其余阶段迁移都由 SandboxSupervisor 自己响应子进程的事件完成。
                         if (phase == bakuon::sandbox::SandboxPhase::Ready) {
                             sandboxSystem.run(sandboxId);
                         }
                     });
    QObject::connect(&sandboxSystem,
                     &bakuon::sandbox::SandboxSystem::sandboxLogMessage,
                     &sandboxSystem,
                     [](const QString &sandboxId, int level, const QString &message) {
                         qCInfo(lcStandalone).noquote() << QStringLiteral("沙箱[%1] (level=%2) %3")
                                                               .arg(sandboxId)
                                                               .arg(level)
                                                               .arg(message);
                     });
    QObject::connect(&sandboxSystem,
                     &bakuon::sandbox::SandboxSystem::sandboxFaulted,
                     &sandboxSystem,
                     [](const QString &sandboxId, const QString &reason) {
                         qCWarning(lcStandalone).noquote()
                             << QStringLiteral("沙箱[%1] 异常: %2").arg(sandboxId, reason);
                     });
    QObject::connect(&sandboxSystem,
                     &bakuon::sandbox::SandboxSystem::sandboxProcessFinished,
                     &sandboxSystem,
                     [](const QString &sandboxId, int exitCode) {
                         qCInfo(lcStandalone)
                             << "沙箱[" << sandboxId << "] 子进程已退出，退出码=" << exitCode;
                     });

    const QString appDir              = QCoreApplication::applicationDirPath();
    // sandbox_runtime 和 standalone 一样落在统一的 bin/ 输出目录下（见根 CMakeLists.txt），
    // 因此直接在自己所在目录里找即可，不需要额外的候选路径。
    const QString sandboxRuntimeExe   = firstExistingFile(appDir, {sandboxRuntimeExecutableName()});
    // 演示用的沙箱化插件复用 plugins/gui 目录下的 sandboxed_example_plugin
    // （见 plugins/sandbox/sandboxed_example/CMakeLists.txt 里的 CATEGORY gui）。
    const QString sandboxedPluginFile = pluginsDir.isEmpty()
                                            ? QString()
                                            : firstExistingFile(pluginsDir,
                                                                {QStringLiteral(
                                                                    "sandboxed_example_plugin")});

    if (sandboxRuntimeExe.isEmpty() || sandboxedPluginFile.isEmpty()) {
        qCInfo(lcStandalone)
            << "未找到 sandbox_runtime 可执行文件或 sandboxed_example_plugin"
               "（可能 BAKUON_BUILD_SANDBOX_RUNTIME/BAKUON_BUILD_PLUGINS 未开启），"
               "跳过沙箱启动流程演示。";
    } else {
        qCInfo(lcStandalone).noquote() << QStringLiteral("沙箱子进程: %1").arg(sandboxRuntimeExe);
        qCInfo(lcStandalone).noquote() << QStringLiteral("沙箱化插件: %1").arg(sandboxedPluginFile);
        sandboxSystem.spawn(sandboxedPluginFile, sandboxRuntimeExe);
    }

    QObject::connect(&app, &QCoreApplication::aboutToQuit, &app, [&sandboxSystem, &pluginSystem]() {
        sandboxSystem.shutdownAll();
        pluginSystem.shutdown();
    });
#else
    QObject::connect(&app, &QCoreApplication::aboutToQuit, &app, [&pluginSystem]() {
        pluginSystem.shutdown();
    });
    qCInfo(lcStandalone) << "本次构建未启用 bakuon::sandbox（BAKUON_BUILD_SANDBOX=OFF），"
                            "跳过沙箱启动流程演示。";
#endif

    if (!parser.isSet(keepAliveOption)) {
        bool ok         = false;
        const int ms    = parser.value(exitAfterOption).toInt(&ok);
        const int delay = ok && ms >= 0 ? ms : 2000;
        qCInfo(lcStandalone) << "将在" << delay << "ms 后自动退出（可用 --keep-alive 关闭此行为）";
        QTimer::singleShot(delay, &app, &QCoreApplication::quit);
    }

    return app.exec();
}
