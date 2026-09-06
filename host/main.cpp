#include <QtCore/QCommandLineParser>
#include <QtCore/QDir>
#include <QtCore/QFileInfo>
#include <QtWidgets/QApplication>

#include <gui/b_contextfocusrouter.h>
#include <sandbox/b_tabsandboxmanager.h>

#include "MainWindow.h"

using namespace bakuon::host;

namespace {

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

} // namespace

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setOrganizationName("bakuon");
    QApplication::setApplicationName(QStringLiteral("bakuon"));
    QApplication::setApplicationVersion(QStringLiteral("0.1.0"));

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("bakuon GUI 主程序：单窗口多标签，一个标签一个沙箱子进程。"));
    parser.addHelpOption();
    parser.addVersionOption();

    QCommandLineOption pluginsDirOption(QStringList{QStringLiteral("plugins-dir")},
                                        QStringLiteral("插件扫描目录（默认自动探测 plugins/gui）"),
                                        QStringLiteral("path"));
    parser.addOption(pluginsDirOption);

    QCommandLineOption
        sessionFileOption(QStringList{QStringLiteral("session-file")},
                          QStringLiteral(
                              "启用 Tab 会话持久化（原地恢复），指向记录文件路径；"
                              "不指定则使用 TabSandboxManager::defaultSessionFilePath()。"),
                          QStringLiteral("path"));
    parser.addOption(sessionFileOption);

    QCommandLineOption noSessionOption(QStringList{QStringLiteral("no-session")},
                                       QStringLiteral(
                                           "完全关闭会话持久化（既不读也不写），用于对比调试。"));
    parser.addOption(noSessionOption);

    parser.process(app);

    const QString appDir = QCoreApplication::applicationDirPath();
    QString pluginsDir   = parser.value(pluginsDirOption);
    if (pluginsDir.isEmpty()) {
        // 两种候选布局，和 standalone/main.cpp 里的说明一致：构建树内运行
        // （<build>/bin/ 和 <build>/plugins/gui/）或假想的安装布局（同目录子文件夹）。
        pluginsDir = firstExistingDir({QDir(appDir).filePath(QStringLiteral("../plugins/gui")),
                                       QDir(appDir).filePath(QStringLiteral("plugins/gui"))});
    }

    const QString sandboxRuntimeExe = firstExistingFile(appDir, {sandboxRuntimeExecutableName()});

    QString sessionFilePath;
    if (!parser.isSet(noSessionOption)) {
        sessionFilePath = parser.value(sessionFileOption);
        if (sessionFilePath.isEmpty()) {
            sessionFilePath = bakuon::sandbox::TabSandboxManager::defaultSessionFilePath();
        }
    }

    auto *router = new bakuon::gui::ContextFocusRouter(&app);
    router->install();

    MainWindow window(pluginsDir, sandboxRuntimeExe, sessionFilePath);
    window.show();
    window.resize(900, 600);

    return app.exec();
}
