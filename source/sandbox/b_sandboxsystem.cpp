#include "sandbox/b_sandboxsystem.h"

#include <QtRemoteObjects/QRemoteObjectRegistryHost>

#include "sandbox/b_sandboxconstants.h"
#include "sandbox/b_sandboxsupervisor.h"

namespace bakuon::sandbox {

SandboxSystem::SandboxSystem(QObject *parent)
    : QObject(parent)
    , m_registry(std::make_unique<QRemoteObjectRegistryHost>(QUrl(registryUrl())))
{
    connect(m_registry.get(),
            &QRemoteObjectNode::error,
            this,
            [this](QRemoteObjectNode::ErrorCode code) {
                Q_EMIT sandboxLogMessage(QStringLiteral("<registry>"),
                                         1 /*Warning*/,
                                         QStringLiteral("注册中心 QRemoteObjectNode 错误码：%1")
                                             .arg(int(code)));
            });
}

SandboxSystem::~SandboxSystem() = default;

QString SandboxSystem::nextSandboxId()
{
    return QStringLiteral("sandbox-%1").arg(m_nextSeq.fetch_add(1));
}

QString SandboxSystem::spawn(const QString &pluginFilePath, const QString &sandboxRuntimeExecutable,
                             QVariantMap pluginArguments)
{
    const QString id = nextSandboxId();
    auto supervisor  = std::make_shared<SandboxSupervisor>(id, pluginFilePath, *m_registry, this);

    connect(supervisor.get(),
            &SandboxSupervisor::phaseChanged,
            this,
            [this, id](SandboxPhase phase) { Q_EMIT sandboxPhaseChanged(id, phase); });
    connect(supervisor.get(),
            &SandboxSupervisor::logMessage,
            this,
            [this, id](int level, const QString &message) {
                Q_EMIT sandboxLogMessage(id, level, message);
            });
    connect(supervisor.get(), &SandboxSupervisor::faulted, this, [this, id](const QString &reason) {
        Q_EMIT sandboxFaulted(id, reason);
    });
    connect(supervisor.get(), &SandboxSupervisor::processFinished, this, [this, id](int exitCode) {
        Q_EMIT sandboxProcessFinished(id, exitCode);
        // 子进程已经真正退出，注册表里的 SandboxSupervisor 不再有存在意义——放到下一个事件循环
        // tick 再 remove()，避免在 SandboxSupervisor 自己发出的信号处理函数里直接销毁自身。
        QMetaObject::invokeMethod(this, [this, id] { remove(id); }, Qt::QueuedConnection);
    });

    m_entries.emplace(id, supervisor);
    supervisor->start(sandboxRuntimeExecutable, std::move(pluginArguments));
    return id;
}

bool SandboxSystem::run(const QString &sandboxId)
{
    auto sup = supervisor(sandboxId);
    if (!sup) {
        return false;
    }
    sup->run();
    return true;
}

bool SandboxSystem::stop(const QString &sandboxId)
{
    auto sup = supervisor(sandboxId);
    if (!sup) {
        return false;
    }
    sup->stop();
    return true;
}

bool SandboxSystem::shutdown(const QString &sandboxId)
{
    auto sup = supervisor(sandboxId);
    if (!sup) {
        return false;
    }
    sup->shutdown();
    return true;
}

void SandboxSystem::shutdownAll()
{
    for (const auto &[id, sup] : m_entries) {
        Q_UNUSED(id)
        sup->shutdown();
    }
}

void SandboxSystem::remove(const QString &sandboxId)
{
    m_entries.erase(sandboxId);
}

std::shared_ptr<SandboxSupervisor> SandboxSystem::supervisor(const QString &sandboxId) const
{
    const auto it = m_entries.find(sandboxId);
    return it != m_entries.end() ? it->second : nullptr;
}

QVector<QString> SandboxSystem::sandboxIds() const
{
    QVector<QString> ids;
    ids.reserve(static_cast<qsizetype>(m_entries.size()));
    for (const auto &[id, sup] : m_entries) {
        Q_UNUSED(sup)
        ids.push_back(id);
    }
    return ids;
}

size_t SandboxSystem::count() const
{
    return m_entries.size();
}

} // namespace bakuon::sandbox
