if(BAKUON_BUILD_HOST
   AND TARGET sandbox_runtime
   AND TARGET sandboxed_example_plugin
)
    set(module_label "host")

    bakuon_add_test(
        NAME
        test_host_command_wiring
        USE_QT_GUI
        SOURCES
        "HostCommandWiringTest.cpp"
        "${CMAKE_SOURCE_DIR}/host/MainWindow.cpp"
        "${CMAKE_SOURCE_DIR}/host/TabContentWidget.cpp"
        DEPENDS
        bakuon::gui
        bakuon::sandbox
        LABELS
        "${module_label}"
    )

    target_compile_definitions(
        test_host_command_wiring
        PRIVATE BAKUON_TEST_SANDBOX_RUNTIME_PATH="$<TARGET_FILE:sandbox_runtime>"
                BAKUON_TEST_SANDBOXED_EXAMPLE_PLUGIN_PATH="$<TARGET_FILE:sandboxed_example_plugin>"
    )
    add_dependencies(test_host_command_wiring sandbox_runtime sandboxed_example_plugin)
endif()
