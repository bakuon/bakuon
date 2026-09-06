#include "TabContentWidget.h"

#include <QtWidgets/QLabel>
#include <QtWidgets/QPlainTextEdit>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QVBoxLayout>

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
} // namespace

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

    m_log = new QPlainTextEdit(this);
    m_log->setReadOnly(true);
    m_log->setMaximumBlockCount(500); // 只是诊断用的滚动日志，不需要无限增长
    m_log->setPlaceholderText(tr("（沙箱子进程的日志会显示在这里）"));

    layout->addWidget(m_stateLabel);
    layout->addWidget(m_sandboxIdLabel);
    layout->addWidget(m_restartButton);
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

} // namespace bakuon::host
