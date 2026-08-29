cmake_minimum_required(VERSION 3.20 FATAL_ERROR)

# ============================================================================
# sandboxed_example_plugin —— ISandboxCommandHandler 扩展点的最小可用示例。
#
# 只依赖 bakuon::gui：include/bakuon/sandbox/ISandboxCommandHandler.h 是纯头文件
# 接口（没有需要链接的 .cpp 符号），而 bakuon::gui 的 PUBLIC_INCLUDE_DIRS 已经把
# ${BAKUON_ROOT_DIR}/include 整个目录暴露出来了（见 source/gui/CMakeLists.txt），
# 因此不需要、也不应该再链接 bakuon::sandbox 整个静态库——那会把 QtRemoteObjects
# 之类只有 sandbox_runtime 子进程才用得到的依赖一并塞进这个插件的动态库里，
# 违反"依赖应尽量窄"的原则。
# ============================================================================
project(
    sandboxed_example_plugin
    VERSION 1.0.0
    LANGUAGES CXX)

set(CMAKE_AUTOMOC ON)

bakuon_add_plugin(
    NAME
    ${PROJECT_NAME}
    PATH
    ${CMAKE_CURRENT_SOURCE_DIR}
    CATEGORY
    gui
    DEPENDS
    bakuon::gui
    METADATA
    ${CMAKE_CURRENT_SOURCE_DIR}/sandboxed_example_plugin.json)
