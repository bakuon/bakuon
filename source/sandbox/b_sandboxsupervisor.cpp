#include "sandbox/b_sandboxsupervisor.h"

#include <QtCore/QDebug>
#include <QtCore/QUuid>
#include <QtRemoteObjects/QRemoteObjectNode>
#include <QtRemoteObjects/QRemoteObjectReplica>

#include "sandbox/b_sandboxconstants.h"
#include "sandbox/b_sharedmemorychannel.h"

// repc 生成的 Replica 端头文件，由 CMakeLists.txt 里的 qt6_add_repc_replicas() 驱动生成。
#include "rep_PluginSandboxControl_replica.h"

namespace bakuon::sandbox {

QString toString(SandboxPhase phase)
{
    switch (phase) {
    case SandboxPhase::Connecting  : return QStringLiteral("Connecting");
    case SandboxPhase::Loading     : return QStringLiteral("Loading");
    case SandboxPhase::Initializing: return QStringLiteral("Initializing");
    case SandboxPhase::Ready       : return QStringLiteral("Ready");
    case SandboxPhase::Running     : return QStringLiteral("Running");
    case SandboxPhase::Stopping    : return QStringLiteral("Stopping");
    case SandboxPhase::Stopped     : return QStringLiteral("Stopped");
    case SandboxPhase::Faulted     : return QStringLiteral("Faulted");
    default                        : break;
    }
    return QStringLiteral("<unknown SandboxPhase>");
}

namespace {
/// PluginSandboxControlReplica::SandboxPhase（repc 生成，嵌在 Replica 类里）
/// 与本模块对外的 SandboxPhase（b_sandboxsupervisor.h，不依赖任何 QtRO 生成头）
/// 之间的转换——刻意不直接把 repc 生成的枚举类型暴露到 SandboxSupervisor 的公开
/// 接口里，这样 SandboxSupervisor 的消费者（比如未来的 SandboxSystem 或 UI 层）
/// 不需要 #include 任何 rep_*.h 生成头，也不需要链接 QtRO 相关的 include 路径。
SandboxPhase fromReplicaPhase(PluginSandboxControlReplica::SandboxPhase p)
{
    switch (p) {
    case PluginSandboxControlReplica::Connecting  : return SandboxPhase::Connecting;
    case PluginSandboxControlReplica::Loading     : return SandboxPhase::Loading;
    case PluginSandboxControlReplica::Initializing: return SandboxPhase::Initializing;
    case PluginSandboxControlReplica::Ready       : return SandboxPhase::Ready;
    case PluginSandboxControlReplica::Running     : return SandboxPhase::Running;
    case PluginSandboxControlReplica::Stopping    : return SandboxPhase::Stopping;
    case PluginSandboxControlReplica::Stopped     : return SandboxPhase::Stopped;
    case PluginSandboxControlReplica::Faulted     : return SandboxPhase::Faulted;
    default                                       : break;
    }
    return SandboxPhase::Faulted;
}
} // namespace

/// beginCommand() 发起、commandFinished 信号到达前，需要保活的每次调用状态。
struct SandboxSupervisor::PendingCommand
{
    SharedMemoryChannel channel;
};

SandboxSupervisor::SandboxSupervisor(QString sandboxId, QString pluginFilePath, QObject *parent)
    : QObject(parent)
    , m_sandboxId(std::move(sandboxId))
    , m_pluginFilePath(std::move(pluginFilePath))
{
}

SandboxSupervisor::~SandboxSupervisor()
{
    // 析构时如果子进程还活着，先礼后兵：给一次优雅退出的机会，超时后强制结束，
    // 避免僵尸沙箱进程残留（尤其是插件里可能存在死循环/未响应 shutdownSandbox() 的情况）。
    if (m_process && m_process->state() != QProcess::NotRunning) {
        if (m_replica) {
            m_replica->shutdownSandbox();
        }
        if (!m_process->waitForFinished(2000)) {
            m_process->kill();
            m_process->waitForFinished(1000);
        }
    }
}

void SandboxSupervisor::start(const QString &sandboxRuntimeExecutable, QVariantMap pluginArguments)
{
    m_pendingPluginArguments = std::move(pluginArguments);

    const QString listenUrl = makeSandboxListenUrl();

    m_process = std::make_unique<QProcess>(this);
    connect(m_process.get(),
            &QProcess::finished,
            this,
            [this](int exitCode, QProcess::ExitStatus status) {
                if (status == QProcess::CrashExit
                    || (exitCode != 0 && m_phase != SandboxPhase::Stopped)) {
                    m_phase = SandboxPhase::Faulted;
                    Q_EMIT phaseChanged(m_phase);
                    Q_EMIT faulted(QStringLiteral("沙箱子进程异常退出，exitCode=%1").arg(exitCode));
                }
                Q_EMIT processFinished(exitCode);
            });
    connect(m_process.get(), &QProcess::readyReadStandardError, this, [this] {
        const QByteArray err = m_process->readAllStandardError();
        if (!err.isEmpty()) {
            Q_EMIT logMessage(2 /*Error*/, QString::fromLocal8Bit(err));
        }
    });

    m_process->setProgram(sandboxRuntimeExecutable);
    m_process->setArguments({QString::fromLatin1(cli::kListen),
                             listenUrl,
                             QString::fromLatin1(cli::kSandboxId),
                             m_sandboxId});
    m_process->start();

    m_node = std::make_unique<QRemoteObjectNode>();
    connect(m_node.get(), &QRemoteObjectNode::error, this, [this](QRemoteObjectNode::ErrorCode code) {
        Q_EMIT logMessage(1 /*Warning*/,
                          QStringLiteral("QRemoteObjectNode 错误码：%1").arg(int(code)));
    });
    m_node->connectToNode(QUrl(listenUrl));

    m_replica.reset(
        m_node->acquire<PluginSandboxControlReplica>(QString::fromLatin1(kSandboxObjectName)));
    connect(m_replica.get(),
            &QRemoteObjectReplica::stateChanged,
            this,
            &SandboxSupervisor::onReplicaStateChanged);
    bindReplicaSignals();
}

void SandboxSupervisor::onReplicaStateChanged()
{
    if (m_replica->state() != QRemoteObjectReplica::Valid) {
        return;
    }
    // Replica 刚变为可用（即已经连接上 Sandbox 子进程发布的 Source），此时才能安全调用
    // 契约 slot——过早调用会因为底层连接尚未建立而被 QtRO 静默丢弃。
    connect(m_replica.get(),
            &PluginSandboxControlReplica::phaseChanged,
            this,
            [this](PluginSandboxControlReplica::SandboxPhase p) {
                m_phase = fromReplicaPhase(p);
                Q_EMIT phaseChanged(m_phase);
            });
    m_replica->loadPlugin(m_pluginFilePath, m_pendingPluginArguments);
}

void SandboxSupervisor::bindReplicaSignals()
{
    connect(m_replica.get(),
            &PluginSandboxControlReplica::logMessage,
            this,
            &SandboxSupervisor::logMessage);
    connect(m_replica.get(),
            &PluginSandboxControlReplica::faulted,
            this,
            [this](const QString &reason) {
                m_phase = SandboxPhase::Faulted;
                Q_EMIT phaseChanged(m_phase);
                Q_EMIT faulted(reason);
            });
    connect(m_replica.get(),
            &PluginSandboxControlReplica::commandFinished,
            this,
            [this](const QString &requestId,
                   bool ok,
                   const QString &memoryKey,
                   const QString &errorMessage) {
                const auto it = m_pendingCommands.find(requestId);
                if (it == m_pendingCommands.end()) {
                    Q_EMIT logMessage(1 /*Warning*/,
                                      QStringLiteral("收到未知 requestId=%1 的 commandFinished")
                                          .arg(requestId));
                    return;
                }
                QByteArray result;
                if (ok) {
                    result = it->second->channel.readPayload();
                }
                Q_UNUSED(memoryKey)          // channel 内部已经记住了自己的 key，这里不需要重新挂载
                m_pendingCommands.erase(it); // SharedMemoryChannel 析构 -> release() 自动 detach
                Q_EMIT commandFinished(requestId, ok, result, errorMessage);
            });
}

void SandboxSupervisor::run()
{
    if (m_replica) {
        m_replica->run();
    }
}

void SandboxSupervisor::stop()
{
    if (m_replica) {
        m_replica->stop();
    }
}

void SandboxSupervisor::shutdown()
{
    if (m_replica) {
        m_replica->shutdownSandbox();
    }
}

QString SandboxSupervisor::beginCommand(const QString &commandId, const QByteArray &inputPayload,
                                        quint32 resultCapacity, QVariantMap params)
{
    if (!m_replica) {
        return {};
    }

    // 不使用 "const QString requestId" 仅为消除
    // clang warning: `Constness of 'requestId' prevents automatic move`
    QString requestId       = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QString memoryKey = makeSharedMemoryKey(m_sandboxId, requestId);

    auto pending = std::make_unique<PendingCommand>();
    if (auto err = pending->channel.create(memoryKey, inputPayload, resultCapacity)) {
        Q_EMIT logMessage(2 /*Error*/,
                          QStringLiteral("beginCommand(%1) 分配共享内存失败：%2")
                              .arg(commandId, *err));
        return {};
    }

    m_pendingCommands.emplace(requestId, std::move(pending));
    m_replica->executeCommand(requestId, commandId, memoryKey, std::move(params));
    return requestId;
}

qint64 SandboxSupervisor::processId() const
{
    return m_process ? m_process->processId() : 0;
}

} // namespace bakuon::sandbox
