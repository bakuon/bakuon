cmake_minimum_required(VERSION 3.20 FATAL_ERROR)

# ============================================================================
# bakuon core project configure
# ============================================================================
project(
    tests
    VERSION 1.0.0
    LANGUAGES CXX)

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

    # 定位通用的 test_main.cpp 路径
    set(TEST_MAIN_CPP_PATH "${CMAKE_SOURCE_DIR}/tests/shared/test_main.cpp")

    # 创建可执行文件（使用自定义的 test_main 函数替代了 gtest_main）
    add_executable(${ARG_NAME} ${ARG_SOURCES} ${TEST_MAIN_CPP_PATH})

    # 根据不同的 Option 注入对应的宏和依赖
    # TODO: 自动追加编译测试所需的 Qt::Test 模块
    if(ARG_USE_QT_GUI)
        # 如果是 QWidget/GUI 测试
        set(CMAKE_AUTOMOC ON)
        set(CMAKE_AUTORCC ON)
        set(CMAKE_AUTOUIC ON)
        target_compile_definitions(${ARG_NAME} PRIVATE USE_QT_GUI_APP)
    elseif(ARG_USE_QT_CORE)
        # 如果是普通的 Qt Core 测试
        set(CMAKE_AUTOMOC ON)
        target_compile_definitions(${ARG_NAME} PRIVATE USE_QT_CORE_APP)
    endif()

    target_link_libraries(${ARG_NAME} PRIVATE gtest ${ARG_DEPENDS}) # 移除了 gtest_main
    target_compile_features(${ARG_NAME} PRIVATE cxx_std_20)

    bakuon_apply_warnings(${ARG_NAME})
    gtest_discover_tests(${ARG_NAME}) # required: include(GoogleTest)
endfunction(bakuon_add_test)

# TODO 根据每个目录模块和文件名使用循环自动批量增加测试

# ============================================================================
# core 模块测试
#
# bakuon::core 本身不依赖 Qt（这正是它存在的意义），但 tests/shared/qt_main.cpp
# 是全项目共用的测试入口，无条件 #include <QTimer>（见该文件），因此这里仍然需要
# 显式链接 Qt::Core——这是测试基础设施本身的选择，不代表 bakuon::core 库反过来
# 依赖了 Qt。
#
# 之前尝试直接引用 gui/sandbox 模块 find_package() 出来的 Qt6::Core 目标，
# 实测发现它在 tests/ 这个兄弟子目录里并不可见（这份 Qt6 打包没有把
# find_package() 产生的导入目标标记为跨目录可见的 GLOBAL），因此这里改为
# 自己独立 find_package() 一次——与 source/gui/CMakeLists.txt、
# source/sandbox/CMakeLists.txt 里的既有写法保持一致，QT_VERSION_MAJOR 只在
# 本目录作用域内使用，不依赖其它子目录是否已经求值过它。
# ============================================================================
if(TARGET core)
    bakuon_add_test(
        NAME
        test_core_registry
        SOURCES
        "core/registry_test.cpp"
        DEPENDS
        bakuon::core)
endif()
