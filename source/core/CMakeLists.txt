cmake_minimum_required(VERSION 3.20 FATAL_ERROR)

# ============================================================================
# bakuon::core —— 纯标准 C++ 静态库，不依赖 Qt，可被 gui / plugin 复用。
# 目前目录下还没有实现文件，本文件仅作为脚手架保留；
# 顶层通过 BAKUON_BUILD_CORE 选项控制是否 add_subdirectory 到此处，默认关闭。
# ============================================================================
project(
    core
    VERSION 1.0.0
    LANGUAGES CXX)

bakuon_add_module(
    NAME
    ${PROJECT_NAME}
    PATH
    ${CMAKE_CURRENT_SOURCE_DIR}
    INCLUDE_DIRS
    ${BAKUON_ROOT_DIR}/include)

# 尚无 include/bakuon/core/ 门面头文件，因此暂不传 PUBLIC_INCLUDE_DIRS；
# 等 core 有了对外的公共 API 之后，仿照 source/gui/CMakeLists.txt 的写法，
# 新建 include/bakuon/core/ 目录并传入 PUBLIC_INCLUDE_DIRS ${BAKUON_ROOT_DIR}/include。
