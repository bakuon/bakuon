if(NOT TARGET core)
    return()
endif()

# 基础模块标签
set(module_label "core")

# 其余普通测试可以通过循环统一打上 core 标签
set(CORE_TESTS
    "ContainerTest.cpp"
    "ResultStatusTest.cpp"
    "BimapTest.cpp"
    "IdentifierTest.cpp"
    "HierarchyTest.cpp"
    "UndoStackTest.cpp"
    "ContainerBatchTest.cpp"
    "SerializerTest.cpp"
    "ComponentsTest.cpp"
    "SelectionTest.cpp"
    "ClonerTest.cpp"
    "WorkspaceTest.cpp"
    "FlagsTest.cpp"
)

foreach(test_file ${CORE_TESTS})
    # 从文件名提取 TestName（例如：RegistryTest -> test_core_registry）
    string(REGEX REPLACE "Test\\.cpp$" "" base_name ${test_file})
    string(TOLOWER ${base_name} lower_name)
    set(target_name "test_core_${lower_name}")

    bakuon_add_test(
        NAME
        ${target_name}
        SOURCES
        ${test_file}
        DEPENDS
        bakuon::core
        LABELS
        "${module_label}"
    )
endforeach()
