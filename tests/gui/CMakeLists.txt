if(NOT TARGET gui)
    return()
endif()

set(module_label "gui")

# 定义测试配置列表：文件名;是否是GUI测试(ON/OFF)
# 采用 大驼峰命名法（PascalCase），并以 Test.cpp 结尾
set(GUI_TEST_CONFIGS
    "TreeNodeTest.cpp\;OFF"
    "ContextTest.cpp\;ON"
    "CommandSystemTest.cpp\;ON"
    "PluginPipelineTest.cpp\;OFF"
    "PluginSystemTest.cpp\;OFF"
    "ExtensionPointTest.cpp\;OFF"
    "RegistryBridgeTest.cpp\;OFF"
)

foreach(config ${GUI_TEST_CONFIGS})
    list(GET config 0 test_file)
    list(GET config 1 is_gui)

    string(REGEX REPLACE "Test\\.cpp$" "" base_name ${test_file})
    string(TOLOWER ${base_name} lower_name)
    set(target_name "test_gui_${lower_name}")

    if(is_gui)
        set(qt_arg "USE_QT_GUI")
    else()
        set(qt_arg "USE_QT_CORE")
    endif()

    bakuon_add_test(
        NAME
        ${target_name}
        ${qt_arg}
        SOURCES
        ${test_file}
        DEPENDS
        bakuon::gui
        LABELS
        "${module_label}"
    )

    # 动态链接插件依赖
    if(TARGET example_plugin AND (lower_name STREQUAL "pluginpipeline" OR lower_name STREQUAL "pluginsystem"))
        target_compile_definitions(
            ${target_name} PRIVATE BAKUON_TEST_EXAMPLE_PLUGIN_PATH="$<TARGET_FILE:example_plugin>"
        )
        add_dependencies(${target_name} example_plugin)
    endif()
endforeach()
