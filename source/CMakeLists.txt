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
option(BAKUON_BUILD_CORE "构建 bakuon::core（source/core 当前为空，实现代码就绪前请勿开启）" OFF)
option(BAKUON_BUILD_PLUGIN "构建 bakuon::plugin（原生插件基础设施静态库）" ON)
option(BAKUON_BUILD_SANDBOX "构建 bakuon::sandbox（进程外插件沙箱隔离，依赖 Qt RemoteObjects）" ON)

if(BAKUON_BUILD_CORE)
    add_subdirectory(core)
endif()

add_subdirectory(gui)

if(BAKUON_BUILD_PLUGIN)
    add_subdirectory(plugin)
endif()

if(BAKUON_BUILD_SANDBOX)
    add_subdirectory(sandbox)
endif()
