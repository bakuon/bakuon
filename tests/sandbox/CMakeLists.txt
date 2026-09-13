if(NOT TARGET sandbox)
    return()
endif()

set(module_label "sandbox")

# 基础沙箱测试
bakuon_add_test(
    NAME
    test_sandbox_sharedmemorychannel
    USE_QT_CORE
    SOURCES
    "SharedMemoryChannelTest.cpp"
    DEPENDS
    bakuon::sandbox
    LABELS
    "${module_label};p1"
)

# 集成与高级沙箱测试（依赖运行时与插件）
if(TARGET sandbox_runtime AND TARGET sandboxed_example_plugin)

    # 辅助测试可执行文件
    add_executable(orphan_test_helper "${CMAKE_SOURCE_DIR}/tests/shared/OrphanTestHelper.cpp")
    target_link_libraries(orphan_test_helper PRIVATE bakuon::sandbox)
    target_compile_features(orphan_test_helper PRIVATE cxx_std_20)
    if(COMMAND bakuon_apply_warnings)
        bakuon_apply_warnings(orphan_test_helper)
    endif()

    # bakuon_add_test(
    #     NAME
    #     test_sandbox_integration
    #     USE_QT_CORE
    #     SOURCES
    #     "SandboxIntegrationTest.cpp"
    #     DEPENDS
    #     bakuon::sandbox
    #     LABELS
    #     "${module_label};integration;slow" # 打上 sandbox、集成、慢测试标签或flaky（易不稳定）
    # )

    # 待批量生成的集成测试列表
    set(SANDBOX_INT_TESTS
        "SandboxIntegrationTest.cpp"
        "TabSandboxManagerTest.cpp"
        "TabSandboxManagerOrphanTest.cpp"
        "TabSandboxManagerSessionTest.cpp"
        "GuiSurfaceCompositingTest.cpp"
    )

    foreach(test_file ${SANDBOX_INT_TESTS})
        string(REGEX REPLACE "Test\\.cpp$" "" base_name ${test_file})
        string(TOLOWER ${base_name} lower_name)
        set(target_name "test_sandbox_${lower_name}")

        bakuon_add_test(
            NAME
            ${target_name}
            USE_QT_CORE
            SOURCES
            ${test_file}
            DEPENDS
            bakuon::sandbox
            LABELS
            "${module_label}"
        )

        # 注入公共的沙箱运行时路径定义
        target_compile_definitions(
            ${target_name} PRIVATE BAKUON_TEST_SANDBOX_RUNTIME_PATH="$<TARGET_FILE:sandbox_runtime>"
                                   BAKUON_TEST_SANDBOXED_EXAMPLE_PLUGIN_PATH="$<TARGET_FILE:sandboxed_example_plugin>"
        )
        add_dependencies(${target_name} sandbox_runtime sandboxed_example_plugin)

        # 特殊处理依赖 orphan_helper 的用例
        if(lower_name MATCHES "orphan" OR lower_name MATCHES "session")
            target_compile_definitions(
                ${target_name} PRIVATE BAKUON_TEST_ORPHAN_HELPER_PATH="$<TARGET_FILE:orphan_test_helper>"
            )
            add_dependencies(${target_name} orphan_test_helper)
        endif()
    endforeach()
endif()
