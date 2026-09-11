#pragma once

#include <QtGui/QImage>
#include <QtWidgets/QWidget>

#include <sandbox/b_tabsandboxmanager.h>

class QLabel;
class QPlainTextEdit;
class QPushButton;

namespace bakuon::host {

/**
 * @brief 跨进程 GUI 合成的真实显示表面：把 TabSandboxManager::tabFrameReady()
 * 送来的 QImage 拼贴到自己持有的完整画面缓冲上，并把这块区域收到的鼠标/键盘
 * 事件转发回沙箱进程。
 *
 * setFrame() 收到的 image 通常只是脏矩形那一小块（Sandbox 侧做了变化检测 +
 * 真脏矩形裁剪，见 b_sandboxruntime.cpp 里 captureAndSendFrame() 的说明），
 * 不是每次都传整张画面——本类内部用 QPainter 把它按 dirtyRect 的位置贴回持有的
 * 完整画面（m_frame），paintEvent() 画的是这份持续累积出来的完整画面，
 * 不是某一次收到的局部小图。
 *
 * 仍然保留的已知限制：固定尺寸（480x360，和 SandboxRuntime 里
 * kSurfaceWidth/kSurfaceHeight 一致，没有做运行期协商），按原样绘制不做缩放。
 */
class GuiSurfaceView final : public QWidget
{
    Q_OBJECT
public:
    explicit GuiSurfaceView(QWidget *parent = nullptr);

    void setFrame(const QImage &image, const QRect &dirtyRect);

Q_SIGNALS:
    /// type/button/modifiers/key 的取值约定见 sandbox::GuiInputEventType 等
    /// （b_guisurfaceevents.h）——这里不直接用 Qt 的枚举，转换在 .cpp 里做一次，
    /// 让"该用哪套取值约定"这件事只在一个地方决定。
    void inputEvent(int type, QPoint pos, int button, int modifiers, int key, QString text);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void keyReleaseEvent(QKeyEvent *event) override;

private:
    QImage m_frame;
};

/**
 * @brief 单个 Tab 的内容视图：状态诊断信息（tabId/sandboxId/状态/日志） +
 * 真实的跨进程 GUI 合成显示表面（GuiSurfaceView）。
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
    void setFrame(const QImage &image, const QRect &dirtyRect);

Q_SIGNALS:
    void restartRequested(uint64_t tabId);
    /// 转发自内部 GuiSurfaceView::inputEvent()，带上 tabId 方便 MainWindow
    /// 直接转给 TabSandboxManager::dispatchInputEvent(tabId, ...)，不需要
    /// MainWindow 自己再去反查"这个信号是哪个 Tab 发出来的"。
    void inputEvent(uint64_t tabId, int type, QPoint pos, int button, int modifiers, int key,
                    QString text);

private:
    uint64_t m_tabId;
    QLabel *m_stateLabel         = nullptr;
    QLabel *m_sandboxIdLabel     = nullptr;
    QPlainTextEdit *m_log        = nullptr;
    QPushButton *m_restartButton = nullptr;
    GuiSurfaceView *m_surface    = nullptr;
};

} // namespace bakuon::host
