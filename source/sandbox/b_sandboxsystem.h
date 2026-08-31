#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <unordered_map>

#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtCore/QVariantMap>
#include <QtCore/QVector>

class QRemoteObjectRegistryHost;

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

    [[nodiscard]] std::shared_ptr<SandboxSupervisor> supervisor(const QString &sandboxId) const;
    [[nodiscard]] QVector<QString> sandboxIds() const;
    [[nodiscard]] size_t count() const;

Q_SIGNALS:
    void sandboxPhaseChanged(const QString &sandboxId, SandboxPhase phase);
    void sandboxLogMessage(const QString &sandboxId, int level, const QString &message);
    void sandboxFaulted(const QString &sandboxId, const QString &reason);
    /// 子进程已经真正退出（对应 SandboxSupervisor::processFinished），此后该 sandboxId 会被自动 remove()。
    void sandboxProcessFinished(const QString &sandboxId, int exitCode);

private:
    QString nextSandboxId();

private:
    std::atomic<uint64_t> m_nextSeq{1};
    std::unique_ptr<QRemoteObjectRegistryHost> m_registry;
    std::unordered_map<QString, std::shared_ptr<SandboxSupervisor>> m_entries;
};

} // namespace bakuon::sandbox
