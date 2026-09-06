#pragma once

#include <QtWidgets/QWidget>

#include <sandbox/b_tabsandboxmanager.h>

class QLabel;
class QPlainTextEdit;
class QPushButton;

namespace bakuon::host {

/**
 * @brief 单个 Tab 的占位内容视图。
 *
 * 跨进程 GUI 合成（沙箱插件真正渲染出来的像素回传到 Host 显示）还没有实现——
 * 见和 Yuri 讨论过的方案（offscreen QApplication + QSharedMemory 位图流 +
 * .rep 契约扩展帧/输入事件），已记录、待 TabHost 主线稳定后再着手。这个类现在
 * 展示的是"这个 Tab 背后的沙箱进程处于什么状态"——tabId/sandboxId/状态/日志——
 * 而不是插件真正的界面内容。等跨进程合成落地后，这里会被替换成真正接收/绘制
 * 帧数据的视图；对外接口（构造参数、tabId()）预计不需要变，方便到时候平滑替换
 * 而不用大改 MainWindow 里的调用方代码。
 */
class TabContentWidget : public QWidget
{
    Q_OBJECT
public:
    explicit TabContentWidget(uint64_t tabId, QWidget *parent = nullptr);

    [[nodiscard]] uint64_t tabId() const noexcept { return m_tabId; }

    void setState(sandbox::TabState state);
    void setSandboxId(const QString &sandboxId);
    void appendLogLine(int level, const QString &message);

Q_SIGNALS:
    void restartRequested(uint64_t tabId);

private:
    uint64_t m_tabId;
    QLabel *m_stateLabel         = nullptr;
    QLabel *m_sandboxIdLabel     = nullptr;
    QPlainTextEdit *m_log        = nullptr;
    QPushButton *m_restartButton = nullptr;
};

} // namespace bakuon::host
