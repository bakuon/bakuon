#include "sandbox/b_sandboxruntime.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QTimer>
#include <QtCore/QVariantMap>
#include <QtGui/QMouseEvent>
#include <QtGui/QWheelEvent>
#include <QtRemoteObjects/QRemoteObjectHost>
#include <QtWidgets/QWidget>

#include <bakuon/gui/IPlugin.h>
#include <bakuon/sandbox/IGuiSurfaceHandler.h>
#include <bakuon/sandbox/ISandboxCommandHandler.h>

#include "gui/b_extensionsystem.h"
#include "gui/b_pluginpipeline.h"
#include "sandbox/b_guisurfaceevents.h"
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
        // 同样的模式注册 GUI 表面扩展点，见 IGuiSurfaceHandler.h 顶部说明。
        m_guiSurfaceHandlers = gui::ExtensionSystem::instance()
                                   .registerDefaultExtensionPoint<IGuiSurfaceHandler>(
                                       "沙箱内 GUI 表面扩展点");
        if (!m_guiSurfaceHandlers) {
            m_guiSurfaceHandlers = gui::ExtensionSystem::instance()
                                       .extensionPoint<IGuiSurfaceHandler>();
        }
        setPid(QCoreApplication::applicationPid());
    }

    void loadPlugin(QString filePath, QVariantMap arguments) override
    {
        if (m_pipeline) {
            Q_EMIT logMessage(1 /*Warning*/,
                              QStringLiteral("loadPlugin 重复调用，忽略（已加载 %1）")
                                  .arg(pluginId()));
            return;
        }

        setPhase(SandboxPhase::Loading);
        setProgress(10);

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
        setProgress(40);
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
        setProgress(100);
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
        startGuiSurfaceCaptureIfAvailable();
    }

    void stop() override
    {
        stopGuiSurfaceCapture();
        setPhase(SandboxPhase::Stopping);
        if (m_pipeline) {
            m_pipeline->stop();
        }
        setPhase(SandboxPhase::Stopped);
    }

    void shutdownSandbox() override
    {
        stopGuiSurfaceCapture();
        if (m_pipeline) {
            // Stopped 才允许 unload()（见 PluginPipeline 状态机），Running 时先补一次 stop()。
            m_pipeline->stop();
            m_pipeline->unload();
        }
        setPhase(SandboxPhase::Stopped); // TODO: Unload or Shutdown phase
        // 真正退出进程的动作交给 sandbox_runtime/main.cpp（本类只负责契约语义，
        // 进程生命周期是宿主 main() 的职责，保持单一职责）。
        Q_EMIT aboutToQuit();
    }

    void dispatchInputEvent(int type, QPoint pos, int button, int modifiers, int key,
                            QString text) override
    {
        if (!m_surfaceWidget) {
            return; // 没有 GUI 表面（插件没注册 IGuiSurfaceHandler，或者还没到 Running），忽略
        }

        const auto qtButton    = toQtMouseButton(button);
        const auto qtModifiers = toQtKeyModifiers(modifiers);

        switch (static_cast<GuiInputEventType>(type)) {
        case GuiInputEventType::MouseMove: {
            QMouseEvent ev(QEvent::MouseMove,
                           QPointF(pos),
                           QPointF(pos),
                           Qt::MouseButton::NoButton,
                           qtButton,
                           qtModifiers);
            QCoreApplication::sendEvent(m_surfaceWidget, &ev);
            break;
        }
        case GuiInputEventType::MousePress: {
            const Qt::MouseButton primary = toSingleQtMouseButton(button);
            QMouseEvent ev(QEvent::MouseButtonPress,
                           QPointF(pos),
                           QPointF(pos),
                           primary,
                           qtButton,
                           qtModifiers);
            QCoreApplication::sendEvent(m_surfaceWidget, &ev);
            break;
        }
        case GuiInputEventType::MouseRelease: {
            const Qt::MouseButton primary = toSingleQtMouseButton(button);
            QMouseEvent ev(QEvent::MouseButtonRelease,
                           QPointF(pos),
                           QPointF(pos),
                           primary,
                           Qt::MouseButton::NoButton,
                           qtModifiers);
            QCoreApplication::sendEvent(m_surfaceWidget, &ev);
            break;
        }
        case GuiInputEventType::Wheel: {
            QWheelEvent ev(QPointF(pos),
                           QPointF(pos),
                           QPoint(),
                           QPoint(0, key),
                           qtButton,
                           qtModifiers,
                           Qt::NoScrollPhase,
                           false);
            QCoreApplication::sendEvent(m_surfaceWidget, &ev);
            break;
        }
        case GuiInputEventType::KeyPress: {
            QKeyEvent ev(QEvent::KeyPress, key, qtModifiers, text);
            QCoreApplication::sendEvent(m_surfaceWidget, &ev);
            break;
        }
        case GuiInputEventType::KeyRelease: {
            QKeyEvent ev(QEvent::KeyRelease, key, qtModifiers, text);
            QCoreApplication::sendEvent(m_surfaceWidget, &ev);
            break;
        }
        }
        // 输入可能改变了界面外观（比如按钮按下的高亮态），下一次定时抓帧会自然带上
        // 这次变化——v1 按固定频率抓帧，不在这里额外触发一次立即抓帧，见类注释。
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
    /**
     * @brief run() 成功后调用一次：如果插件注册了 IGuiSurfaceHandler，
     * 把它的 widget 定住固定尺寸、创建帧缓冲共享内存段、启动周期性抓帧定时器。
     *
     * v1 已知限制（后续优化方向，不在本次范围内）：
     *  1. 固定尺寸、固定帧率轮询，不支持运行期 resize()，不做"内容有没有变化"
     *     的脏检测——每一帧都无条件抓取+发送，即使界面完全静止。
     *  2. dirtyRect 恒等于整帧范围，没有做真正的脏矩形裁剪。
     *  3. 同一沙箱进程只取第一个注册的 IGuiSurfaceHandler。
     * 这些都是为了先把"链路通不通"跑通、有意收窄的范围，见
     * IGuiSurfaceHandler.h 和 pluginsandboxcontrol.rep 里 frameReady 的说明。
     */
    /**
     * @details run() 成功后调用一次：如果插件注册了 IGuiSurfaceHandler，
     * 把它的 widget 定住固定尺寸、创建帧缓冲共享内存段、安装脏区域监听、
     * 启动周期性抓帧定时器。
     *
     * v1.1 更新：已经做了变化检测 + 真脏矩形裁剪，见 eventFilter()/captureAndSendFrame()。
     * 仍然保留的已知限制（后续优化方向，不在本次范围内）：
     *  1. 固定尺寸，不支持运行期 resize()。
     *  2. 固定 10fps 轮询上限（有变化才真正抓帧发送，但检测本身仍然是定时轮询，
     *     不是每次 update() 立即触发——这是刻意的节流，避免密集重绘时每次都
     *     发一帧，把带宽让给"按最高 10fps 合并发送"）。
     *  3. 同一沙箱进程只取第一个注册的 IGuiSurfaceHandler。
     * 这些都是为了先把"链路通不通"跑通、有意收窄的范围，见
     * IGuiSurfaceHandler.h 和 pluginsandboxcontrol.rep 里 frameReady 的说明。
     */
    void startGuiSurfaceCaptureIfAvailable()
    {
        if (m_surfaceWidget) {
            return; // 已经启动过了（比如 stop() 之后又 run() 一次），不重复初始化
        }
        if (!m_guiSurfaceHandlers) {
            return;
        }
        const auto handlers = m_guiSurfaceHandlers->extensions(
            [](const std::shared_ptr<IGuiSurfaceHandler> &) { return true; });
        if (handlers.empty()) {
            return; // 插件没有注册 GUI 表面，纯后台/命令行式插件，正常情况，不是错误
        }

        m_surfaceWidget = handlers.front()->surfaceWidget();
        if (!m_surfaceWidget) {
            Q_EMIT logMessage(1 /*Warning*/,
                              QStringLiteral("IGuiSurfaceHandler::surfaceWidget() 返回了空指针"));
            return;
        }

        // 固定尺寸：见类文档"已知限制"第 1 条。480x360 只是一个能验证链路的合理初始值。
        m_surfaceWidget->resize(kSurfaceWidth, kSurfaceHeight);
        // 关键一步：Qt 的 update()/重绘调度机制只对"可见"widget 生效——一个从未
        // show() 过的 widget，调用 update() 不会真正产生 QEvent::Paint（Qt 认为
        // "反正没人看得见，调度它干什么"，直接丢弃这次请求，widget 变成不可见时
        // 后续的 update() 调用全部沉默失败）。但本类的整个设计前提就是这个 widget
        // 永远不能真的显示到屏幕上（这是运行在 `-platform offscreen` 下的沙箱进程，
        // 压根没有真实窗口系统）。`Qt::WA_DontShowOnScreen` 是 Qt 官方为这类场景
        // 提供的属性：让 widget 在内部各种事件/调度逻辑里被当成"已经 show() 过"
        // 处理（因此 update() 能正常触发真正的 QEvent::Paint），但不会真的创建/
        // 合成任何原生窗口——不加这一步，本类依赖 QEvent::Paint 做的变化检测/
        // 脏矩形裁剪会在第一帧之后完全失效（第一帧能画出来是因为 grab() 自己
        // 强制渲染，绕过了这层可见性判断；之后 widget 自己 update() 调度的重绘
        // 则一直被无声丢弃）。
        m_surfaceWidget->setAttribute(Qt::WA_DontShowOnScreen, true);
        m_surfaceWidget->show();

        const QString frameKey   = makeFrameMemoryKey(m_sandboxId);
        const quint32 frameBytes = static_cast<quint32>(kSurfaceWidth)
                                   * static_cast<quint32>(kSurfaceHeight) * 4;
        if (auto err = m_frameChannel.create(frameKey, QByteArray(), frameBytes)) {
            Q_EMIT logMessage(2 /*Error*/, QStringLiteral("帧缓冲共享内存创建失败：%1").arg(*err));
            m_surfaceWidget = nullptr;
            return;
        }

        // 监听 widget 自己的 QEvent::Paint，累积"这段时间内到底哪些区域真的被
        // 重绘过"的并集矩形——这是脏矩形的数据来源：直接问 Qt 自己刚刚画了哪里，
        // 比"抓两帧图逐像素比较找差异"更省、更准确（后者对 480x360 这种小尺寸
        // 影响不大，但语义上"问 Qt 自己"更直接、也不需要额外持有一份"上一帧"
        // 的拷贝）。initialDirtyRect 覆盖整个 widget，保证第一帧发出去的是完整画面
        // （Host 侧此时还没有任何基线画面可以拼接）。
        m_surfaceWidget->installEventFilter(this);
        m_pendingDirtyRect = m_surfaceWidget->rect();

        m_frameTimer = new QTimer(this);
        connect(m_frameTimer,
                &QTimer::timeout,
                this,
                &SandboxControlSourceImpl::captureAndSendFrame);
        m_frameTimer->start(kFrameIntervalMs);
        captureAndSendFrame(); // 立即发一帧，不等第一个定时器 tick，减少用户能感知到的首帧延迟
    }

    void stopGuiSurfaceCapture()
    {
        if (m_frameTimer) {
            m_frameTimer->stop();
            m_frameTimer->deleteLater();
            m_frameTimer = nullptr;
        }
        if (m_surfaceWidget) {
            m_surfaceWidget->removeEventFilter(this);
            m_surfaceWidget->hide(); // 对应 startGuiSurfaceCaptureIfAvailable() 里的 show()
        }
        m_surfaceWidget    = nullptr; // 不 delete：widget 的生命周期属于插件自己，本类只是借用指针
        m_pendingDirtyRect = QRect();
    }

    /// 监听 m_surfaceWidget 的 QEvent::Paint，累积脏区域；只观察不拦截
    /// （永远返回 false，绝不吞掉事件——那会让 widget 真的没画上，画面出错）。
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (watched == m_surfaceWidget && event->type() == QEvent::Paint && !m_capturingFrame) {
            auto *paintEvent   = static_cast<QPaintEvent *>(event);
            m_pendingDirtyRect = m_pendingDirtyRect.united(paintEvent->rect());
        }
        return PluginSandboxControlSimpleSource::eventFilter(watched, event);
    }

    void captureAndSendFrame()
    {
        if (!m_surfaceWidget || m_pendingDirtyRect.isEmpty()) {
            return; // 变化检测：这段时间里 widget 没有任何真实重绘，直接跳过，
                    // 不抓帧、不写共享内存、不发信号——这是本轮优化的核心。
        }

        // 和 widget 自身范围求交：event filter 累积的矩形理论上不会越界，这里只是
        // 防御性裁剪，避免任何边界计算误差导致 grab() 越界。
        const QRect dirtyRect = m_pendingDirtyRect.intersected(m_surfaceWidget->rect());
        m_pendingDirtyRect    = QRect();
        if (dirtyRect.isEmpty()) {
            return;
        }

        m_capturingFrame   = true;
        const QImage image = m_surfaceWidget->grab(dirtyRect).toImage().convertToFormat(
            QImage::Format_ARGB32);
        m_capturingFrame = false;

        const QByteArray raw(reinterpret_cast<const char *>(image.constBits()),
                             static_cast<qsizetype>(image.sizeInBytes()));
        if (auto err = m_frameChannel.writePayload(raw)) {
            Q_EMIT logMessage(1 /*Warning*/, QStringLiteral("帧数据写入共享内存失败：%1").arg(*err));
            return;
        }
        // size 字段的含义是"这次传输的像素数据尺寸"，不是 widget 的完整尺寸——
        // 大多数帧只是局部更新，image.size() 就等于 dirtyRect.size()，Host 侧
        // （TabContentWidget::GuiSurfaceView）按 dirtyRect 的位置把这块小图贴回
        // 自己持有的完整画面缓冲，而不是整体替换，见该类的说明。
        Q_EMIT frameReady(m_frameChannel.key(),
                          image.size(),
                          static_cast<int>(QImage::Format_ARGB32),
                          dirtyRect);
    }

    [[nodiscard]] static Qt::MouseButtons toQtMouseButton(int button)
    {
        Qt::MouseButtons result;
        const auto b = static_cast<GuiMouseButton>(button);
        if ((static_cast<int>(b) & static_cast<int>(GuiMouseButton::Left)) != 0) {
            result |= Qt::LeftButton;
        }
        if ((static_cast<int>(b) & static_cast<int>(GuiMouseButton::Right)) != 0) {
            result |= Qt::RightButton;
        }
        if ((static_cast<int>(b) & static_cast<int>(GuiMouseButton::Middle)) != 0) {
            result |= Qt::MiddleButton;
        }
        return result;
    }

    /// QMouseEvent 的 press/release 事件需要单个"触发本次事件的按钮"，
    /// 和"当前按住的按钮集合"（Qt::MouseButtons，见 toQtMouseButton()）是两个
    /// 不同的参数——按位组合取第一个命中的位即可，契约层面本来就没打算支持
    /// "同一个事件里报告好几个按钮同时按下/松开"这种边界情况。
    [[nodiscard]] static Qt::MouseButton toSingleQtMouseButton(int button)
    {
        const auto b = static_cast<GuiMouseButton>(button);
        if ((static_cast<int>(b) & static_cast<int>(GuiMouseButton::Left)) != 0) {
            return Qt::LeftButton;
        }
        if ((static_cast<int>(b) & static_cast<int>(GuiMouseButton::Right)) != 0) {
            return Qt::RightButton;
        }
        if ((static_cast<int>(b) & static_cast<int>(GuiMouseButton::Middle)) != 0) {
            return Qt::MiddleButton;
        }
        return Qt::NoButton;
    }

    [[nodiscard]] static Qt::KeyboardModifiers toQtKeyModifiers(int modifiers)
    {
        Qt::KeyboardModifiers result;
        const auto m = static_cast<GuiKeyModifier>(modifiers);
        if ((static_cast<int>(m) & static_cast<int>(GuiKeyModifier::Shift)) != 0) {
            result |= Qt::ShiftModifier;
        }
        if ((static_cast<int>(m) & static_cast<int>(GuiKeyModifier::Ctrl)) != 0) {
            result |= Qt::ControlModifier;
        }
        if ((static_cast<int>(m) & static_cast<int>(GuiKeyModifier::Alt)) != 0) {
            result |= Qt::AltModifier;
        }
        return result;
    }

private:
    static constexpr int kSurfaceWidth    = 480;
    static constexpr int kSurfaceHeight   = 360;
    static constexpr int kFrameIntervalMs = 100; // 约 10fps，v1 固定频率，见"已知限制"

    QString m_sandboxId;
    std::shared_ptr<gui::PluginPipeline> m_pipeline;
    std::shared_ptr<gui::IExtensionPoint<ISandboxCommandHandler>> m_commandHandlers;
    std::shared_ptr<gui::IExtensionPoint<IGuiSurfaceHandler>> m_guiSurfaceHandlers;
    QWidget *m_surfaceWidget = nullptr; // 借用指针，生命周期属于插件
    SharedMemoryChannel m_frameChannel;
    QTimer *m_frameTimer = nullptr;
    QRect m_pendingDirtyRect;      // 自上次发送以来累积的脏区域（并集）
    bool m_capturingFrame = false; // grab() 期间守卫，见 eventFilter() 的说明
};

SandboxRuntime::SandboxRuntime(QString sandboxId, QObject *parent)
    : QObject(parent)
    , m_sandboxId(std::move(sandboxId))
{
}

SandboxRuntime::~SandboxRuntime() = default;

std::optional<QString> SandboxRuntime::start(const QUrl &listenUrl, const QUrl &registryUrl)
{
    m_host = std::make_unique<QRemoteObjectHost>(listenUrl);
    if (m_host->hostUrl() != listenUrl) {
        return QStringLiteral("QRemoteObjectHost 监听 %1 失败（地址可能已被占用）")
            .arg(listenUrl.toString());
    }

    // 引入注册中心：本进程不再需要 Host 主程序的地址，只需要向注册中心报到；
    // Host 侧凭对象名（makeSandboxObjectName(sandboxId)）就能找到本进程发布的 Source，
    // 具体的实际数据连接地址由注册中心在背后转达，不需要我们自己处理。
    if (!m_host->setRegistryUrl(registryUrl)) {
        return QStringLiteral("setRegistryUrl(%1) 失败").arg(registryUrl.toString());
    }

    m_source = std::make_unique<SandboxControlSourceImpl>(m_sandboxId);
    connect(m_source.get(),
            &SandboxControlSourceImpl::aboutToQuit,
            this,
            &SandboxRuntime::shutdownRequested);

    // 对象名必须带上 sandboxId：同一个注册中心下可能同时挂着多个沙箱实例，
    // 都发布 PluginSandboxControl 契约，用固定的类名当对象名会互相覆盖。
    const QString objectName = makeSandboxObjectName(m_sandboxId);
    if (!m_host->enableRemoting(m_source.get(), objectName)) {
        return QStringLiteral("enableRemoting(%1) 失败").arg(objectName);
    }
    return std::nullopt;
}

} // namespace bakuon::sandbox

#include "b_sandboxruntime.moc"
