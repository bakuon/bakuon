cmake_minimum_required(VERSION 3.20 FATAL_ERROR)

# ============================================================================
# bakuon core project configure
# ============================================================================
project(
    bakuon_tests
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

    if(ARG_USE_QT_GUI)
        set(CMAKE_AUTOMOC ON)
        set(CMAKE_AUTORCC ON)
        set(CMAKE_AUTOUIC ON)
    elseif(ARG_USE_QT_CORE)
        set(CMAKE_AUTOMOC ON)
    endif()

    # 定位通用的 test_main.cpp 路径
    set(TEST_MAIN_CPP_PATH "${CMAKE_SOURCE_DIR}/tests/shared/test_main.cpp")

    # 创建可执行文件（使用自定义的 main 函数替代了 gtest_main）
    add_executable(${ARG_NAME} ${ARG_SOURCES} ${TEST_MAIN_CPP_PATH})

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

# TODO 根据每个目录模块和文件名使用循环自动批量增加测试

bakuon_add_test(
    NAME
    test_gui_treenode
    USE_QT_CORE
    SOURCES
    "gui/treenode_test.cpp"
    DEPENDS
    bakuon::gui)

bakuon_add_test(
    NAME
    test_gui_context
    USE_QT_GUI
    SOURCES
    "gui/context_test.cpp"
    DEPENDS
    bakuon::gui)

bakuon_add_test(
    NAME
    test_gui_commandsystem
    USE_QT_GUI
    SOURCES
    "gui/commandsystem_test.cpp"
    DEPENDS
    bakuon::gui)

bakuon_add_test(
    NAME
    test_gui_pluginpipeline
    USE_QT_CORE
    SOURCES
    "gui/pluginpipeline_test.cpp"
    DEPENDS
    bakuon::gui)

bakuon_add_test(
    NAME
    test_gui_pluginsystem
    USE_QT_CORE
    SOURCES
    "gui/pluginsystem_test.cpp"
    DEPENDS
    bakuon::gui)

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

# example_plugin 只在 BAKUON_BUILD_PLUGINS 打开时才存在；测试用它验证 registerFile()/launch() 走
# 真实动态库路径（而不是只测内置插件），target 不存在时对应的测试用例会在编译期被 #ifdef 跳过。
if(TARGET example_plugin)
    foreach(t test_gui_pluginpipeline test_gui_pluginsystem)
        target_compile_definitions(
            ${t} PRIVATE BAKUON_TEST_EXAMPLE_PLUGIN_PATH="$<TARGET_FILE:example_plugin>")
        add_dependencies(${t} example_plugin)
    endforeach()
endif()

bakuon_add_test(
    NAME
    test_gui_extensionpoint
    USE_QT_CORE
    SOURCES
    "gui/extensionpoint_test.cpp"
    DEPENDS
    bakuon::gui)

bakuon_add_test(
    NAME
    test_gui_result_status
    SOURCES
    "gui/result_status_test.cpp"
    DEPENDS
    bakuon::gui)

# ============================================================================
# sandbox 模块测试
# ============================================================================
if(TARGET sandbox)
    bakuon_add_test(
        NAME
        test_sandbox_sharedmemorychannel
        USE_QT_CORE
        SOURCES
        "sandbox/sharedmemorychannel_test.cpp"
        DEPENDS
        bakuon::sandbox)

    # 端到端集成测试需要真实的 sandbox_runtime 可执行文件 + sandboxed_example_plugin
    # 动态库都被构建出来才有意义（分别对应 BAKUON_BUILD_SANDBOX_RUNTIME 和
    # BAKUON_BUILD_PLUGINS）；任一条件不满足时这个 target 就不存在，整体跳过，
    # 不影响其余测试正常跑。
    if(TARGET sandbox_runtime AND TARGET sandboxed_example_plugin)
        bakuon_add_test(
            NAME
            test_sandbox_integration
            USE_QT_CORE
            SOURCES
            "sandbox/sandbox_integration_test.cpp"
            DEPENDS
            bakuon::sandbox)
        target_compile_definitions(
            test_sandbox_integration
            PRIVATE
                BAKUON_TEST_SANDBOX_RUNTIME_PATH="$<TARGET_FILE:sandbox_runtime>"
                BAKUON_TEST_SANDBOXED_EXAMPLE_PLUGIN_PATH="$<TARGET_FILE:sandboxed_example_plugin>")
        add_dependencies(test_sandbox_integration sandbox_runtime sandboxed_example_plugin)

        bakuon_add_test(
            NAME
            test_sandbox_tabsandboxmanager
            USE_QT_CORE
            SOURCES
            "sandbox/tabsandboxmanager_test.cpp"
            DEPENDS
            bakuon::sandbox)
        target_compile_definitions(
            test_sandbox_tabsandboxmanager
            PRIVATE
                BAKUON_TEST_SANDBOX_RUNTIME_PATH="$<TARGET_FILE:sandbox_runtime>"
                BAKUON_TEST_SANDBOXED_EXAMPLE_PLUGIN_PATH="$<TARGET_FILE:sandboxed_example_plugin>")
        add_dependencies(test_sandbox_tabsandboxmanager sandbox_runtime sandboxed_example_plugin)

        # 孤儿收编测试需要一个真正独立的"第一代 Host"进程可以被 SIGKILL——见
        # orphan_test_helper.cpp 顶部注释。这是纯测试辅助可执行文件，不是产品代码，
        # 因此不用 bakuon_add_test()（那是给 gtest 用例用的），普通 add_executable 即可。
        add_executable(orphan_test_helper "shared/orphan_test_helper.cpp")
        target_link_libraries(orphan_test_helper PRIVATE bakuon::sandbox)
        target_compile_features(orphan_test_helper PRIVATE cxx_std_20)
        bakuon_apply_warnings(orphan_test_helper)

        bakuon_add_test(
            NAME
            test_sandbox_tabsandboxmanager_orphan
            USE_QT_CORE
            SOURCES
            "sandbox/tabsandboxmanager_orphan_test.cpp"
            DEPENDS
            bakuon::sandbox)
        target_compile_definitions(
            test_sandbox_tabsandboxmanager_orphan
            PRIVATE
                BAKUON_TEST_SANDBOX_RUNTIME_PATH="$<TARGET_FILE:sandbox_runtime>"
                BAKUON_TEST_SANDBOXED_EXAMPLE_PLUGIN_PATH="$<TARGET_FILE:sandboxed_example_plugin>"
                BAKUON_TEST_ORPHAN_HELPER_PATH="$<TARGET_FILE:orphan_test_helper>")
        add_dependencies(
            test_sandbox_tabsandboxmanager_orphan
            sandbox_runtime
            sandboxed_example_plugin
            orphan_test_helper)

        # 会话持久化（原地恢复成同一个 Tab）测试：复用同一个 orphan_test_helper，
        # 只是这次多传一个 sessionFilePath 参数（见 orphan_test_helper.cpp）。
        bakuon_add_test(
            NAME
            test_sandbox_tabsandboxmanager_session
            USE_QT_CORE
            SOURCES
            "sandbox/tabsandboxmanager_session_test.cpp"
            DEPENDS
            bakuon::sandbox)
        target_compile_definitions(
            test_sandbox_tabsandboxmanager_session
            PRIVATE
                BAKUON_TEST_SANDBOX_RUNTIME_PATH="$<TARGET_FILE:sandbox_runtime>"
                BAKUON_TEST_SANDBOXED_EXAMPLE_PLUGIN_PATH="$<TARGET_FILE:sandboxed_example_plugin>"
                BAKUON_TEST_ORPHAN_HELPER_PATH="$<TARGET_FILE:orphan_test_helper>")
        add_dependencies(
            test_sandbox_tabsandboxmanager_session
            sandbox_runtime
            sandboxed_example_plugin
            orphan_test_helper)

        # 跨进程 GUI 合成端到端验证：真实起 sandbox_runtime + sandboxed_example_plugin，
        # 验证渲染（frameReady）和输入（dispatchInputEvent）两个方向都真正打通。
        # USE_QT_CORE 而不是 USE_QT_GUI：TabSandboxManager 本身不碰 QWidget，
        # 只需要一个跑事件循环的 QCoreApplication。
        bakuon_add_test(
            NAME
            test_sandbox_gui_surface_compositing
            USE_QT_CORE
            SOURCES
            "sandbox/gui_surface_compositing_test.cpp"
            DEPENDS
            bakuon::sandbox)
        target_compile_definitions(
            test_sandbox_gui_surface_compositing
            PRIVATE
                BAKUON_TEST_SANDBOX_RUNTIME_PATH="$<TARGET_FILE:sandbox_runtime>"
                BAKUON_TEST_SANDBOXED_EXAMPLE_PLUGIN_PATH="$<TARGET_FILE:sandboxed_example_plugin>")
        add_dependencies(test_sandbox_gui_surface_compositing sandbox_runtime
                         sandboxed_example_plugin)
    endif()
endif()

# ============================================================================
# host/ 的回归测试：直接把 host/ 下的 .cpp 编译进测试可执行文件（host/ 本身是
# 一个 add_executable，不是库，没有 bakuon:: 目标可以链接），需要
# BAKUON_BUILD_HOST 打开且沙箱子系统齐备。
# ============================================================================
if(BAKUON_BUILD_HOST
   AND TARGET sandbox_runtime
   AND TARGET sandboxed_example_plugin)
    find_package(
        QT
        NAMES
        Qt6
        Qt5
        COMPONENTS Core Gui Widgets
        REQUIRED)
    find_package(
        Qt${QT_VERSION_MAJOR}
        COMPONENTS Core Gui Widgets
        REQUIRED)

    bakuon_add_test(
        NAME
        test_host_command_wiring
        USE_QT_GUI
        SOURCES
        "host/host_command_wiring_test.cpp"
        "${CMAKE_SOURCE_DIR}/host/MainWindow.cpp"
        "${CMAKE_SOURCE_DIR}/host/TabContentWidget.cpp"
        DEPENDS
        Qt${QT_VERSION_MAJOR}::Core
        Qt${QT_VERSION_MAJOR}::Gui
        Qt${QT_VERSION_MAJOR}::Widgets
        bakuon::gui
        bakuon::sandbox)

    target_compile_definitions(
        test_host_command_wiring
        PRIVATE BAKUON_TEST_SANDBOX_RUNTIME_PATH="$<TARGET_FILE:sandbox_runtime>"
                BAKUON_TEST_SANDBOXED_EXAMPLE_PLUGIN_PATH="$<TARGET_FILE:sandboxed_example_plugin>")
    add_dependencies(test_host_command_wiring sandbox_runtime sandboxed_example_plugin)
endif()
