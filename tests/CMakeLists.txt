cmake_minimum_required(VERSION 3.20 FATAL_ERROR)

# ============================================================================
# bakuon core project configure
# ============================================================================
project(
    bakuon_tests
    VERSION 1.0.0
    LANGUAGES CXX)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

function(bakuon_add_test)
    cmake_parse_arguments(
        ARG
        ""
        "NAME;SOURCES"
        "DEPENDS"
        ${ARGN})

    add_executable(${ARG_NAME} ${ARG_SOURCES})
    target_link_libraries(${ARG_NAME} PRIVATE gtest gtest_main ${ARG_DEPENDS})
    target_compile_features(${ARG_NAME} PRIVATE cxx_std_20)
    bakuon_apply_warnings(${ARG_NAME})
    gtest_discover_tests(${ARG_NAME}) # required: include(GoogleTest)
endfunction(bakuon_add_test)

# TODO 根据每个目录模块和文件名使用循环自动批量增加测试

bakuon_add_test(
    NAME
    test_gui_treenode
    SOURCES
    "gui/treenode_test.cpp"
    DEPENDS
    bakuon::gui)

bakuon_add_test(
    NAME
    test_gui_context
    SOURCES
    "gui/context_test.cpp"
    DEPENDS
    bakuon::gui)

bakuon_add_test(
    NAME
    test_gui_commandsystem
    SOURCES
    "gui/commandsystem_test.cpp"
    DEPENDS
    bakuon::gui)

bakuon_add_test(
    NAME
    test_gui_pluginpipeline
    SOURCES
    "gui/pluginpipeline_test.cpp"
    DEPENDS
    bakuon::gui)

bakuon_add_test(
    NAME
    test_gui_pluginsystem
    SOURCES
    "gui/pluginsystem_test.cpp"
    DEPENDS
    bakuon::gui)

# example_plugin 只在 BAKUON_BUILD_PLUGINS 打开时才存在；测试用它验证 registerFile()/launch() 走
# 真实动态库路径（而不是只测内置插件），target 不存在时对应的测试用例会在编译期被 #ifdef 跳过。
if(TARGET example_plugin)
    foreach(t test_gui_pluginpipeline test_gui_pluginsystem)
        target_compile_definitions(
            ${t} PRIVATE BAKUON_TEST_EXAMPLE_PLUGIN_PATH="$<TARGET_FILE:example_plugin>")
        add_dependencies(${t} example_plugin)
    endforeach()
endif()
