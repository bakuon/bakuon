#pragma once

#include <memory>
#include <unordered_map>

#include <QtCore/QByteArray>
#include <QtCore/QObject>
#include <QtCore/QProcess>
#include <QtCore/QString>
#include <QtCore/QVariantMap>

QT_BEGIN_NAMESPACE
class QRemoteObjectNode;
QT_END_NAMESPACE

class PluginSandboxControlReplica;

namespace bakuon::sandbox {

/// 与 pluginsandboxcontrol.rep 里的 SandboxPhase 一一对应，见该文件顶部注释。
enum class SandboxPhase {
    Connecting,
    Loading,
    Initializing,
    Ready,
    Running,
    Stopping,
    Stopped,
    Faulted,
};

QString toString(SandboxPhase phase);

/**
 * @brief Host 侧对单个"进程外插件沙箱"实例的管理者。
 *
 * 职责三件事（与 gui::PluginPipeline 对"单个进程内插件"的职责刻意对称，
 * 方便熟悉既有插件系统的人快速理解这是同一个思路在"进程外"的投影）：
 *  1. spawn sandbox_runtime 子进程，并把本次实例专属的本地监听地址通过
 *     命令行参数传给它（见 b_sandboxconstants.h 的 cli::kListen）；
 *  2. connectToNode() 连接过去，acquire<PluginSandboxControlReplica>()，
 *     驱动 loadPlugin()/run()/stop()/shutdownSandbox() 这几个契约 slot；
 *  3. 把 Replica 的属性变化（phase/progress/pluginId）和信号
 *     （logMessage/commandFinished/faulted）转成本类自己的 Qt 信号，
 *     对外屏蔽掉 QtRO/QRemoteObjectReplica 这些实现细节。
 *
 * ## 为什么本类持有的是 QRemoteObjectNode 而不是 QRemoteObjectHost
 * 见 pluginsandboxcontrol.rep 顶部注释的架构说明：真正 enableRemoting()
 * 发布 Source 对象的是 Sandbox 子进程（见 SandboxRuntime），Host 侧只需要
 * 连接过去 acquire，一个轻量 QRemoteObjectNode 就够用，不需要自己监听。
 *
 * ## executeCommand() 与共享内存的配合
 * beginCommand() 负责：分配一块共享内存段、写入输入 Payload、记下
 * requestId → SharedMemoryChannel 的映射，再调用 Replica 的
 * executeCommand()（只传 key，不传数据本身）。真正的结果通过
 * commandFinished 信号异步返回时，本类从映射表里找回对应的
 * SharedMemoryChannel、读出结果区、释放共享内存段，再把结果通过自己的
 * commandFinished 信号转发给调用方——调用方全程不需要触碰 QSharedMemory。
 */
class SandboxSupervisor : public QObject
{
    Q_OBJECT
public:
    /**
     * @param sandboxId      本实例 id，同时也是它在注册中心里发布的对象名的一部分
     *                       （见 b_sandboxconstants.h 的 makeSandboxObjectName()）。
     * @param pluginFilePath 要在沙箱里加载的插件动态库路径。
     * @param registryNode   Host 主程序内唯一一份、生命周期跨所有沙箱实例共享的
     *                       QRemoteObjectRegistryHost（以 QRemoteObjectNode& 传入，
     *                       本类只需要用它 acquire<Replica>()，注册中心本身的创建/持有
     *                       是 SandboxSystem 的职责）。引入注册中心之后，本类不再需要
     *                       为每个沙箱实例单独持有一个 QRemoteObjectNode、也不需要预先
     *                       知道子进程会监听在哪个地址上——子进程拿到注册中心地址后
     *                       会自己去注册，acquire() 时的点对点连接由 QtRO 在背后转发建立。
     */
    explicit SandboxSupervisor(QString sandboxId, QString pluginFilePath,
                               QRemoteObjectNode &registryNode, QObject *parent = nullptr);
    ~SandboxSupervisor() override;

    SandboxSupervisor(const SandboxSupervisor &)            = delete;
    SandboxSupervisor &operator=(const SandboxSupervisor &) = delete;

    /**
     * @brief spawn 子进程 + 建立 QtRO 连接；一旦 Replica 变为可用，会自动调用
     *        一次 loadPlugin()（阶段随之从 Connecting 经 Loading 自动推进）。
     * @param sandboxRuntimeExecutable sandbox_runtime 可执行文件路径（通常由调用方通过
     *        QCoreApplication::applicationDirPath() 或已知的构建产物路径拼接得到）
     * @param pluginArguments 透传给 IPlugin::initialize() 的插件启动参数
     */
    void start(const QString &sandboxRuntimeExecutable, QVariantMap pluginArguments = {});

    /// 等价于对 Replica 调用 run()；仅当 phase() == Ready 时有意义。
    void run();
    /// 等价于对 Replica 调用 stop()。
    void stop();
    /**
     * @brief 请求沙箱进程优雅退出；返回后异步等待子进程真正结束
     *        （见 processFinished 信号），不在本调用内阻塞。
     */
    void shutdown();

    /**
     * @brief 发起一次"重型命令"执行：分配共享内存、写入输入、调用 Replica::executeCommand()。
     * @param commandId     对应沙箱内某个 ISandboxCommandHandler::commandId()
     * @param inputPayload  写入共享内存输入区的数据
     * @param resultCapacity 除 inputPayload 外，额外预留给结果回写的容量
     * @param params        透传给 ISandboxCommandHandler::execute() 的额外参数
     * @return 本次调用的 requestId（用于在 commandFinished 信号里匹配是哪次调用完成了）；
     *         共享内存分配失败时返回空字符串，此时不会触发 executeCommand()。
     */
    QString beginCommand(const QString &commandId, const QByteArray &inputPayload,
                         quint32 resultCapacity = 0, QVariantMap params = {});

    [[nodiscard]] const QString &sandboxId() const noexcept { return m_sandboxId; }
    [[nodiscard]] SandboxPhase phase() const noexcept { return m_phase; }
    [[nodiscard]] qint64 processId() const;

Q_SIGNALS:
    void phaseChanged(SandboxPhase phase);
    void logMessage(int level, const QString &message);
    /// 见 beginCommand()：requestId 与其返回值一一对应，result 已经是从共享内存读出的拷贝。
    void commandFinished(const QString &requestId, bool ok, const QByteArray &result,
                         const QString &errorMessage);
    void faulted(const QString &reason);
    /// 子进程真正退出（QProcess::finished）时触发，携带退出码；之后本对象即可安全析构。
    void processFinished(int exitCode);

private:
    void onReplicaStateChanged();
    void bindReplicaSignals();

private:
    QString m_sandboxId;
    QString m_pluginFilePath;
    SandboxPhase m_phase = SandboxPhase::Connecting;

    QRemoteObjectNode &
        m_registryNode; // 外部注入（通常是 SandboxSystem 持有的注册中心），生命周期由调用方保证长于本对象
    std::unique_ptr<PluginSandboxControlReplica> m_replica;
    std::unique_ptr<QProcess> m_process;

    QVariantMap m_pendingPluginArguments;

    struct PendingCommand;
    std::unordered_map<QString, std::unique_ptr<PendingCommand>> m_pendingCommands;
};

} // namespace bakuon::sandbox
