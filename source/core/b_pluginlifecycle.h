#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

namespace bakuon::core {

enum class PluginState : std::uint8_t {
    Idle,
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

enum class PluginEvent : std::uint8_t {
    Success,
    Fail,
    StartDiscover,
    StartValidate,
    StartResolve,
    StartLoad,
    StartInitialize,
    StartRun,
    StartStop,
    StartUnload,
};

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
                return S::Running;
            if (event == E::StartStop)
                return S::Stopping;
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

    static constexpr bool isFailed(PluginState s) noexcept
    {
        switch (s) {
        case PluginState::DiscoverFailed:
        case PluginState::ValidateFailed:
        case PluginState::ResolveFailed:
        case PluginState::LoadFailed:
        case PluginState::InitializeFailed:
        case PluginState::RunFailed:
        case PluginState::StopFailed:
        case PluginState::UnloadFailed    : return true;
        default                           : return false;
        }
    }

    static constexpr bool isTransient(PluginState s) noexcept
    {
        switch (s) {
        case PluginState::Discovering:
        case PluginState::Validating:
        case PluginState::Resolving:
        case PluginState::Loading:
        case PluginState::Initializing:
        case PluginState::Stopping:
        case PluginState::Unloading   : return true;
        default                       : return false;
        }
    }
};

[[nodiscard]] constexpr std::string_view toStringView(PluginState state) noexcept
{
    using S = PluginState;
    switch (state) {
    case S::Idle            : return "Idle";
    case S::Discovering     : return "Discovering";
    case S::Discovered      : return "Discovered";
    case S::DiscoverFailed  : return "DiscoverFailed";
    case S::Validating      : return "Validating";
    case S::Validated       : return "Validated";
    case S::ValidateFailed  : return "ValidateFailed";
    case S::Resolving       : return "Resolving";
    case S::Resolved        : return "Resolved";
    case S::ResolveFailed   : return "ResolveFailed";
    case S::Loading         : return "Loading";
    case S::Loaded          : return "Loaded";
    case S::LoadFailed      : return "LoadFailed";
    case S::Initializing    : return "Initializing";
    case S::Initialized     : return "Initialized";
    case S::InitializeFailed: return "InitializeFailed";
    case S::Running         : return "Running";
    case S::RunFailed       : return "RunFailed";
    case S::Stopping        : return "Stopping";
    case S::Stopped         : return "Stopped";
    case S::StopFailed      : return "StopFailed";
    case S::Unloading       : return "Unloading";
    case S::Unloaded        : return "Unloaded";
    case S::UnloadFailed    : return "UnloadFailed";
    }
    return "<unknown PluginState>";
}

[[nodiscard]] constexpr std::string_view toStringView(PluginEvent event) noexcept
{
    using E = PluginEvent;
    switch (event) {
    case E::Success        : return "Success";
    case E::Fail           : return "Fail";
    case E::StartDiscover  : return "StartDiscover";
    case E::StartValidate  : return "StartValidate";
    case E::StartResolve   : return "StartResolve";
    case E::StartLoad      : return "StartLoad";
    case E::StartInitialize: return "StartInitialize";
    case E::StartRun       : return "StartRun";
    case E::StartStop      : return "StartStop";
    case E::StartUnload    : return "StartUnload";
    }
    return "<unknown PluginEvent>";
}

} // namespace bakuon::core
