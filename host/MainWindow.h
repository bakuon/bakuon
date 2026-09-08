#pragma once

#include <QtWidgets/QMainWindow>

#include <gui/b_commandlayout.h>
#include <sandbox/b_tabsandboxmanager.h>

class QTabWidget;
class QImage;

namespace bakuon::host {

class TabContentWidget;

/**
 * @brief 真正的 GUI TabHost 主窗口：单窗口多标签，一个标签对应一个沙箱子进程
 * （bakuon::sandbox::TabSandboxManager），菜单/工具栏走 bakuon::gui 的
 * CommandSystem/CommandLayout。
 *
 * 每个 Tab 的内容目前是 TabContentWidget 占位视图（状态 + 日志），不是插件真正
 * 渲染出来的画面——跨进程 GUI 合成还没有实现，见 TabContentWidget.h 顶部的说明。
 */
class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    /**
     * @param pluginsDir 扫描可用沙箱插件的目录（New Tab 时供选择）
     * @param sandboxRuntimeExecutable sandbox_runtime 可执行文件路径
     * @param sessionFilePath Tab 会话持久化文件路径；留空表示不启用（见
     *        TabSandboxManager::setSessionFilePath() 的说明）
     */
    MainWindow(QString pluginsDir, QString sandboxRuntimeExecutable, QString sessionFilePath,
               QWidget *parent = nullptr);
    ~MainWindow() override;

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    void registerCommands();
    void buildMenuAndToolBar();
    void restoreSessionIfAny();

    void newTab();
    void closeCurrentTab();
    void restartCurrentTab();
    void restartTab(uint64_t tabId);

    TabContentWidget *addPlaceholderTab(uint64_t tabId, const QString &title);
    [[nodiscard]] TabContentWidget *widgetForTab(uint64_t tabId) const;
    [[nodiscard]] int pageIndexForTab(uint64_t tabId) const;
    void removeTabPage(uint64_t tabId);

    void onTabQueued(uint64_t tabId);
    void onTabLaunching(uint64_t tabId);
    void onTabRunning(uint64_t tabId);
    void onTabFaulted(uint64_t tabId, const QString &reason);
    void onTabClosed(uint64_t tabId);
    void onTabLogMessage(uint64_t tabId, int level, const QString &message);
    void onTabRestoring(uint64_t tabId);
    void onTabRestored(uint64_t tabId, const QString &sandboxId);
    void onTabAdopted(uint64_t tabId, const QString &sandboxId);
    void onTabFrameReady(uint64_t tabId, const QImage &image, const QRect &dirtyRect);

private:
    QString m_pluginsDir;
    QString m_sandboxRuntimeExecutable;

    QTabWidget *m_tabs = nullptr;
    // 声明顺序：m_tabManager 必须在所有会被它信号回调用到的成员（m_tabs 等）*之后*
    // 声明——C++ 成员按反向声明顺序析构，这样 m_tabManager 先于它们被析构，
    // 避免析构 TabSandboxManager 内部 SandboxSystem 时同步重入信号处理函数、
    // 访问到已经析构的 m_tabs（同类问题在 TabSandboxManager 自身内部也修过一次，
    // 见 b_tabsandboxmanager.h 里对声明顺序的详细注释，这里是同一个坑在更外层
    // 又出现了一次，直接照搬同样的规避方式）。
    gui::CommandLayout m_menuLayout;

    sandbox::TabSandboxManager m_tabManager;
};

} // namespace bakuon::host
