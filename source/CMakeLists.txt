cmake_minimum_required(VERSION 3.20 FATAL_ERROR)

# ============================================================================
# bakuon 的三个物理静态库：
#   core   —— 纯标准 C++，无 Qt 依赖（当前是空目录，尚无实现代码，默认不构建）
#   gui    —— 基于 Qt 的 GUI 框架层（command / context / plugin 架构主体）
#   plugin —— 标准 C++ 原生插件基础设施（不含 GUI/Qt 依赖）
#
# core 用 BAKUON_BUILD_CORE 显式开关，而不是简单注释掉 add_subdirectory()：
# 一旦 source/core 下补齐了第一批实现文件，直接 -DBAKUON_BUILD_CORE=ON 即可，
# 好过每次都要来回增删注释、还容易忘记同步。
# ============================================================================
project(
    bakuon
    VERSION 1.0.0
    LANGUAGES CXX)

if(BAKUON_BUILD_CORE)
    add_subdirectory(core)
endif()

if(BAKUON_BUILD_GUI)
    add_subdirectory(gui)
endif()

if(BAKUON_BUILD_PLUGIN)
    add_subdirectory(plugin)
endif()

if(BAKUON_BUILD_SANDBOX)
    add_subdirectory(sandbox)
endif()
