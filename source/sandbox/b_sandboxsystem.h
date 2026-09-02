#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <unordered_map>

#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtCore/QVariantMap>
#include <QtCore/QVector>

QT_BEGIN_NAMESPACE
class QRemoteObjectRegistryHost;
QT_END_NAMESPACE

namespace bakuon::sandbox {

class SandboxSupervisor;
enum class SandboxPhase;

/**
 * @brief 门面类 —— 沙箱实例注册表 + 统一事件转发。
 *
 * 与 gui::PluginSystem 之于 gui::PluginPipeline 的关系刻意对称：本类不重复
 * 实现任何单个沙箱的生命周期逻辑（那是 SandboxSupervisor 的职责），只做两件事：
 *  1. 生成 sandboxId、创建/持有 SandboxSupervisor；
 *  2. 把每个 SandboxSupervisor 的信号转发成带 sandboxId 的统一信号，方便 Host
 *     用一个槽函数就能观测所有并发运行的沙箱实例，不需要对每个实例单独 connect。
 *
 * 注意：是否要把某个 IPlugin 放进"进程内直接加载"（gui::PluginSystem）还是
 * "进程外沙箱隔离"（本类）是调用方的业务决策，本类不参与这个判断——PluginMetadata
 * 目前也还没有一个"是否需要沙箱隔离"的字段（这是刻意的：先把两条腿都能跑通，
 * 具体哪些插件默认走沙箱、要不要在 PluginMetadata/json 里加 "Sandboxed": true
 * 这类声明式配置，等两条路径都稳定之后再决定，避免过早收窄设计）。
 *
 * 本类还额外持有整个主程序生命周期内唯一一份 QRemoteObjectRegistryHost（引入注册中心
 * 之后：子进程不再需要预先知道 Host 的地址，Host 也不需要为每个沙箱实例单独维护一个
 * QRemoteObjectNode——所有沙箱实例共用这一个注册中心 Node 做 acquire()，发现关系
 * 完全由注册中心居中代理，见 b_sandboxconstants.h 的 registryUrl()）。
 */
class SandboxSystem : public QObject
{
    Q_OBJECT
public:
    explicit SandboxSystem(QObject *parent = nullptr);
    ~SandboxSystem() override;

    /**
     * @brief 创建并启动一个新的沙箱实例。
     * @param pluginFilePath          要在沙箱里加载的插件动态库路径
     * @param sandboxRuntimeExecutable sandbox_runtime 可执行文件路径
     * @param pluginArguments          透传给插件 initialize() 的启动参数
     * @return 新分配的 sandboxId（用于后续 supervisor()/run()/stop()/shutdown() 查询和操作）
     */
    QString spawn(const QString &pluginFilePath, const QString &sandboxRuntimeExecutable,
                  QVariantMap pluginArguments = {});

    /// 便捷方法：对指定 sandboxId 调用 SandboxSupervisor::run()/stop()/shutdown()。
    bool run(const QString &sandboxId);
    bool stop(const QString &sandboxId);
    bool shutdown(const QString &sandboxId);
    /// 对所有仍在注册表里的实例调用 shutdown()；不等待子进程真正退出。
    void shutdownAll();

    /// 子进程真正退出（processFinished）后从注册表移除；之后 sandboxId 不再有效。
    void remove(const QString &sandboxId);

    /**
     * @brief 收编一个通过 orphanDiscovered 信号报告的孤儿沙箱：不会 spawn 新进程，
     *        只是用共享的注册中心 Node acquire() 一个 Replica 连接上去，开始追踪它
     *        后续的阶段变化/日志/故障事件——底层子进程本来就已经在跑，是 QtRO 层面
     *        重新"接上线"而已。
     * @param sandboxId 从 orphanDiscovered 信号拿到的 sandboxId
     * @return 成功发起收编返回 true；sandboxId 已经在注册表里（不是真正的孤儿）
     *         或格式不合法时返回 false。
     * @note 收编之后无法获得该实例的 pluginFilePath（当初是哪个插件、用什么参数
     *       启动的——这些信息只存在于旧 Host 进程的内存里，本类和调用方都无从得知，
     *       这也是为什么本方法不像 spawn() 一样需要 pluginFilePath 参数）。
     *       调用方如果需要恢复"这个孤儿对应哪个业务身份"，需要自己维护一份跨 Host
     *       进程生命周期的会话记录并按 sandboxId 反查——本类只负责把连接接上。
     */
    bool adopt(const QString &sandboxId);

    [[nodiscard]] std::shared_ptr<SandboxSupervisor> supervisor(const QString &sandboxId) const;
    [[nodiscard]] QVector<QString> sandboxIds() const;
    [[nodiscard]] size_t count() const;

Q_SIGNALS:
    void sandboxPhaseChanged(const QString &sandboxId, SandboxPhase phase);
    void sandboxLogMessage(const QString &sandboxId, int level, const QString &message);
    void sandboxFaulted(const QString &sandboxId, const QString &reason);
    /// 子进程已经真正退出（对应 SandboxSupervisor::processFinished），此后该 sandboxId 会被自动 remove()。
    void sandboxProcessFinished(const QString &sandboxId, int exitCode);
    /**
     * @brief 注册中心里出现了一个本实例没有 spawn() 过的 PluginSandboxControl 对象。
     *
     * 典型场景：上一个 Host 进程 spawn() 出来的沙箱子进程还活着（Host 崩溃/被杀但
     * 子进程没有被一起杀掉），本实例（新 Host 进程）用同一个注册中心地址起来后，
     * 那个子进程自动重连到了这个新注册中心、重新宣告了自己——这条信号就是那一刻的
     * 通知。是否要 adopt() 收编它是调用方的业务决策，本类只负责发现和上报。
     */
    void orphanDiscovered(const QString &sandboxId);

private:
    QString nextSandboxId();
    /// spawn()/adopt() 共用：把新建的 SandboxSupervisor 的信号转发成带 sandboxId 的
    /// 统一信号，并注册进程退出后的自动 remove()。
    void wireSupervisorSignals(const QString &sandboxId,
                               const std::shared_ptr<SandboxSupervisor> &supervisor);

    // void adoptOrphans(const QRemoteObjectSourceLocation &entities);

private:
    std::atomic<uint64_t> m_nextSeq{1};
    std::unique_ptr<QRemoteObjectRegistryHost> m_registry;
    std::unordered_map<QString, std::shared_ptr<SandboxSupervisor>> m_entries;
};

} // namespace bakuon::sandbox
