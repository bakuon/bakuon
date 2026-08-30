# ============================================================================
# plugins/ —— 运行时插件实例（MODULE/SHARED），按“宿主类型”分类到子目录，
# 与 source/{core,gui,plugin} 这种“编译期静态库”是完全不同的产物形态，
# 见 cmake/BakuonUtils.cmake 里 bakuon_add_plugin() 的说明。
#
# 目前有 gui（依赖 bakuon::gui 的插件）和 sandbox（沙箱子进程里加载、用于演示/验证
# 沙箱基础流程的插件，见 plugins/sandbox/CMakeLists.txt 里的 BAKUON_BUILD_SANDBOX 开关）
# 两个分类；core 分类的原生插件（不依赖 Qt，基于 source/plugin 基础设施）等有实际实现
# 后再补 add_subdirectory(core)。
#
# 修正：plugins/sandbox/CMakeLists.txt 此前一直存在，但没有任何地方对它调用
# add_subdirectory()，导致 sandboxed_example_plugin 无论 BAKUON_BUILD_SANDBOX 开没开
# 都不会被构建——这里补上。
# ============================================================================
add_subdirectory(gui)
add_subdirectory(sandbox)
