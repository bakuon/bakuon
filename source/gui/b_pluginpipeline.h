#pragma once

#include <deque>
#include <functional>
#include <memory>
#include <optional>

#include <QtCore/QDateTime>
#include <QtCore/QMetaType>
#include <QtCore/QObject>
#include <QtCore/QPluginLoader>
#include <QtCore/QString>
#include <QtCore/QStringList>

#include <bakuon/gui/IPlugin.h>
#include <bakuon/gui/PluginContext.h>

#include "gui/b_pluginmetadata.h"

namespace bakuon::gui {

/// ----------------------------------------------------------------------------
/// Pipeline 管道流转
///  事件是以 “多米诺骨牌（Pipeline）” 方式流转的,分为两类状态角色：
/// 1.过程态（-ing 状态，如 Validating, Resolving）：这类状态由主动事件（
/// 如 StartValidate）触发进入。进入后，状态机立刻执行对应的阻塞同步业务函数
/// （如执行 executeValidate()）。业务函数执行完毕后，根据其内部的
/// true/false 结果，主动向状态机投递 Success 或 Fail 事件。
///
/// 2.稳定态/结果态（-ed 状态，如 Validated, Resolved, Loaded）：这类状态由上一阶段
/// 的 Success 事件驱动进入。一旦进入结果态，onStateEntered/stateReact 路由表会
/// 立刻自动向下投递下一阶段的启动事件（如进入 Validated 后自动投递 StartResolve）。
///
/// 这种链式流转的优势：
/// 调用者完全解耦：宿主只需要在开始时按一下开关（launch()），整个复杂的校验、
/// 解析、加载、GUI初始化链条就会自动一环扣一环地安全运行。重试极其简单：如果
/// 执行 executeLoad 失败，状态机会停在 LoadFailed。由于业务解耦，
/// 只需在 handleFailure() 中再次调用 handleEvent(PluginEvent::StartLoad)，
/// 整个加载及后续的流水线就会自动重新运转，无需重写任何逻辑。
/// -----------------------------------------------------------------------------

/**
 * 插件生命周期状态转换：
 *
 * Idle
 *    ↓ StartDiscover
 * Discovering → DiscoverFailed
 *    ↓
 * Discovered
 *    ↓ (自动)
 * Validating → ValidateFailed
 *    ↓
 * Validated
 *    ↓ (自动)
 * Resolving → ResolveFailed
 *    ↓
 * Resolved
 *    ↓ (自动)
 * Loading  → LoadFailed
 *    ↓
 * Loaded
 *    ↓ (自动)
 * Initializing → InitializeFailed
 *    ↓
 * Initialized  ────────┐ 到这里停下来，等待外部显式 StartRun（见下方“为什么这里不自动前进”）
 *    ↓ StartRun        │
 * Running → RunFailed  │
 *    ↓ StartStop       │
 * Stopping → StopFailed│
 *    ↓                 │
 * Stopped ─────────────┘ 同样停下来，等待外部显式 StartUnload（“停用”和“卸载”是两个不同的host决策）
 *    ↓ StartUnload
 * Unloading → UnloadFailed
 *    ↓
 * Unloaded（终态）
 *
 * ## 为什么 Initialized → Running 和 Stopped → Unloading 不自动前进
 * 其余所有 "-ed" 稳定态都会自动投递下一阶段的 Start 事件，形成链式推进（调用方只需要 launch()
 * 一次）。这两处是刻意的例外：
 *  - IPlugin::initialize() 的约定是"此时其他插件可能尚未完成初始化，不应依赖其他插件提供的服务"，
 *    只有 extensionsInitialized()（对应进入 Running）才允许跨插件交互。如果每个插件各自独立地
 *    自动从 Initialized 跑到 Running，就没有任何机制保证"所有插件都初始化完了才能互相访问"——
 *    这必须由持有全局视角的 PluginSystem 统一协调（见 b_pluginsystem.cpp 的 runAll()）。
 *  - Stopped → Unloading：“停用一个插件”（不再运行，但动态库还在内存里，随时可以重新 run()）
 *    和“彻底卸载它”（连动态库都释放掉）是两个不同的host级决策，不应该被强行绑在一起。
 */
enum class PluginState {
    Idle, // 尚未开始（构造后的初始状态；内置插件跳过 Discovering/Validating，直接从 Validated 开始）

    Discovering,
    Discovered,
    DiscoverFailed,

    Validating,
    Validated,
    ValidateFailed,

    Resolving,
    Resolved,
    ResolveFailed,

    Loading,
    Loaded,
    LoadFailed,

    Initializing,
    Initialized,
    InitializeFailed,

    Running,
    RunFailed,

    Stopping,
    Stopped,
    StopFailed,

    Unloading,
    Unloaded,
    UnloadFailed,
};

/// 调用者只需要关心"发生了什么事"，不需要知道当前处于什么状态、下一步该去哪。
enum class PluginEvent {
    Success, // 当前 "-ing" 阶段的业务动作执行成功
    Fail,    // 当前 "-ing" 阶段的业务动作执行失败

    StartDiscover,
    StartValidate,
    StartResolve,
    StartLoad,
    StartInitialize,
    StartRun,
    StartStop,
    StartUnload,
};

/**
 * @brief 事件驱动的状态转移法则：只依赖 [当前状态] 和 [输入事件]，不依赖任何外部上下文，
 *        因此可以是一个纯 constexpr 函数——转移表本身就是可以脱离真实插件单独测试、
 *        甚至编译期验证的"文档"。非法转换返回 std::nullopt，调用方（PluginPipeline::handle）
 *        据此拒绝该事件、保持状态不变。
 */
struct PluginLifecycleRules
{
    static constexpr std::optional<PluginState> nextState(PluginState from,
                                                          PluginEvent event) noexcept
    {
        using S = PluginState;
        using E = PluginEvent;

        switch (from) {
        case S::Idle:
            if (event == E::StartDiscover)
                return S::Discovering;
            break;

        case S::Discovering:
            if (event == E::Success)
                return S::Discovered;
            if (event == E::Fail)
                return S::DiscoverFailed;
            break;
        case S::DiscoverFailed:
            if (event == E::StartDiscover)
                return S::Discovering;
            break;

        case S::Discovered:
            if (event == E::StartValidate)
                return S::Validating;
            break;
        case S::Validating:
            if (event == E::Success)
                return S::Validated;
            if (event == E::Fail)
                return S::ValidateFailed;
            break;
        case S::ValidateFailed:
            if (event == E::StartValidate)
                return S::Validating;
            break;

        case S::Validated:
            if (event == E::StartResolve)
                return S::Resolving;
            break;
        case S::Resolving:
            if (event == E::Success)
                return S::Resolved;
            if (event == E::Fail)
                return S::ResolveFailed;
            break;
        case S::ResolveFailed:
            if (event == E::StartResolve)
                return S::Resolving;
            break;

        case S::Resolved:
            if (event == E::StartLoad)
                return S::Loading;
            break;
        case S::Loading:
            if (event == E::Success)
                return S::Loaded;
            if (event == E::Fail)
                return S::LoadFailed;
            break;
        case S::LoadFailed:
            if (event == E::StartLoad)
                return S::Loading;
            break;

        case S::Loaded:
            if (event == E::StartInitialize)
                return S::Initializing;
            break;
        case S::Initializing:
            if (event == E::Success)
                return S::Initialized;
            if (event == E::Fail)
                return S::InitializeFailed;
            break;
        case S::InitializeFailed:
            if (event == E::StartInitialize)
                return S::Initializing;
            break;

        case S::Initialized:
            if (event == E::StartRun)
                return S::Running;
            break;
        case S::Running:
            if (event == E::StartStop)
                return S::Stopping;
            if (event == E::Fail)
                return S::RunFailed;
            break;
        case S::RunFailed:
            if (event == E::StartRun)
                return S::Running; // 重试运行
            if (event == E::StartStop)
                return S::Stopping; // 放弃重试，直接停止
            break;

        case S::Stopping:
            if (event == E::Success)
                return S::Stopped;
            if (event == E::Fail)
                return S::StopFailed;
            break;
        case S::StopFailed:
            if (event == E::StartStop)
                return S::Stopping;
            break;

        case S::Stopped:
            if (event == E::StartUnload)
                return S::Unloading;
            break;
        case S::Unloading:
            if (event == E::Success)
                return S::Unloaded;
            if (event == E::Fail)
                return S::UnloadFailed;
            break;
        case S::UnloadFailed:
            if (event == E::StartUnload)
                return S::Unloading;
            break;

        case S::Unloaded:
        default         : break;
        }
        return std::nullopt;
    }
};

/// 供日志/UI/测试使用；不参与状态机逻辑本身。
QString toString(PluginState state);

/**
 * @brief 单个插件从发现到卸载的完整生命周期管道。
 *
 * 用法（动态库插件）：
 *   PluginPipeline pipeline(id, filePath);
 *   pipeline.launch();           // 自动跑完 Discovering → ... → Initialized
 *   // ...（PluginSystem 确认所有插件都 Initialized 后）
 *   pipeline.run();              // Initialized → Running
 *   // ...
 *   pipeline.stop();             // Running → Stopping → Stopped
 *   pipeline.unloadNow();        // Stopped → Unloading → Unloaded
 *
 * 用法（内置插件）：构造时直接跳到 Validated（没有文件可发现/校验），launch() 会自动识别
 * 当前状态、从 Resolving 开始跑，其余完全一致。
 *
 * 失败重试：任何 *Failed 状态都可以通过重新投递对应的 Start* 事件重试（比如
 * pipeline.handle(PluginEvent::StartLoad) 从 LoadFailed 重新尝试加载），不需要重建对象、
 * 不需要重新走前面已经成功的阶段。
 *
 * 单独使用本类完全不需要 PluginSystem——setResolveHook() 不设置时，Resolving 阶段永远直接通过，
 * 适合脚本/测试场景只想跑单个插件、不关心跨插件依赖的情况。
 */
class PluginPipeline : public QObject
{
    Q_OBJECT
public:
    /// 动态库插件。
    PluginPipeline(size_t id, QString filePath, QObject *parent = nullptr);
    /// 内置插件：没有文件可发现，构造后直接处于 Validated 状态，metadata 从 instance 读取。
    PluginPipeline(size_t id, std::shared_ptr<IPlugin> instance, QObject *parent = nullptr);
    ~PluginPipeline() override;

    [[nodiscard]] size_t id() const noexcept { return m_id; }
    [[nodiscard]] PluginState state() const noexcept { return m_state; }
    [[nodiscard]] const QString &filePath() const noexcept { return m_filePath; }
    [[nodiscard]] const PluginMetadata &metadata() const noexcept { return m_metadata; }
    [[nodiscard]] QString lastError() const { return m_lastError; }
    [[nodiscard]] bool isFailed() const noexcept;

    [[nodiscard]] std::optional<QDateTime> discoveredAt() const noexcept { return m_discoveredAt; }
    [[nodiscard]] std::optional<QDateTime> loadedAt() const noexcept { return m_loadedAt; }
    [[nodiscard]] std::optional<QDateTime> initializedAt() const noexcept
    {
        return m_initializedAt;
    }
    [[nodiscard]] std::optional<QDateTime> runningAt() const noexcept { return m_runningAt; }

    /**
     * @brief 获取当前设置的运行时命令行参数值。
     * @see PluginArgument
     */
    [[nodiscard]] QStringList argumentValues() const noexcept { return m_argumentValues; }
    /**
     * @brief 设置运行时命令行参数值，Initializing 阶段会用它构造 PluginContext。
     * @note 通常由 PluginSystem 在 Validated 时机根据 metadata().id 计算好后下发
     *       （见 b_pluginsystem.cpp 的 argumentsFor()），不需要调用方手动拼接；
     *       单独使用 PluginPipeline（不经过 PluginSystem）时可以直接调用本方法自己传值。
     *       调用时机没有限制——哪怕已经 Initialized，重新设置后走 InitializeFailed 重试也会用新值。
     */
    void setArgumentValues(QStringList values) { m_argumentValues = std::move(values); }

    /**
     * @brief 依赖解析钩子：返回 std::nullopt 表示通过，否则是失败原因（写入 lastError()）。
     * PluginSystem 会注入一个查询自身注册表 + 循环依赖检测的实现；不设置时 Resolving 阶段
     * 永远直接通过（不做任何跨插件校验），见类头部"单独使用"的说明。
     */
    using ResolveHook = std::function<std::optional<QString>(const PluginMetadata &)>;
    void setResolveHook(ResolveHook hook) { m_resolveHook = std::move(hook); }

    /**
     * @brief 驱动状态机的唯一入口。
     * @return 非法转换返回 false、状态不变（原因见 lastError()）。
     */
    bool handle(PluginEvent event);

    /**
     * @brief 便捷方法：从当前状态自动继续（Idle 用 StartDiscover；Validated 用 StartResolve，
     * 覆盖"从头开始"和"内置插件跳过发现"两种情况），一路跑到 Initialized 为止。
     * @note 语义如 start(), 对应 end()
     */
    bool launch();

    /**
     * @brief 便捷方法：等价于 handle(StartRun)。
     */
    bool run();

    /**
     * @brief 便捷方法：等价于 handle(StartStop)。
     */
    bool stop();

    /**
     * @brief 便捷方法：等价于 handle(StartUnload)。
     * @note 语义如 end(), 对应 start()
     */
    bool unload();

Q_SIGNALS:
    void stateChanged(size_t id, PluginState state);
    void running(size_t id);
    void failed(size_t id, PluginState state, const QString &reason);

private:
    bool processQueue();
    bool stateReact();
    void recordTimestamp(PluginState state);

    // 管线分流
    void executeDiscover();
    void executeValidate();
    void executeResolve();
    void executeLoad();
    void executeInitialize();
    void executeStop();
    void executeUnload();

private:
    size_t m_id;
    QString m_filePath;
    PluginState m_state = PluginState::Idle;
    QString m_lastError;
    PluginMetadata m_metadata;
    ResolveHook m_resolveHook;

    std::optional<QDateTime> m_discoveredAt;
    std::optional<QDateTime> m_loadedAt;
    std::optional<QDateTime> m_initializedAt;
    std::optional<QDateTime> m_runningAt;

    std::unique_ptr<QPluginLoader> m_loader; // nullptr：内置插件，或动态库插件尚未到达 Loading
    std::shared_ptr<IPlugin> m_instance;     // Loading 成功后设置（内置插件构造时就已设置）
    QStringList m_argumentValues;            // Initializing 阶段传给 PluginContext 的命令行参数

    // 用队列代替递归，避免 handle()/execute_*() 互相调用时栈深不受控
    std::deque<PluginEvent> m_pendingEvents;
    bool m_processing = false;
};

} // namespace bakuon::gui

Q_DECLARE_METATYPE(bakuon::gui::PluginState)
