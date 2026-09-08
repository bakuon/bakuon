#include "TabContentWidget.h"

#include <QtGui/QKeyEvent>
#include <QtGui/QMouseEvent>
#include <QtGui/QPainter>
#include <QtGui/QWheelEvent>
#include <QtWidgets/QLabel>
#include <QtWidgets/QPlainTextEdit>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QVBoxLayout>

#include <sandbox/b_guisurfaceevents.h>

namespace bakuon::host {

namespace {

QString tabStateString(sandbox::TabState state)
{
    switch (state) {
    case sandbox::TabState::Queued   : return QStringLiteral("Queued");
    case sandbox::TabState::Launching: return QStringLiteral("Launching");
    case sandbox::TabState::Running  : return QStringLiteral("Running");
    case sandbox::TabState::Faulted  : return QStringLiteral("Faulted");
    case sandbox::TabState::Closing  : return QStringLiteral("Closing");
    case sandbox::TabState::Restoring: return QStringLiteral("Restoring");
    default                          : break;
    }
    return QStringLiteral("<unknown TabState>");
}

int toWireMouseButton(Qt::MouseButton button)
{
    switch (button) {
    case Qt::LeftButton  : return static_cast<int>(sandbox::GuiMouseButton::Left);
    case Qt::RightButton : return static_cast<int>(sandbox::GuiMouseButton::Right);
    case Qt::MiddleButton: return static_cast<int>(sandbox::GuiMouseButton::Middle);
    default              : return static_cast<int>(sandbox::GuiMouseButton::None);
    }
}

int toWireModifiers(Qt::KeyboardModifiers modifiers)
{
    int result = static_cast<int>(sandbox::GuiKeyModifier::None);
    if (modifiers.testFlag(Qt::ShiftModifier)) {
        result |= static_cast<int>(sandbox::GuiKeyModifier::Shift);
    }
    if (modifiers.testFlag(Qt::ControlModifier)) {
        result |= static_cast<int>(sandbox::GuiKeyModifier::Ctrl);
    }
    if (modifiers.testFlag(Qt::AltModifier)) {
        result |= static_cast<int>(sandbox::GuiKeyModifier::Alt);
    }
    return result;
}

} // namespace

GuiSurfaceView::GuiSurfaceView(QWidget *parent)
    : QWidget(parent)
{
    // 和 SandboxRuntime 里 kSurfaceWidth/kSurfaceHeight 保持一致（v1 固定尺寸，
    // 没有做运行期协商，见该文件里的说明）。
    setFixedSize(480, 360);
    setFocusPolicy(Qt::StrongFocus); // 要能收到键盘事件，必须能获得焦点
    setMouseTracking(true);          // 没有按键按住也要能收到 MouseMove（用于悬停类交互）
    setCursor(Qt::ArrowCursor);
}

void GuiSurfaceView::setFrame(const QImage &image, const QRect &dirtyRect)
{
    Q_UNUSED(dirtyRect) // v1 恒等于整帧范围，这里直接整体替换，见类文档
    m_frame = image;
    update();
}

void GuiSurfaceView::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event)
    QPainter painter(this);
    if (m_frame.isNull()) {
        painter.fillRect(rect(), Qt::black);
        painter.setPen(Qt::gray);
        painter.drawText(rect(), Qt::AlignCenter, tr("等待画面…"));
        return;
    }
    painter.drawImage(0, 0, m_frame);
}

void GuiSurfaceView::mousePressEvent(QMouseEvent *event)
{
    setFocus(Qt::MouseFocusReason); // 点击即获得焦点，方便紧接着的键盘输入也能转发
    Q_EMIT inputEvent(static_cast<int>(sandbox::GuiInputEventType::MousePress),
                      event->pos(),
                      toWireMouseButton(event->button()),
                      toWireModifiers(event->modifiers()),
                      0,
                      QString());
}

void GuiSurfaceView::mouseReleaseEvent(QMouseEvent *event)
{
    Q_EMIT inputEvent(static_cast<int>(sandbox::GuiInputEventType::MouseRelease),
                      event->pos(),
                      toWireMouseButton(event->button()),
                      toWireModifiers(event->modifiers()),
                      0,
                      QString());
}

void GuiSurfaceView::mouseMoveEvent(QMouseEvent *event)
{
    Q_EMIT inputEvent(static_cast<int>(sandbox::GuiInputEventType::MouseMove),
                      event->pos(),
                      static_cast<int>(sandbox::GuiMouseButton::None),
                      toWireModifiers(event->modifiers()),
                      0,
                      QString());
}

void GuiSurfaceView::wheelEvent(QWheelEvent *event)
{
    // key 参数在 Wheel 事件里借用来传垂直滚动量（见 SandboxRuntime::dispatchInputEvent()
    // 对 Wheel 分支的解读，契约层面刻意不为 Wheel 单独加一个字段，复用现有参数）。
    Q_EMIT inputEvent(static_cast<int>(sandbox::GuiInputEventType::Wheel),
                      event->position().toPoint(),
                      static_cast<int>(sandbox::GuiMouseButton::None),
                      toWireModifiers(event->modifiers()),
                      event->angleDelta().y(),
                      QString());
}

void GuiSurfaceView::keyPressEvent(QKeyEvent *event)
{
    Q_EMIT inputEvent(static_cast<int>(sandbox::GuiInputEventType::KeyPress),
                      QPoint(),
                      static_cast<int>(sandbox::GuiMouseButton::None),
                      toWireModifiers(event->modifiers()),
                      event->key(),
                      event->text());
}

void GuiSurfaceView::keyReleaseEvent(QKeyEvent *event)
{
    Q_EMIT inputEvent(static_cast<int>(sandbox::GuiInputEventType::KeyRelease),
                      QPoint(),
                      static_cast<int>(sandbox::GuiMouseButton::None),
                      toWireModifiers(event->modifiers()),
                      event->key(),
                      event->text());
}

TabContentWidget::TabContentWidget(uint64_t tabId, QWidget *parent)
    : QWidget(parent)
    , m_tabId(tabId)
{
    auto *layout = new QVBoxLayout(this);

    m_stateLabel     = new QLabel(this);
    m_sandboxIdLabel = new QLabel(this);

    m_restartButton = new QPushButton(tr("重启该 Tab"), this);
    m_restartButton->setVisible(false); // 只在 Faulted/Restoring 时才有意义，见 setState()
    connect(m_restartButton, &QPushButton::clicked, this, [this] {
        Q_EMIT restartRequested(m_tabId);
    });

    m_surface = new GuiSurfaceView(this);
    connect(m_surface,
            &GuiSurfaceView::inputEvent,
            this,
            [this](int type, QPoint pos, int button, int modifiers, int key, QString text) {
                Q_EMIT inputEvent(m_tabId, type, pos, button, modifiers, key, text);
            });

    m_log = new QPlainTextEdit(this);
    m_log->setReadOnly(true);
    m_log->setMaximumBlockCount(500); // 只是诊断用的滚动日志，不需要无限增长
    m_log->setMaximumHeight(120);     // 画面表面才是主角，日志区压缩到一个较小的高度
    m_log->setPlaceholderText(tr("（沙箱子进程的日志会显示在这里）"));

    layout->addWidget(m_stateLabel);
    layout->addWidget(m_sandboxIdLabel);
    layout->addWidget(m_restartButton);
    layout->addWidget(m_surface);
    layout->addWidget(m_log, /*stretch=*/1);

    setState(sandbox::TabState::Queued);
    setSandboxId(QString());
}

void TabContentWidget::setState(sandbox::TabState state)
{
    m_stateLabel->setText(tr("状态：%1").arg(tabStateString(state)));
    m_restartButton->setVisible(state == sandbox::TabState::Faulted
                                || state == sandbox::TabState::Restoring);
}

void TabContentWidget::setSandboxId(const QString &sandboxId)
{
    m_sandboxIdLabel->setText(
        tr("沙箱 ID：%1").arg(sandboxId.isEmpty() ? tr("<尚未分配>") : sandboxId));
}

void TabContentWidget::appendLogLine(int level, const QString &message)
{
    m_log->appendPlainText(QStringLiteral("[level=%1] %2").arg(level).arg(message));
}

void TabContentWidget::setFrame(const QImage &image, const QRect &dirtyRect)
{
    m_surface->setFrame(image, dirtyRect);
}

} // namespace bakuon::host
