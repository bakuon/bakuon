# tests/CMakeLists.txt
cmake_minimum_required(VERSION 3.20 FATAL_ERROR)

project(
    bakuon_tests
    VERSION 1.0.0
    LANGUAGES CXX
)

# 引入各个子模块测试
add_subdirectory(core)
add_subdirectory(gui)
add_subdirectory(sandbox)
add_subdirectory(host)

# 为 IDE 组织项目结构
bakuon_apply_targets_folder(${CMAKE_CURRENT_LIST_DIR})
