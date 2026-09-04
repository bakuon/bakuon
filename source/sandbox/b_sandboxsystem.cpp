#include "sandbox/b_sandboxsystem.h"

#include <QtCore/QCoreApplication>
#include <QtRemoteObjects/QRemoteObjectRegistry>
#include <QtRemoteObjects/QRemoteObjectRegistryHost>
#include <QtRemoteObjects/QRemoteObjectSourceLocation>

#include "sandbox/b_sandboxconstants.h"
#include "sandbox/b_sandboxsupervisor.h"

namespace bakuon::sandbox {

SandboxSystem::SandboxSystem(QObject *parent)
    : QObject(parent)
    , m_host(std::make_unique<QRemoteObjectRegistryHost>(QUrl(registryUrl())))
{
    connect(m_host.get(), &QRemoteObjectNode::error, this, [this](QRemoteObjectNode::ErrorCode code) {
        Q_EMIT sandboxLogMessage(QStringLiteral("<registry>"),
                                 1 /*Warning*/,
                                 QStringLiteral("注册中心 QRemoteObjectNode 错误码：%1")
                                     .arg(int(code)));
    });

    // 孤儿发现：任何时候注册中心里出现一个新对象，都检查一下是不是本实例已知的
    // （正常 spawn() 出来的实例，在子进程真正把 Source 发布出来、这个信号触发之前，
    // m_entries 里已经有它的条目了，见 spawn() 的插入顺序），不认识的才当作孤儿上报。
    // 实测验证过这条链路是可行的（旧 Host 进程的注册中心销毁后，仍然存活的沙箱子进程
    // 会自动重连到新起的、同地址的注册中心，重新出现在这里）。
    connect(m_host->registry(),
            &QRemoteObjectRegistry::remoteObjectAdded,
            this,
            [this](const QRemoteObjectSourceLocation &loc) {
                const auto sandboxId = parseSandboxObjectName(loc.first);
                if (!sandboxId) {
                    return; // 不是本契约的对象名，不关我们的事
                }
                if (m_entries.contains(*sandboxId)) {
                    return; // 本实例自己 spawn() 的，不是孤儿
                }
                Q_EMIT orphanDiscovered(*sandboxId);
            });

    // adopt() 收编来的实例没有 m_process（见 SandboxSupervisor::attach()），感知不到
    // 底层进程退出的唯一渠道是 QtRO 连接层面的信号——最初尝试用注册中心的
    // remoteObjectRemoved 信号，实测（throwaway 实验 + 本模块集成测试）发现它并不
    // 可靠触发，即使打开了心跳也是如此；真正可靠、且已经实测验证过的是
    // QRemoteObjectReplica::state() 变为 Suspect（"连接到 Source 之后又丢失"的
    // 错误态），这条路径改在 SandboxSupervisor::onReplicaStateChanged() 里处理
    // （它本来就在监听 Replica 的 stateChanged），见该函数实现。
    //
    // Suspect 检测依赖心跳——QtRO 默认心跳间隔是 0（关闭），文档原话是"客户端只有
    // 在尝试发送数据时才会发现服务端不可用"，也就是说不主动探测的话，一个已经
    // 断开的连接可能会无限期停留在 Valid 状态。这里显式打开心跳，让 Suspect 转换
    // 在合理时间内可靠触发。
    m_host->setHeartbeatInterval(3000);
}

SandboxSystem::~SandboxSystem() = default;

QString SandboxSystem::nextSandboxId()
{
    // 混入 Host 自身 PID 作为进程级别的区分符：sandboxId 现在还被用作注册中心对象名的
    // 一部分（见 makeSandboxObjectName()）。如果单纯从 1 开始重新计数，一旦 Host
    // 崩溃重启、又恰好有上一次进程存活下来的孤儿沙箱（见 orphanDiscovered 信号 /
    // TabSandboxManager::tryAdoptOrphanedSandboxes()），新 Host 的计数器迟早会数到
    // 一个孤儿正在占用的旧 id，在注册中心里两个不同的沙箱进程抢同一个对象名。
    // 混入 PID 后，不同 Host 进程生成的 sandboxId 前缀天然不同，从根上避免这个碰撞
    // （不追求绝对不可能碰撞——同一个 PID 在系统重启后被复用是理论可能的，但那个
    // 时间窗口里孤儿沙箱早就没了，不构成实际风险）。
    return QStringLiteral("sandbox-%1-%2")
        .arg(QCoreApplication::applicationPid())
        .arg(m_nextSeq.fetch_add(1));
}

QString SandboxSystem::spawn(const QString &pluginFilePath, const QString &sandboxRuntimeExecutable,
                             QVariantMap pluginArguments)
{
    const QString id = nextSandboxId();
    auto supervisor  = std::make_shared<SandboxSupervisor>(id, pluginFilePath, *m_host, this);
    wireSupervisorSignals(id, supervisor);

    m_entries.emplace(id, supervisor);
    supervisor->start(sandboxRuntimeExecutable, std::move(pluginArguments));
    return id;
}

bool SandboxSystem::adopt(const QString &sandboxId)
{
    if (sandboxId.isEmpty() || m_entries.contains(sandboxId)) {
        return false;
    }
    // pluginFilePath 留空：收编模式下 attach() 根本不会用到它（不会重新 loadPlugin()），
    // 见 SandboxSupervisor::attach() 的实现和文档。
    auto supervisor = std::make_shared<SandboxSupervisor>(sandboxId, QString(), *m_host, this);
    wireSupervisorSignals(sandboxId, supervisor);

    m_entries.emplace(sandboxId, supervisor);
    supervisor->attach();
    return true;
}

void SandboxSystem::wireSupervisorSignals(const QString &sandboxId,
                                          const std::shared_ptr<SandboxSupervisor> &supervisor)
{
    connect(supervisor.get(),
            &SandboxSupervisor::phaseChanged,
            this,
            [this, sandboxId](SandboxPhase phase) { Q_EMIT sandboxPhaseChanged(sandboxId, phase); });
    connect(supervisor.get(),
            &SandboxSupervisor::logMessage,
            this,
            [this, sandboxId](int level, const QString &message) {
                Q_EMIT sandboxLogMessage(sandboxId, level, message);
            });
    connect(supervisor.get(),
            &SandboxSupervisor::faulted,
            this,
            [this, sandboxId](const QString &reason) { Q_EMIT sandboxFaulted(sandboxId, reason); });
    connect(supervisor.get(),
            &SandboxSupervisor::processFinished,
            this,
            [this, sandboxId](int exitCode) {
                Q_EMIT sandboxProcessFinished(sandboxId, exitCode);
                // 子进程已经真正退出，注册表里的 SandboxSupervisor 不再有存在意义——放到下一个事件循环
                // tick 再 remove()，避免在 SandboxSupervisor 自己发出的信号处理函数里直接销毁自身。
                QMetaObject::invokeMethod(
                    this, [this, sandboxId] { remove(sandboxId); }, Qt::QueuedConnection);
            });
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
