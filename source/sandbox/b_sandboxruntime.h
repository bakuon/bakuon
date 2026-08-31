#pragma once

#include <memory>
#include <optional>

#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtCore/QUrl>

QT_BEGIN_NAMESPACE
class QRemoteObjectHost;
QT_END_NAMESPACE

namespace bakuon::sandbox {

/**
 * @brief sandbox_runtime 可执行文件的核心逻辑：运行在被 SandboxSupervisor
 *        spawn 出来的子进程里，是 PluginSandboxControl 契约里真正承载业务
 *        逻辑的一端。
 *
 * ## 为什么这里是 QRemoteObjectHost 而不是需求原文里的 QRemoteObjectNode
 * 见 pluginsandboxcontrol.rep 顶部注释——经实测验证，QtRO 点对点连接下只有
 * 监听/发布（enableRemoting）的一端才能把对象开放给已连接的对端 acquire()。
 * 真正执行 loadPlugin()/run()/executeCommand() 等业务逻辑的一端必须是
 * enableRemoting() 的那一端，所以 Sandbox 子进程实际构造的是
 * QRemoteObjectHost（只调用 setHostUrl() 监听，不主动 connectToNode()
 * 连出去），Host 主程序反而只需要一个轻量 QRemoteObjectNode
 * connectToNode() 过来 acquire。
 *
 * ## 与既有 PluginPipeline 的关系
 * 加载真实 IPlugin 完全复用 gui::PluginPipeline 现成的状态机（Discovering→
 * ...→Running→...→Unloaded），不重新发明一套"进程外加载器"——沙箱进程内部
 * 看到的插件加载流程与主进程内加载插件完全一致，只是这条流水线现在跑在另一个
 * 操作系统进程里。SandboxPhase（见 .rep）只是 PluginPipeline::PluginState
 * 的粗粒度对外投影，供 Host 侧驱动"该调用哪个 slot"，不重复维护一套细粒度
 * 状态机。
 */
class SandboxRuntime : public QObject
{
    Q_OBJECT
public:
    explicit SandboxRuntime(QString sandboxId, QObject *parent = nullptr);
    ~SandboxRuntime() override;

    SandboxRuntime(const SandboxRuntime &)            = delete;
    SandboxRuntime &operator=(const SandboxRuntime &) = delete;

    /**
     * @brief 在给定地址上监听并发布 PluginSandboxControl 契约的 Source 实现。
     * @param listenUrl  见 makeSandboxListenUrl()；本进程自己的监听地址，
     *                   供 Replica 的实际数据连接落地（注册中心只负责"告诉对端去哪连"，
     *                   不代替这个地址本身，所以依然需要）。
     * @param registryUrl 见 registryUrl()；引入 QRemoteObjectRegistryHost 之后，
     *                   本进程不再需要知道 Host 主程序的地址，只需要 setRegistryUrl()
     *                   到这个固定的注册中心地址，Host 侧就能通过注册中心按对象名
     *                   acquire() 到本进程发布的 Source，不需要双方预先交换地址。
     * @return 成功返回 std::nullopt；失败返回错误描述（典型原因：地址已被占用）。
     */
    [[nodiscard]] std::optional<QString> start(const QUrl &listenUrl, const QUrl &registryUrl);

Q_SIGNALS:
    /// Host 调用 shutdownSandbox() 后触发；sandbox_runtime/main.cpp 据此退出事件循环。
    void shutdownRequested();

private:
    QString m_sandboxId;
    std::unique_ptr<QRemoteObjectHost> m_host;
    // SandboxControlSourceImpl 定义在 .cpp 里（Q_OBJECT + moc include 模式，见该文件顶部
    // 说明），故意不作为 SandboxRuntime 的嵌套类——嵌套类 + Q_OBJECT + 类外定义会让 moc
    // 的名字解析变复杂，独立的命名空间作用域类型更符合 moc 的常规使用方式。
    std::unique_ptr<class SandboxControlSourceImpl> m_source;
};

} // namespace bakuon::sandbox
