cmake_minimum_required(VERSION 3.20 FATAL_ERROR)

# ============================================================================
# bakuon core project configure
# ============================================================================
project(
    tests
    VERSION 1.0.0
    LANGUAGES CXX)

set(CMAKE_AUTOMOC ON)

function(bakuon_add_test)
    cmake_parse_arguments(
        ARG
        "USE_QT_GUI;USE_QT_CORE" # Options (例如: 单选开关)
        "NAME" # One value keywords (只能传单个值的关键字)
        "SOURCES;DEPENDS" # Multi value keywords (可以传多个值的关键字)
        ${ARGN})

    if(NOT ARG_NAME)
        message(FATAL_ERROR "bakuon_add_test: NAME parameter is required")
    endif()

    if(NOT ARG_SOURCES)
        message(FATAL_ERROR "bakuon_add_test: SOURCES parameter is required")
    endif()

    # 定位通用的 qt_main.cpp 路径
    set(QT_MAIN_CPP_PATH "${CMAKE_SOURCE_DIR}/tests/shared/qt_main.cpp")

    # 创建可执行文件（使用自定义的 main 函数替代了 gtest_main）
    add_executable(${ARG_NAME} ${ARG_SOURCES} ${QT_MAIN_CPP_PATH})

    # 根据不同的 Option 注入对应的宏和依赖
    # TODO: 自动追加编译测试所需的 Qt::Test 模块
    if(ARG_USE_QT_GUI)
        # 如果是 QWidget/GUI 测试
        target_compile_definitions(${ARG_NAME} PRIVATE USE_QT_GUI_APP)
    elseif(ARG_USE_QT_CORE)
        # 如果是普通的 Qt Core 测试
        target_compile_definitions(${ARG_NAME} PRIVATE USE_QT_CORE_APP)
    endif()

    target_link_libraries(${ARG_NAME} PRIVATE gtest ${ARG_DEPENDS}) # 移除了 gtest_main
    target_compile_features(${ARG_NAME} PRIVATE cxx_std_20)

    bakuon_apply_warnings(${ARG_NAME})
    gtest_discover_tests(${ARG_NAME}) # required: include(GoogleTest)
endfunction(bakuon_add_test)
