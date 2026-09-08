#include "MainWindow.h"

#include <QtCore/QDir>
#include <QtCore/QFileInfo>
#include <QtCore/QTimer>
#include <QtWidgets/QInputDialog>
#include <QtWidgets/QMessageBox>
#include <QtWidgets/QStatusBar>
#include <QtWidgets/QTabWidget>

#include "Constants.h"
#include "TabContentWidget.h"

namespace bakuon::host {

namespace {

// 与 standalone/main.cpp 里同名 helper 的写法/意图完全一致，这里是不同可执行文件、
// 目前没有共享 util 头，按项目里"小 helper 直接各自拷一份"的既有惯例处理。
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

// 扫描插件目录，找出候选的沙箱插件文件——按"去掉平台相关后缀"归一化去重
// （同一个插件在同一目录下只会有一种后缀，这里的去重主要是防御性的）。
QStringList discoverPluginBaseNames(const QString &pluginsDir)
{
    if (pluginsDir.isEmpty()) {
        return {};
    }
    QDir dir(pluginsDir);
    QStringList baseNames;
    for (const QFileInfo &info : dir.entryInfoList(QDir::Files)) {
        QString base = info.fileName();
        for (const QString &suffix :
             {QStringLiteral(".dll"), QStringLiteral(".so"), QStringLiteral(".dylib")}) {
            if (base.endsWith(suffix)) {
                base.chop(suffix.length());
                break;
            }
        }
        if (!baseNames.contains(base)) {
            baseNames.push_back(base);
        }
    }
    return baseNames;
}

} // namespace

MainWindow::MainWindow(QString pluginsDir, QString sandboxRuntimeExecutable,
                       QString sessionFilePath, QWidget *parent)
    : QMainWindow(parent)
    , m_pluginsDir(std::move(pluginsDir))
    , m_sandboxRuntimeExecutable(sandboxRuntimeExecutable)
    , m_tabManager(std::move(sandboxRuntimeExecutable))
{
    setWindowTitle(QStringLiteral("bakuon"));

    m_tabs = new QTabWidget(this);
    m_tabs->setTabsClosable(true);
    m_tabs->setMovable(true);
    connect(m_tabs, &QTabWidget::tabCloseRequested, this, [this](int index) {
        auto *w = qobject_cast<TabContentWidget *>(m_tabs->widget(index));
        if (w) {
            m_tabManager.closeTab(w->tabId());
        }
    });
    setCentralWidget(m_tabs);

    connect(&m_tabManager, &sandbox::TabSandboxManager::tabQueued, this, &MainWindow::onTabQueued);
    connect(&m_tabManager,
            &sandbox::TabSandboxManager::tabLaunching,
            this,
            &MainWindow::onTabLaunching);
    connect(&m_tabManager, &sandbox::TabSandboxManager::tabRunning, this, &MainWindow::onTabRunning);
    connect(&m_tabManager, &sandbox::TabSandboxManager::tabFaulted, this, &MainWindow::onTabFaulted);
    connect(&m_tabManager, &sandbox::TabSandboxManager::tabClosed, this, &MainWindow::onTabClosed);
    connect(&m_tabManager,
            &sandbox::TabSandboxManager::tabLogMessage,
            this,
            &MainWindow::onTabLogMessage);
    connect(&m_tabManager,
            &sandbox::TabSandboxManager::tabRestoring,
            this,
            &MainWindow::onTabRestoring);
    connect(&m_tabManager,
            &sandbox::TabSandboxManager::tabRestored,
            this,
            &MainWindow::onTabRestored);
    connect(&m_tabManager, &sandbox::TabSandboxManager::tabAdopted, this, &MainWindow::onTabAdopted);
    connect(&m_tabManager, &sandbox::TabSandboxManager::orphanSandboxAvailable, this, [this]() {
        qDebug() << "adopted: " << m_tabManager.tryAdoptOrphanedSandboxes();
    });
    connect(&m_tabManager,
            &sandbox::TabSandboxManager::tabFrameReady,
            this,
            &MainWindow::onTabFrameReady);

    registerCommands();
    buildMenuAndToolBar();

    if (!sessionFilePath.isEmpty()) {
        m_tabManager.setSessionFilePath(std::move(sessionFilePath));
    }
    restoreSessionIfAny();

    gui::CommandSystem::pushContext(kCtxGlobal, this);
    statusBar()->showMessage(QStringLiteral(
                                 "文件(&F) -> 新建标签…，每个标签对应一个独立的沙箱子进程"),
                             5000);
}

MainWindow::~MainWindow()
{
    gui::CommandSystem::releaseContext(this);
}

void MainWindow::registerCommands()
{
    auto &newTab = gui::CommandSystem::registerCommand(kCmdNewTab, QStringLiteral("新建标签…"));
    newTab.setShortcut(QKeySequence::New);
    newTab.setAttribute(gui::Command::Attribute::UpdateText, false);
    auto *newTabAction = new QAction(this);
    connect(newTabAction, &QAction::triggered, this, &MainWindow::newTab);

    auto &closeTab = gui::CommandSystem::registerCommand(kCmdCloseTab, QStringLiteral("关闭标签"));
    closeTab.setShortcut(QKeySequence::Close);
    closeTab.setAttribute(gui::Command::Attribute::UpdateText, false);
    auto *closeTabAction = new QAction(this);
    connect(closeTabAction, &QAction::triggered, this, &MainWindow::closeCurrentTab);

    auto &restartTabCmd = gui::CommandSystem::registerCommand(kCmdRestartTab,
                                                              QStringLiteral("重启当前标签"));
    restartTabCmd.setAttribute(gui::Command::Attribute::UpdateText, false);
    auto *restartTabAction = new QAction(this);
    connect(restartTabAction, &QAction::triggered, this, &MainWindow::restartCurrentTab);

    auto &quit = gui::CommandSystem::registerCommand(kCmdQuit, QStringLiteral("退出"));
    quit.setShortcut(QKeySequence::Quit);
    quit.setAttribute(gui::Command::Attribute::UpdateText, false);
    auto *quitAction = new QAction(this);
    connect(quitAction, &QAction::triggered, this, &QMainWindow::close);

    gui::CommandSystem::context(kCtxGlobal)->addAction(newTab.id(), newTabAction);
    gui::CommandSystem::context(kCtxGlobal)->addAction(closeTab.id(), closeTabAction);
    gui::CommandSystem::context(kCtxGlobal)->addAction(restartTabCmd.id(), restartTabAction);
    gui::CommandSystem::context(kCtxGlobal)->addAction(quit.id(), quitAction);
}

void MainWindow::buildMenuAndToolBar()
{
    auto fileMenu = m_menuLayout.addContainer(QStringLiteral("文件(&F)"));
    m_menuLayout.addCommand(kCmdNewTab.toString(), fileMenu);
    m_menuLayout.addCommand(kCmdCloseTab.toString(), fileMenu);
    m_menuLayout.addCommand(kCmdRestartTab.toString(), fileMenu);
    m_menuLayout.addSeparator(fileMenu);
    m_menuLayout.addCommand(kCmdQuit.toString(), fileMenu);

    gui::CommandSystem::renderMenuBar(&m_menuLayout, menuBar());

    auto *toolBar = addToolBar(QStringLiteral("常用操作"));
    toolBar->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    toolBar->addAction(gui::CommandSystem::command(kCmdNewTab)->action());
    toolBar->addAction(gui::CommandSystem::command(kCmdCloseTab)->action());
    toolBar->addAction(gui::CommandSystem::command(kCmdRestartTab)->action());
}

void MainWindow::restoreSessionIfAny()
{
    if (m_tabManager.sessionFilePath().isEmpty()) {
        return;
    }
    const int restored = m_tabManager.restoreSession();
    if (restored <= 0) {
        return;
    }
    statusBar()->showMessage(QStringLiteral("正在尝试恢复上一次会话的 %1 个标签…").arg(restored),
                             5000);

    // Host 层的"等多久放弃"策略：TabSandboxManager 本身不内置超时，这里给一个 5 秒
    // 的宽限期，时间到了还停留在 Restoring 就主动放弃等待、重新 spawn()（对应
    // standalone/main.cpp 里演示过的同一套策略，这里是它在真实 GUI 里的对应实现）。
    QTimer::singleShot(5000, this, [this] {
        if (m_tabManager.pendingRestoreCount() == 0) {
            return;
        }
        for (const uint64_t tabId : m_tabManager.tabIds()) {
            if (m_tabManager.tabState(tabId) == sandbox::TabState::Restoring) {
                m_tabManager.respawnRestoredTab(tabId);
            }
        }
    });
}

void MainWindow::newTab()
{
    const QStringList baseNames = discoverPluginBaseNames(m_pluginsDir);
    if (baseNames.isEmpty()) {
        QMessageBox::warning(this,
                             QStringLiteral("新建标签"),
                             QStringLiteral("在插件目录里没有找到任何可加载的沙箱插件：\n%1")
                                 .arg(m_pluginsDir));
        return;
    }

    bool ok              = false;
    const QString choice = baseNames.size() == 1
                               ? baseNames.front()
                               : QInputDialog::getItem(this,
                                                       QStringLiteral("新建标签"),
                                                       QStringLiteral(
                                                           "选择要在新标签里加载的插件："),
                                                       baseNames,
                                                       0,
                                                       /*editable=*/false,
                                                       &ok);
    if (baseNames.size() > 1 && !ok) {
        return; // 用户取消了选择
    }

    const QString pluginFile = firstExistingFile(m_pluginsDir, {choice});
    if (pluginFile.isEmpty()) {
        QMessageBox::warning(this,
                             QStringLiteral("新建标签"),
                             QStringLiteral("找不到插件文件：%1").arg(choice));
        return;
    }

    const uint64_t tabId = m_tabManager.openTab(pluginFile);
    if (tabId == 0) {
        QMessageBox::warning(this,
                             QStringLiteral("新建标签"),
                             QStringLiteral("openTab() 失败——检查是否配置了 sandbox_runtime 路径"));
    }
}

void MainWindow::closeCurrentTab()
{
    auto *w = qobject_cast<TabContentWidget *>(m_tabs->currentWidget());
    if (w) {
        m_tabManager.closeTab(w->tabId());
    }
}

void MainWindow::restartCurrentTab()
{
    auto *w = qobject_cast<TabContentWidget *>(m_tabs->currentWidget());
    if (w) {
        restartTab(w->tabId());
    }
}

void MainWindow::restartTab(uint64_t tabId)
{
    // respawnRestoredTab() 就是 restartTab() 的别名（见 TabSandboxManager 文档），
    // 对 Faulted/Restoring 两种场景都适用，UI 侧不需要关心底层具体是哪一种，
    // 统一调 restartTab() 即可。
    m_tabManager.restartTab(tabId);
}

TabContentWidget *MainWindow::addPlaceholderTab(uint64_t tabId, const QString &title)
{
    auto *widget = new TabContentWidget(tabId, m_tabs);
    connect(widget, &TabContentWidget::restartRequested, this, &MainWindow::restartTab);
    connect(widget,
            &TabContentWidget::inputEvent,
            this,
            [this](uint64_t id,
                   int type,
                   QPoint pos,
                   int button,
                   int modifiers,
                   int key,
                   QString text) {
                m_tabManager.dispatchInputEvent(id, type, pos, button, modifiers, key, text);
            });
    m_tabs->addTab(widget, title);
    return widget;
}

TabContentWidget *MainWindow::widgetForTab(uint64_t tabId) const
{
    const int index = pageIndexForTab(tabId);
    return index < 0 ? nullptr : qobject_cast<TabContentWidget *>(m_tabs->widget(index));
}

int MainWindow::pageIndexForTab(uint64_t tabId) const
{
    for (int i = 0; i < m_tabs->count(); ++i) {
        auto *w = qobject_cast<TabContentWidget *>(m_tabs->widget(i));
        if (w && w->tabId() == tabId) {
            return i;
        }
    }
    return -1;
}

void MainWindow::removeTabPage(uint64_t tabId)
{
    const int index = pageIndexForTab(tabId);
    if (index >= 0) {
        QWidget *w = m_tabs->widget(index);
        m_tabs->removeTab(index);
        w->deleteLater();
    }
}

void MainWindow::onTabQueued(uint64_t tabId)
{
    auto *w = addPlaceholderTab(tabId, QStringLiteral("Tab %1 (排队中)").arg(tabId));
    w->setState(sandbox::TabState::Queued);
}

void MainWindow::onTabLaunching(uint64_t tabId)
{
    TabContentWidget *w = widgetForTab(tabId);
    if (!w) {
        w = addPlaceholderTab(tabId, QStringLiteral("Tab %1").arg(tabId));
    }
    w->setState(sandbox::TabState::Launching);
    w->setSandboxId(m_tabManager.sandboxIdForTab(tabId));
    m_tabs->setCurrentWidget(w);
    m_tabs->setTabText(m_tabs->indexOf(w), QStringLiteral("Tab %1").arg(tabId));
}

void MainWindow::onTabRunning(uint64_t tabId)
{
    if (auto *w = widgetForTab(tabId)) {
        w->setState(sandbox::TabState::Running);
    }
}

void MainWindow::onTabFaulted(uint64_t tabId, const QString &reason)
{
    if (auto *w = widgetForTab(tabId)) {
        w->setState(sandbox::TabState::Faulted);
        w->appendLogLine(2 /*Warning*/, QStringLiteral("异常：%1").arg(reason));
        m_tabs->setTabText(m_tabs->indexOf(w), QStringLiteral("Tab %1 (异常)").arg(tabId));
    }
    statusBar()->showMessage(QStringLiteral("Tab %1 出现异常：%2").arg(tabId).arg(reason), 5000);
}

void MainWindow::onTabClosed(uint64_t tabId)
{
    removeTabPage(tabId);
}

void MainWindow::onTabLogMessage(uint64_t tabId, int level, const QString &message)
{
    if (auto *w = widgetForTab(tabId)) {
        w->appendLogLine(level, message);
    }
}

void MainWindow::onTabRestoring(uint64_t tabId)
{
    qDebug() << Q_FUNC_INFO;
    auto *w = addPlaceholderTab(tabId, QStringLiteral("Tab %1 (恢复中…)").arg(tabId));
    w->setState(sandbox::TabState::Restoring);
    if (const auto session = m_tabManager.sessionForTab(tabId)) {
        w->setSandboxId(session->sandboxId);
    }
}

void MainWindow::onTabRestored(uint64_t tabId, const QString &sandboxId)
{
    if (auto *w = widgetForTab(tabId)) {
        w->setState(sandbox::TabState::Launching);
        w->setSandboxId(sandboxId);
        m_tabs->setTabText(m_tabs->indexOf(w), QStringLiteral("Tab %1 (已恢复)").arg(tabId));
    }
    statusBar()->showMessage(QStringLiteral("Tab %1 原地恢复成功").arg(tabId), 5000);
}

void MainWindow::onTabAdopted(uint64_t tabId, const QString &sandboxId)
{
    auto *w = widgetForTab(tabId);
    if (!w) {
        w = addPlaceholderTab(tabId, QStringLiteral("Tab %1 (恢复的孤儿)").arg(tabId));
    }
    w->setState(sandbox::TabState::Launching);
    w->setSandboxId(sandboxId);
    w->appendLogLine(1 /*Info*/,
                     QStringLiteral("这是一个收编回来的陌生沙箱进程，原始文档信息不可用"));
    statusBar()->showMessage(QStringLiteral("发现了一个来路不明的沙箱进程，已作为新标签 %1 收编")
                                 .arg(tabId),
                             5000);
}

void MainWindow::onTabFrameReady(uint64_t tabId, const QImage &image, const QRect &dirtyRect)
{
    if (auto *w = widgetForTab(tabId)) {
        w->setFrame(image, dirtyRect);
    }
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    m_tabManager.closeAll();
    QMainWindow::closeEvent(event);
}

} // namespace bakuon::host
