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

bakuon_add_test(
    NAME
    test_gui_extensionpoint
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
        add_executable(orphan_test_helper "sandbox/orphan_test_helper.cpp")
        target_link_libraries(orphan_test_helper PRIVATE bakuon::sandbox)
        target_compile_features(orphan_test_helper PRIVATE cxx_std_20)
        bakuon_apply_warnings(orphan_test_helper)

        bakuon_add_test(
            NAME
            test_sandbox_tabsandboxmanager_orphan
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
    endif()
endif()
