# ============================================================================
# 定义非聚合模块注册函数
# ============================================================================
function(bakuon_add_module)
    cmake_parse_arguments(
        ARG
        ""
        "NAME;PATH"
        "DEPENDS;INCLUDE_DIRS"
        ${ARGN})

    set(MODULE_NAME ${ARG_NAME})
    set(MODULE_PATH ${ARG_PATH})
    set(AMALGAM_H "${MODULE_PATH}/${MODULE_NAME}.h")
    set(AMALGAM_CPP "${MODULE_PATH}/${MODULE_NAME}.cpp")

    # 收集所有子文件
    file(GLOB_RECURSE ALL_HEADERS "${MODULE_PATH}/*.h")
    file(GLOB_RECURSE ALL_SOURCES "${MODULE_PATH}/*.cpp")

    # 从子列表中排除聚合入口本身
    list(REMOVE_ITEM ALL_SOURCES ${AMALGAM_CPP})
    list(REMOVE_ITEM ALL_HEADERS ${AMALGAM_H})

    add_library(${MODULE_NAME} STATIC)
    add_library(bakuon::${MODULE_NAME} ALIAS ${MODULE_NAME})

    target_sources(
        ${MODULE_NAME}
        PUBLIC ${AMALGAM_H}
        PRIVATE ${ALL_SOURCES} ${ALL_HEADERS})

    target_include_directories(
        ${MODULE_NAME}
        PUBLIC ${CMAKE_SOURCE_DIR}/source # CMAKE_CURRENT_SOURCE_DIR
        PRIVATE ${ARG_INCLUDE_DIRS})

    if(ARG_DEPENDS)
        target_link_libraries(${MODULE_NAME} PUBLIC ${ARG_DEPENDS})
    endif()

    message(STATUS "[Build] ${MODULE_NAME} (Non unity build, ${CMAKE_CURRENT_LIST_FILE})")

endfunction()

# ============================================================================
# 定义聚合模块注册函数
# ============================================================================
function(bakuon_add_amalgamated_module)
    cmake_parse_arguments(
        ARG
        ""
        "NAME;PATH"
        "DEPENDS;INCLUDE_DIRS"
        ${ARGN})

    set(MODULE_NAME ${ARG_NAME})
    set(MODULE_PATH ${ARG_PATH})
    set(AMALGAM_H "${MODULE_PATH}/${MODULE_NAME}.h")
    set(AMALGAM_CPP "${MODULE_PATH}/${MODULE_NAME}.cpp")

    # 收集所有子文件
    file(GLOB_RECURSE ALL_HEADERS "${MODULE_PATH}/*.h")
    file(GLOB_RECURSE ALL_SOURCES "${MODULE_PATH}/*.cpp")

    # 从子列表中排除聚合入口本身
    list(REMOVE_ITEM ALL_SOURCES ${AMALGAM_CPP})
    list(REMOVE_ITEM ALL_HEADERS ${AMALGAM_H})

    # ── 1. 真正的构建目标（Unity Build）────────────────
    add_library(${MODULE_NAME} STATIC)

    target_sources(
        ${MODULE_NAME} PRIVATE ${AMALGAM_CPP} # 唯一编译单元
                               ${ALL_HEADERS} # 头文件加入 IDE 项目树（不编译）
    )

    # 子 .cpp 加入项目树但不编译
    foreach(sub_src ${ALL_SOURCES})
        target_sources(${MODULE_NAME} PRIVATE ${sub_src})
        set_source_files_properties(${sub_src} PROPERTIES HEADER_FILE_ONLY TRUE)
    endforeach()

    target_include_directories(
        ${MODULE_NAME}
        PUBLIC ${CMAKE_SOURCE_DIR}/source # 外部用 <core/bakuon_core.h>
        PRIVATE ${ARG_INCLUDE_DIRS})

    if(ARG_DEPENDS)
        target_link_libraries(${MODULE_NAME} PUBLIC ${ARG_DEPENDS})
    endif()

    # ── 2. IDE 专用 OBJECT 目标（生成 compile_commands 条目）──
    #    EXCLUDE_FROM_ALL：不参与默认构建，但 CMake 仍会为其生成编译命令
    if(CMAKE_EXPORT_COMPILE_COMMANDS)
        set(IDE_TARGET "${MODULE_NAME}_ide")

        add_library(${IDE_TARGET} OBJECT EXCLUDE_FROM_ALL)

        target_sources(${IDE_TARGET} PRIVATE ${ALL_SOURCES})

        # 关键：通过 -include 强制注入聚合头，模拟 unity build 的上下文
        target_compile_options(${IDE_TARGET} PRIVATE "SHELL:-include ${AMALGAM_H}")

        # 复制与主目标相同的 include 路径
        target_include_directories(${IDE_TARGET} PRIVATE ${CMAKE_SOURCE_DIR}/source
                                                         ${ARG_INCLUDE_DIRS})

        if(ARG_DEPENDS)
            target_link_libraries(${IDE_TARGET} PRIVATE ${ARG_DEPENDS})
        endif()

        message(STATUS "[IDE]  ${IDE_TARGET} compile_commands entries generated")
    endif()

    message(STATUS "[Build] ${MODULE_NAME} (unity build, ${CMAKE_CURRENT_LIST_FILE})")
endfunction()
