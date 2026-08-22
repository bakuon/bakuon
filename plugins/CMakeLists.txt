# ============================================================================
# plugins/ —— 运行时插件实例（MODULE/SHARED），按“宿主类型”分类到子目录，
# 与 source/{core,gui,plugin} 这种“编译期静态库”是完全不同的产物形态，
# 见 cmake/BakuonUtils.cmake 里 bakuon_add_plugin() 的说明。
#
# 目前只有 gui 分类（依赖 bakuon::gui 的插件）；core 分类的原生插件
# （不依赖 Qt，基于 source/plugin 基础设施）等有实际实现后再补
# add_subdirectory(core)。
# ============================================================================
add_subdirectory(gui)
