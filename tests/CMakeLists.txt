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

# enable_testing() 已在根 CMakeLists.txt 内、add_subdirectory(tests) 之前调用，此处不再重复。

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
    test_actorsystem
    SOURCES
    "actorsystem_test.cpp")
