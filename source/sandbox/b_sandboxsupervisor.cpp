#include "sandbox/b_sandboxsupervisor.h"

#include <QtCore/QDebug>
#include <QtCore/QUuid>
#include <QtRemoteObjects/QRemoteObjectNode>
#include <QtRemoteObjects/QRemoteObjectReplica>

#include "sandbox/b_sandboxconstants.h"
#include "sandbox/b_sharedmemorychannel.h"

// repc 生成的 Replica 端头文件，由 CMakeLists.txt 里的 qt6_add_repc_replicas() 驱动生成。
#include "rep_b_pluginsandboxcontrol_replica.h"

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

SandboxSupervisor::SandboxSupervisor(QString sandboxId, QString pluginFilePath,
                                     QRemoteObjectNode &registryNode, QObject *parent)
    : QObject(parent)
    , m_sandboxId(std::move(sandboxId))
    , m_pluginFilePath(std::move(pluginFilePath))
    , m_registryNode(registryNode)
{
}

SandboxSupervisor::~SandboxSupervisor()
{
    // 析构时如果子进程还活着，先礼后兵：给一次优雅退出的机会，超时后强制结束，
    // 避免僵尸沙箱进程残留（尤其是插件里可能存在死循环/未响应 shutdownSandbox() 的情况）。
    // 注意：attach() 模式下 m_process 恒为空（本类没有 spawn 过它，也就没有 QProcess
    // 句柄可以 kill()）——这个 if 分支天然对收编来的实例整体跳过，意味着析构一个
    // attach() 来的 SandboxSupervisor 不会做任何优雅关闭尝试。调用方如果想关掉一个
    // 收编来的实例，必须在析构之前显式调用 shutdown()（通过 SandboxSystem::shutdown()）
    // 并给对方响应的时间；本类没有能力替调用方强制终止一个自己没有 spawn 过的进程。
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

    // 子进程仍然需要自己独立的监听地址（真正的 Replica 数据连接落地点），
    // 只是不再需要由本类去 connectToNode() 到这个地址——那部分现在由 QtRO 通过
    // 注册中心在背后完成，本类只需要把地址通过命令行告诉子进程即可。
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
                             QString::fromLatin1(cli::kRegistry),
                             registryUrl(),
                             QString::fromLatin1(cli::kSandboxId),
                             m_sandboxId});
    m_process->start();

    m_needsLoadPlugin = true;
    acquireReplica();
}

void SandboxSupervisor::attach()
{
    // 不 spawn 子进程——对方早就在跑了，本类要做的只是重新把 QtRO 连接接上。
    m_needsLoadPlugin = false;
    acquireReplica();
}

void SandboxSupervisor::acquireReplica()
{
    // 直接用注入进来的、Host 主程序全局共用的注册中心 Node acquire——不再需要
    // 自己 connectToNode()（更不需要知道子进程到底监听在哪个地址上，那是子进程和
    // 注册中心之间的事）。对象名必须带上 sandboxId，见 makeSandboxObjectName()。
    m_replica.reset(
        m_registryNode.acquire<PluginSandboxControlReplica>(makeSandboxObjectName(m_sandboxId)));
    connect(m_replica.get(),
            &QRemoteObjectReplica::stateChanged,
            this,
            &SandboxSupervisor::onReplicaStateChanged);
    bindReplicaSignals();
}

void SandboxSupervisor::onReplicaStateChanged()
{
    if (m_replica->state() == QRemoteObjectReplica::Suspect) {
        // 连接彻底丢了（对方进程真的没了，或者出现了别的网络层异常）——见
        // SandboxSystem 构造函数里对 setHeartbeatInterval() 的说明，没有心跳的话
        // 这个状态转换可能长期不会发生。
        //
        // 对 start() 出来的、本类拥有 QProcess 的实例：这基本只是 QProcess::finished
        // 的一个先兆，那条路径本身权威、迟早也会到，这里不重复处理，避免
        // processFinished 信号触发两次。
        //
        // 对 attach() 收编来的实例：没有 m_process，Suspect 是唯一能知道"对方已经
        // 不在了"的信号，这里补一次 processFinished（退出码未知，用 -1 表示），
        // 让 SandboxSystem/TabSandboxManager 走和正常退出一样的收尾路径。
        if (!m_process) {
            Q_EMIT processFinished(-1);
        }
        return;
    }
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

    if (m_needsLoadPlugin) {
        m_replica->loadPlugin(m_pluginFilePath, m_pendingPluginArguments);
    } else {
        // attach() 模式：对方早就跑起来了，Replica 变 Valid 的这一刻 QtRO 已经把
        // Source 当前的完整属性状态同步过来了（phase 等 PROP 字段），直接读出来
        // 广播一次即可——不能再调用 loadPlugin()，那是"从头初始化"的语义，
        // 对一个已经在跑的实例重放会违反契约（对方要么忽略、要么状态错乱）。
        m_phase = fromReplicaPhase(m_replica->phase());
        Q_EMIT phaseChanged(m_phase);
    }
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
                Q_UNUSED(memoryKey);         // channel 内部已经记住了自己的 key，这里不需要重新挂载
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

    const QString requestId = QUuid::createUuid().toString(QUuid::WithoutBraces);
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
    if (m_process) {
        return m_process->processId();
    }
    // attach() 模式没有 m_process（本类没有 spawn 过它，无从持有 QProcess 句柄），
    // 退而求其次用契约里的 pid 属性（PROP(qint64 pid)，见 .rep）——由沙箱子进程自己
    // 上报，Replica 变 Valid 后就应该有值。
    return m_replica ? m_replica->pid() : 0;
}

} // namespace bakuon::sandbox
