#include "sandbox/b_sandboxruntime.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QVariantMap>
#include <QtRemoteObjects/QRemoteObjectHost>

#include <bakuon/gui/IPlugin.h>
#include <bakuon/sandbox/ISandboxCommandHandler.h>

#include "gui/b_defaultextensionpoint.h"
#include "gui/b_extensionsystem.h"
#include "gui/b_pluginpipeline.h"
#include "sandbox/b_sandboxconstants.h"
#include "sandbox/b_sharedmemorychannel.h"

// repc 生成的 Source 端头文件，由 CMakeLists.txt 里的 qt6_add_repc_sources() 驱动生成，
// 落在本 target 的 CMAKE_CURRENT_BINARY_DIR 下（源码里看不到这个文件是正常的）。
#include "rep_b_pluginsandboxcontrol_source.h"

namespace bakuon::sandbox {

namespace {

/**
 * @brief ISandboxCommandContext 的具体实现：包一层 SharedMemoryChannel + Host 下发的 params。
 * @note 仅在 executeCommand() 调用栈内存活，不长期持有。
 */
class CommandContextImpl final : public ISandboxCommandContext
{
public:
    CommandContextImpl(SharedMemoryChannel &channel, QVariantMap params)
        : m_channel(channel)
        , m_params(std::move(params))
    {
    }

    [[nodiscard]] QVariantMap params() const override { return m_params; }
    [[nodiscard]] QByteArray readInput() const override { return m_channel.readPayload(); }

    bool writeResult(const QByteArray &result) override
    {
        return !m_channel.writePayload(result).has_value();
    }

    [[nodiscard]] qsizetype sharedMemoryCapacity() const override { return m_channel.capacity(); }

private:
    SharedMemoryChannel &m_channel;
    QVariantMap m_params;
};

} // namespace

/**
 * @brief PluginSandboxControl 契约的 Source 端真正实现，运行在 Sandbox 子进程里。
 *
 * 职责：
 *  - 复用既有的 gui::PluginPipeline 原地加载/驱动一个真实 IPlugin；
 *  - 把 PluginPipeline 的状态变化映射到契约的粗粒度 SandboxPhase 属性，
 *    自动同步给 Host（QtRO 属性变更会自动推送给所有 Replica）；
 *  - 收到 executeCommand() 后挂载对应共享内存段，通过
 *    IExtensionPoint<ISandboxCommandHandler> 把执行分发给插件自己注册的
 *    具体处理器——这正是"骨架搭好、血肉可插拔"的落地点，本类完全不知道、
 *    也不需要知道任何具体命令的业务含义。
 *
 * Q_OBJECT 类定义在 .cpp 里、文件末尾 #include "b_sandboxruntime.moc"：
 * 这是 Qt 官方支持的写法（AUTOMOC 会扫描 .cpp 里的 Q_OBJECT 宏并生成对应
 * moc_*.cpp，通过这个 include 织入同一个编译单元），目的是不把纯实现细节的
 * SourceImpl 类型暴露进公开头文件，与仓库里 Plugin::Implementation 的 pimpl
 * 风格保持一致。
 */
class SandboxControlSourceImpl final : public PluginSandboxControlSimpleSource
{
    Q_OBJECT
public:
    explicit SandboxControlSourceImpl(QString sandboxId, QObject *parent = nullptr)
        : PluginSandboxControlSimpleSource(parent)
        , m_sandboxId(std::move(sandboxId))
    {
        // ExtensionSystem::instance() 是进程内单例；沙箱是独立 OS 进程，天然与主程序及
        // 其他沙箱隔离，这里统一注册好 ISandboxCommandHandler 扩展点，插件在 initialize()
        // 里直接 extensionPoint<ISandboxCommandHandler>()->registerExtension() 即可，
        // 不需要关心扩展点本身是谁创建的（同 ExtensionSystem 现有的"主程序/核心库负责创建
        // 扩展点，插件负责填充扩展"的既定约定，见 b_extensionsystem.h 类注释）。
        m_commandHandlers = gui::ExtensionSystem::instance()
                                .registerDefaultExtensionPoint<ISandboxCommandHandler>(
                                    "沙箱内命令处理器扩展点");
        if (!m_commandHandlers) {
            // 理论上不会发生（同一沙箱进程只应该有一个 SourceImpl 实例）；
            // 兜底走查询路径，避免因为重复注册导致整个命令分发功能失效。
            m_commandHandlers = gui::ExtensionSystem::instance()
                                    .extensionPoint<ISandboxCommandHandler>();
        }
    }

    void loadPlugin(QString filePath, QVariantMap arguments) override
    {
        setPhase(SandboxPhase::Loading);

        m_pipeline = std::make_shared<gui::PluginPipeline>(1, filePath, this);
        connect(m_pipeline.get(),
                &gui::PluginPipeline::failed,
                this,
                [this](size_t /*id*/, gui::PluginState state, const QString &reason) {
                    Q_EMIT logMessage(2 /*Error*/,
                                      QStringLiteral("插件流水线在阶段 %1 失败：%2")
                                          .arg(gui::toString(state), reason));
                    Q_EMIT faulted(reason);
                    setPhase(SandboxPhase::Faulted);
                });

        // 把 Host 下发的命令行参数（executeCommand 契约之外、loadPlugin 自带的 arguments）
        // 还原成 PluginPipeline::setArgumentValues() 期望的 "--key=value" 形式，
        // 复用既有的 PluginContext::arguments() 通道，不另开一套参数传递机制。
        QStringList argValues;
        for (auto it = arguments.constBegin(); it != arguments.constEnd(); ++it) {
            argValues << QStringLiteral("--%1=%2").arg(it.key(), it.value().toString());
        }
        m_pipeline->setArgumentValues(argValues);

        setPhase(SandboxPhase::Initializing);
        if (!m_pipeline->launch()) {
            // launch() 内部失败时上面连的 failed 信号已经把 phase 打到 Faulted 并上报了原因；
            // 这里兜底一次，防止某些非 *Failed 但仍返回 false 的边界情况下 phase 停留在
            // Initializing 卡死 Host 侧的状态机。
            if (phase() != SandboxPhase::Faulted) {
                setPhase(SandboxPhase::Faulted);
            }
            return;
        }

        setPluginId(m_pipeline->metadata().id);
        setPhase(SandboxPhase::Ready);
        Q_EMIT logMessage(0 /*Info*/,
                          QStringLiteral("插件 %1 已在沙箱进程中加载完成").arg(pluginId()));
    }

    void run() override
    {
        if (!m_pipeline || !m_pipeline->run()) {
            Q_EMIT faulted(QStringLiteral(
                "插件尚未处于可运行状态（未 loadPlugin() 成功或状态不是 Initialized）"));
            setPhase(SandboxPhase::Faulted);
            return;
        }
        setPhase(SandboxPhase::Running);
    }

    void stop() override
    {
        setPhase(SandboxPhase::Stopping);
        if (m_pipeline) {
            m_pipeline->stop();
        }
        setPhase(SandboxPhase::Stopped);
    }

    void shutdownSandbox() override
    {
        if (m_pipeline) {
            // Stopped 才允许 unload()（见 PluginPipeline 状态机），Running 时先补一次 stop()。
            m_pipeline->stop();
            m_pipeline->unload();
        }
        setPhase(SandboxPhase::Stopped);
        // 真正退出进程的动作交给 sandbox_runtime/main.cpp（本类只负责契约语义，
        // 进程生命周期是宿主 main() 的职责，保持单一职责）。
        Q_EMIT aboutToQuit();
    }

    void executeCommand(QString requestId, QString commandId, QString memoryKey,
                        QVariantMap params) override
    {
        SharedMemoryChannel channel;
        if (auto err = channel.attach(memoryKey)) {
            Q_EMIT commandFinished(requestId,
                                   false,
                                   memoryKey,
                                   QStringLiteral("挂载共享内存失败：%1").arg(*err));
            return;
        }

        if (!m_commandHandlers) {
            Q_EMIT commandFinished(requestId,
                                   false,
                                   memoryKey,
                                   QStringLiteral("沙箱内 ISandboxCommandHandler 扩展点不可用"));
            return;
        }

        const auto handlers = m_commandHandlers->extensions(
            [&](const std::shared_ptr<ISandboxCommandHandler> &h) {
                return h && h->commandId() == commandId;
            });

        if (handlers.empty()) {
            Q_EMIT commandFinished(requestId,
                                   false,
                                   memoryKey,
                                   QStringLiteral(
                                       "找不到能处理命令 '%1' 的 ISandboxCommandHandler，"
                                       "请确认插件已在 initialize() 中注册")
                                       .arg(commandId));
            return;
        }

        // 同一 commandId 理论上应当只有一个处理器（第三方插件生态成型后可以在这里加冲突检测/
        // 优先级仲裁，目前先取第一个，行为与 IExtensionPoint::extensions() 的排序约定一致）。
        CommandContextImpl context(channel, std::move(params));
        const auto &handler = handlers.front();
        const bool ok       = handler->execute(context);
        channel.setStatus(ok ? SharedMemoryChannel::StatusOk : SharedMemoryChannel::StatusFailed);
        Q_EMIT commandFinished(requestId, ok, memoryKey, ok ? QString{} : handler->errorMessage());
    }

Q_SIGNALS:
    /// 契约之外的内部信号：通知 SandboxRuntime 可以安全退出事件循环了。
    void aboutToQuit();

private:
    QString m_sandboxId;
    std::shared_ptr<gui::PluginPipeline> m_pipeline;
    std::shared_ptr<gui::IExtensionPoint<ISandboxCommandHandler>> m_commandHandlers;
};

SandboxRuntime::SandboxRuntime(QString sandboxId, QObject *parent)
    : QObject(parent)
    , m_sandboxId(std::move(sandboxId))
{
}

SandboxRuntime::~SandboxRuntime() = default;

std::optional<QString> SandboxRuntime::start(const QUrl &listenUrl)
{
    m_host = std::make_unique<QRemoteObjectHost>(listenUrl);
    if (m_host->hostUrl() != listenUrl) {
        return QStringLiteral("QRemoteObjectHost 监听 %1 失败（地址可能已被占用）")
            .arg(listenUrl.toString());
    }

    m_source = std::make_unique<SandboxControlSourceImpl>(m_sandboxId);
    connect(m_source.get(),
            &SandboxControlSourceImpl::aboutToQuit,
            this,
            &SandboxRuntime::shutdownRequested);

    if (!m_host->enableRemoting(m_source.get(), QString::fromLatin1(kSandboxObjectName))) {
        return QStringLiteral("enableRemoting(%1) 失败").arg(QString::fromLatin1(kSandboxObjectName));
    }
    return std::nullopt;
}

} // namespace bakuon::sandbox

#include "b_sandboxruntime.moc"
