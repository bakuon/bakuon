cmake_minimum_required(VERSION 3.20 FATAL_ERROR)

# ============================================================================
# bakuon::plugin —— 标准 C++ 原生插件基础设施静态库（不依赖 GUI / Qt）。
# 注：目标名由 bakuon_plugin 改为 plugin，与 core / gui 保持一致的命名习惯，
# 对外别名相应变为 bakuon::plugin（原 bakuon::bakuon_plugin 未被其他 CMakeLists 引用，重命名安全）。
# ============================================================================
project(
    plugin
    VERSION 1.0.0
    LANGUAGES CXX)

bakuon_add_module(
    NAME
    ${PROJECT_NAME}
    PATH
    ${CMAKE_CURRENT_SOURCE_DIR}
    INCLUDE_DIRS
    ${BAKUON_ROOT_DIR}/include)

# 尚无 include/bakuon/plugin/ 门面头文件，因此暂不传 PUBLIC_INCLUDE_DIRS；
# 等 plugin 有了对外的公共 API 之后，仿照 source/gui/CMakeLists.txt 的写法，
# 新建 include/bakuon/plugin/ 目录并传入 PUBLIC_INCLUDE_DIRS ${BAKUON_ROOT_DIR}/include。

# 位置无关代码已在根 CMakeLists.txt 通过 CMAKE_POSITION_INDEPENDENT_CODE 全局开启，
# 无需在此单独设置 —— 该静态库最终会被链接进以共享库形式加载的运行时插件模块。
