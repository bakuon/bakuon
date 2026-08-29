cmake_minimum_required(VERSION 3.20 FATAL_ERROR)

# ============================================================================
# bakuon::sandbox —— 进程外插件沙箱隔离：QtRO 控制平面 + QSharedMemory 数据平面。
# ============================================================================
project(
    sandbox
    VERSION 1.0.0
    LANGUAGES CXX)

set(CMAKE_AUTOMOC ON)

find_package(
    QT
    NAMES
    Qt6
    Qt5
    COMPONENTS Core RemoteObjects
    REQUIRED)
find_package(
    Qt${QT_VERSION_MAJOR}
    COMPONENTS Core RemoteObjects
    REQUIRED)

bakuon_add_module(
    NAME
    ${PROJECT_NAME}
    PATH
    ${CMAKE_CURRENT_SOURCE_DIR}
    DEPENDS
    Qt${QT_VERSION_MAJOR}::Core
    Qt${QT_VERSION_MAJOR}::RemoteObjects
    bakuon::gui
    # 面向"沙箱内插件作者"的稳定门面头文件根目录：include/bakuon/sandbox/*.h，
    # 约定与 source/gui/CMakeLists.txt 里 bakuon::gui 的 PUBLIC_INCLUDE_DIRS 完全一致。
    PUBLIC_INCLUDE_DIRS
    ${BAKUON_ROOT_DIR}/include)

# ============================================================================
# QtRO 契约代码生成：本模块同时包含 Host 侧（用到 Replica）和 Sandbox 侧
# （用到 Source）逻辑，因此对同一个 target 同时生成两份头文件——
# 分别由 b_sandboxsupervisor.cpp（#include "rep_PluginSandboxControl_replica.h"）
# 和 b_sandboxruntime.cpp（#include "rep_PluginSandboxControl_source.h"）使用。
# 生成的头文件落在 CMAKE_CURRENT_BINARY_DIR，需要额外加进 include 路径
# （bakuon_add_module() 默认不知道这个目录，PUBLIC 是因为 sandbox_runtime/
# 可执行文件不直接使用这些生成头，只有链接了 bakuon::sandbox 的目标间接需要，
# 用 PRIVATE 即可满足，这里选择 PRIVATE 更贴合"内部生成代码不对外泄漏"的原则）。
# ----------------------------------------------------------------------------
#
# 生成产物（构建目录）：
#   rep_PluginSandboxControl_source.h   → PluginSandboxControlSource / PluginSandboxControlSimpleSource
#   rep_PluginSandboxControl_replica.h  → PluginSandboxControlReplica
#
# 必须在 bakuon_add_module 之后调用，以便挂到已存在的 target `sandbox` 上。
# qt6_add_repc_* 会把生成头加入 target 的 include 路径，并注册 moc。
# ============================================================================
set(BAKUON_SANDBOX_REP "${CMAKE_CURRENT_SOURCE_DIR}/PluginSandboxControl.rep")

qt6_add_repc_sources(${PROJECT_NAME} ${BAKUON_SANDBOX_REP})
qt6_add_repc_replicas(${PROJECT_NAME} ${BAKUON_SANDBOX_REP})

# 生成头位于 ${CMAKE_CURRENT_BINARY_DIR}，对 sandbox 目标已由 qt*_add_repc_* 注入；
# 显式再加一次 PUBLIC，方便下游 target（examples / tests）直接 #include "rep_PluginSandbox_*.h"
target_include_directories(${PROJECT_NAME} PRIVATE $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>)

# ============================================================================
# SandBox Runtime 可执行文件构建配置
# ============================================================================
set(SANDBOX_RUNTIME "sandbox_runtime")
add_executable(${SANDBOX_RUNTIME} main.cpp)
target_link_libraries(${SANDBOX_RUNTIME} PRIVATE bakuon::sandbox Qt6::Core Qt6::RemoteObjects)
target_compile_features(${SANDBOX_RUNTIME} PRIVATE cxx_std_20)
bakuon_apply_warnings(${SANDBOX_RUNTIME})
