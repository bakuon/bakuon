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
        default:
            break;
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
        case PluginState::UnloadFailed:
            return true;
        default:
            return false;
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
        case PluginState::Unloading:
            return true;
        default:
            return false;
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
