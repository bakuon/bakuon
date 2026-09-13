if(NOT TARGET core)
    return()
endif()

# 基础模块标签
set(module_label "core")

# P0 级测试（核心阻塞级）
# bakuon_add_test(
#     NAME test_core_hierarchy
#     SOURCES "HierarchyTest.cpp"
#     DEPENDS bakuon::core
#     LABELS "${module_label};p0" # 同时打上 core 和 p0 标签
# )

# P1 级测试（重要功能）
# bakuon_add_test(
#     NAME test_core_undostack
#     SOURCES "UndoStackTest.cpp"
#     DEPENDS bakuon::core
#     LABELS "${module_label};p1"
# )

# 其余普通测试可以通过循环统一打上 core 标签
set(CORE_TESTS
    "RegistryTest.cpp"
    "ResultStatusTest.cpp"
    "BimapTest.cpp"
    "IdentityTest.cpp"
    "HierarchyTest.cpp"
    "UndoStackTest.cpp"
    "RegistryBatchTest.cpp"
    "SerializerTest.cpp"
    "ComponentsTest.cpp"
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
