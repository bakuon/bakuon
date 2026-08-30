# ============================================================================
# 内部辅助：收集模块目录下的全部 .h / .cpp（不做任何排除）。
# CONFIGURE_DEPENDS：新增/删除源文件时自动触发 cmake 重新配置，
# 早期孵化阶段文件变动频繁，这个便利性优先于极少数场景下的重新配置开销。
# ============================================================================
function(
    _bakuon_glob_module_files
    MODULE_PATH
    OUT_HEADERS
    OUT_SOURCES)
    file(
        GLOB_RECURSE
        HEADERS
        CONFIGURE_DEPENDS
        "${MODULE_PATH}/*.h")
    file(
        GLOB_RECURSE
        SOURCES
        CONFIGURE_DEPENDS
        "${MODULE_PATH}/*.cpp")

    set(${OUT_HEADERS}
        "${HEADERS}"
        PARENT_SCOPE)
    set(${OUT_SOURCES}
        "${SOURCES}"
        PARENT_SCOPE)
endfunction()

# ============================================================================
# 内部辅助：目标的公共基础设置（C++ 标准 / 警告开关）
# 统一入口，避免 bakuon_add_module 与 bakuon_add_amalgamated_module 各写一份、逐渐漂移。
# ============================================================================
function(_bakuon_apply_common_target_settings TARGET_NAME)
    target_compile_features(${TARGET_NAME} PUBLIC cxx_std_20)
    set_target_properties(${TARGET_NAME} PROPERTIES CXX_EXTENSIONS OFF)
    bakuon_apply_warnings(${TARGET_NAME})
endfunction()

# ============================================================================
# 公共函数：为目标启用统一的编译警告策略
# 独立导出，方便 standalone / examples / tests 下的可执行目标也复用同一套警告策略。
# ============================================================================
function(bakuon_apply_warnings TARGET_NAME)
    if(NOT BAKUON_ENABLE_WARNINGS)
        return()
    endif()

    if(MSVC)
        target_compile_options(${TARGET_NAME} PRIVATE /W4
                                                      $<$<BOOL:${BAKUON_WARNINGS_AS_ERRORS}>:/WX>)
    else()
        target_compile_options(
            ${TARGET_NAME}
            PRIVATE -Wall
                    -Wextra
                    -Wpedantic
                    $<$<BOOL:${BAKUON_WARNINGS_AS_ERRORS}>:-Werror>)
    endif()
endfunction()

# ============================================================================
# 公共函数：按需为一个库目标安装头文件 + 导出 target（装进 SDK / devkit 包）
#
# 默认关闭（BAKUON_INSTALL_SDK=OFF）：项目目前还在孵化期，没有正式的发布/打包流程，
# 贸然打开 install() 只会带来一堆没人验证过的坑。这里先把“将来要不要装、装哪些东西”的
# 骨架搭好、语义正确（BUILD_INTERFACE/INSTALL_INTERFACE 已经在 target_include_directories
# 里配好了），真正决定打包方案时只需要把 BAKUON_INSTALL_SDK 打开，
# 并按需在根目录补一份 install(EXPORT ...) + Config.cmake.in（见下方 TODO）。
#
# TODO（不确定，需要产品化打包方案确定后再完善）：
#  - 是否需要生成 bakuonConfig.cmake / bakuonConfigVersion.cmake 供
#    find_package(bakuon) 使用？如果需要，要另外写一份 cmake/bakuonConfig.cmake.in
#    并在根 CMakeLists.txt 里调 configure_package_config_file() + write_basic_package_version_file()。
#  - core/gui/plugin 是否要合并成一个 export set 一起装，还是分开装？
#  - 版本兼容策略（SameMajorVersion？ExactVersion？）目前完全没有讨论过。
# ============================================================================
function(bakuon_install_module TARGET_NAME PUBLIC_HEADER_DIR)
    if(NOT BAKUON_INSTALL_SDK)
        return()
    endif()

    install(
        TARGETS ${TARGET_NAME}
        EXPORT bakuonTargets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

    if(PUBLIC_HEADER_DIR AND EXISTS "${PUBLIC_HEADER_DIR}")
        install(DIRECTORY "${PUBLIC_HEADER_DIR}/" DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
    endif()
endfunction()

# ============================================================================
# 定义非聚合模块注册函数
# 每个 .cpp 独立编译单元，适合日常开发（增量编译快、可读性好）。
# ============================================================================
function(bakuon_add_module)
    cmake_parse_arguments(
        ARG
        "SHARED"
        "NAME;PATH"
        "DEPENDS;INCLUDE_DIRS;PUBLIC_INCLUDE_DIRS"
        ${ARGN})

    set(MODULE_NAME ${ARG_NAME})
    set(MODULE_PATH ${ARG_PATH})
    set(AMALGAM_H "${MODULE_PATH}/${MODULE_NAME}.h")

    _bakuon_glob_module_files("${MODULE_PATH}" ALL_HEADERS ALL_SOURCES)

    # “聚合入口头”（MODULE_NAME.h 作为整个模块唯一的公共门面）是可选约定，不是强制要求：
    # 只有当它真实存在时才作为 PUBLIC 源加入、并从 PRIVATE 头文件列表中去重；模块内其余头文件
    # 一律按 "gui/b_xxx.h" 这种模块前缀路径被直接 #include，不需要也不应该被强制归纳进一个总头文件。
    #
    # 注意：这里不排除 MODULE_NAME.cpp —— 每个 .cpp 独立编译单元是本函数的核心语义，
    # 一个模块下允许存在恰好与模块同名的普通实现文件（例如 plugin/plugin.cpp），它必须被正常编译，
    # “同名 .cpp 即聚合入口”的约定只属于 bakuon_add_amalgamated_module。
    if(EXISTS "${AMALGAM_H}")
        set(PUBLIC_AMALGAM_HEADER "${AMALGAM_H}")
        list(REMOVE_ITEM ALL_HEADERS "${AMALGAM_H}")
    else()
        set(PUBLIC_AMALGAM_HEADER "")
    endif()

    if(NOT ALL_SOURCES AND NOT PUBLIC_AMALGAM_HEADER)
        message(FATAL_ERROR "bakuon_add_module(${MODULE_NAME}): 在 ${MODULE_PATH} 下没有找到任何 .cpp 文件，"
                            "无法创建库；该模块尚无实现代码前不要 add_subdirectory() 它。")
    endif()

    # SHARED（可选开关）：默认仍产出 STATIC 库，与现状保持兼容；传入 SHARED 后改为产出
    # 动态库（.dll/.so/.dylib）——目前 bakuon::gui 用它，是为了让插件 / 沙箱进程 / Host
    # 应用程序都能共享同一份 ExtensionSystem::instance() 之类的进程内单例（STATIC 库分别
    # 静态链接进多个二进制时，每个二进制各自持有一份独立的单例，见
    # include/bakuon/gui/PluginContext.h 顶部关于这一点的详细说明）。
    set(_bakuon_lib_type STATIC)
    if(ARG_SHARED)
        set(_bakuon_lib_type SHARED)
    endif()

    add_library(${MODULE_NAME} ${_bakuon_lib_type})
    add_library(bakuon::${MODULE_NAME} ALIAS ${MODULE_NAME})

    target_sources(${MODULE_NAME} PRIVATE ${ALL_SOURCES} ${ALL_HEADERS})
    if(PUBLIC_AMALGAM_HEADER)
        target_sources(${MODULE_NAME} PUBLIC ${PUBLIC_AMALGAM_HEADER})
    endif()

    # PUBLIC 路径统一以仓库 source/ 为根，保持 "gui/xxx.h" 这种模块前缀式 include 惯例；
    # 使用 BAKUON_ROOT_DIR 而非 CMAKE_SOURCE_DIR，确保被上层工程 add_subdirectory() 集成时依然正确。
    target_include_directories(
        ${MODULE_NAME}
        PUBLIC $<BUILD_INTERFACE:${BAKUON_ROOT_DIR}/source>
        PRIVATE ${ARG_INCLUDE_DIRS})

    # PUBLIC_INCLUDE_DIRS：面向“第三方插件开发者”的稳定门面 API（include/bakuon/...），
    # 与上面 source/ 的内部实现路径分开传入是刻意的——
    # source/ 暴露的是内部实现（b_xxx.h 命名，随时可能重构），
    # PUBLIC_INCLUDE_DIRS 暴露的是精心维护、尽量不破坏兼容性的转发门面（见 include/bakuon/gui/*.h）。
    # 二者当前都以 PUBLIC 方式传递给消费者（这意味着插件工程 link 了 bakuon::gui 后两条路径都能拿到，
    # 也就还没有做到“插件只能看到门面、看不到内部实现”的强隔离——真要做到这点，
    # 需要把 source/ 的 include 拆到单独的 PRIVATE/INTERFACE 目标里，目前先不做这个更大的改动，
    # 留给以后插件生态成型、确实需要收紧可见性时再处理）。
    if(ARG_PUBLIC_INCLUDE_DIRS)
        target_include_directories(
            ${MODULE_NAME} PUBLIC $<BUILD_INTERFACE:${ARG_PUBLIC_INCLUDE_DIRS}>
                                  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
    endif()

    if(ARG_DEPENDS)
        target_link_libraries(${MODULE_NAME} PUBLIC ${ARG_DEPENDS})
    endif()

    _bakuon_apply_common_target_settings(${MODULE_NAME})

    # ------------------------------------------------------------------------
    # SHARED 专属收尾：生成导出宏 + 收紧默认符号可见性。
    #
    # 1. generate_export_header() 生成 BAKUON_<MODULE>_EXPORT，落在
    #    "<module>/b_<module>_export.h"，与仓库既有的 "gui/b_xxx.h" 模块前缀 include
    #    惯例保持一致，跨动态库边界访问的类/自由函数需要在声明处加上这个宏——
    #    MSVC 上没有 __declspec(dllexport/dllimport) 就是链接错误，这一步不是可选项。
    # 2. CXX_VISIBILITY_PRESET hidden + VISIBILITY_INLINES_HIDDEN：GCC/Clang 下默认
    #    所有符号都可见，只有 Windows 才"必须显式导出"，如果不主动收紧 GCC/Clang 的
    #    默认可见性，很容易出现"本地 Linux 编译一直正常，到 Windows CI 才发现漏标导出宏"
    #    这种平台特有的返工。提前打开 hidden，让三个工具链用同一套导出宏描述来验证。
    # ------------------------------------------------------------------------
    if(ARG_SHARED)
        string(TOUPPER "${MODULE_NAME}" _bakuon_module_upper)
        set(_bakuon_export_dir "${CMAKE_CURRENT_BINARY_DIR}/generated_include")
        set(_bakuon_export_header "${_bakuon_export_dir}/${MODULE_NAME}/b_${MODULE_NAME}_export.h")

        generate_export_header(
            ${MODULE_NAME}
            BASE_NAME
            ${MODULE_NAME}
            EXPORT_MACRO_NAME
            BAKUON_${_bakuon_module_upper}_EXPORT
            EXPORT_FILE_NAME
            ${_bakuon_export_header}
            DEPRECATED_MACRO_NAME
            BAKUON_${_bakuon_module_upper}_DEPRECATED)

        # BUILD_INTERFACE 即可：生成头只在构建这份源码时用得到，装 SDK 包的场景等
        # BAKUON_INSTALL_SDK 真正打开时再一并处理导出头的安装路径（见 bakuon_install_module()）。
        target_include_directories(${MODULE_NAME} PUBLIC $<BUILD_INTERFACE:${_bakuon_export_dir}>)

        set_target_properties(
            ${MODULE_NAME}
            PROPERTIES CXX_VISIBILITY_PRESET hidden
                       VISIBILITY_INLINES_HIDDEN ON
                       # SOVERSION 用于 Linux/macOS 的 SONAME；早期孵化阶段版本号还会频繁
                       # 变动，先固定在与模块 VERSION 的主版本一致即可。
                       SOVERSION ${PROJECT_VERSION_MAJOR})
    endif()

    bakuon_install_module(${MODULE_NAME} "${ARG_PUBLIC_INCLUDE_DIRS}")

    message(
        STATUS
            "[Build] ${MODULE_NAME} (${_bakuon_lib_type}, Non unity build, ${CMAKE_CURRENT_LIST_FILE})"
    )
endfunction()

# ============================================================================
# 定义聚合模块注册函数（Unity Build）
# 单一编译单元换取更快的全量构建速度；同时生成一个 EXCLUDE_FROM_ALL 的
# OBJECT IDE 目标，只为让各子 .cpp 在 compile_commands.json / clangd 中有独立的编译条目，
# 不参与实际链接产物。
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

    _bakuon_glob_module_files("${MODULE_PATH}" ALL_HEADERS ALL_SOURCES)

    # Unity Build 下 MODULE_NAME.h / MODULE_NAME.cpp 是硬性约定的聚合入口，必须存在，
    # 因此从常规列表中剔除，分别按“唯一公共头”和“唯一编译单元”特殊处理。
    list(REMOVE_ITEM ALL_SOURCES "${AMALGAM_CPP}")
    list(REMOVE_ITEM ALL_HEADERS "${AMALGAM_H}")

    # ── 1. 真正的构建目标（Unity Build）────────────────
    add_library(${MODULE_NAME} STATIC)
    add_library(bakuon::${MODULE_NAME} ALIAS ${MODULE_NAME})

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
        PUBLIC $<BUILD_INTERFACE:${BAKUON_ROOT_DIR}/source> # 外部用 <core/${MODULE_NAME}.h>
        PRIVATE ${ARG_INCLUDE_DIRS})

    if(ARG_DEPENDS)
        target_link_libraries(${MODULE_NAME} PUBLIC ${ARG_DEPENDS})
    endif()

    _bakuon_apply_common_target_settings(${MODULE_NAME})

    # ── 2. IDE 专用 OBJECT 目标（生成 compile_commands 条目）──
    #    EXCLUDE_FROM_ALL：不参与默认构建，但 CMake 仍会为其生成编译命令
    if(CMAKE_EXPORT_COMPILE_COMMANDS)
        set(IDE_TARGET "${MODULE_NAME}_ide")

        add_library(${IDE_TARGET} OBJECT EXCLUDE_FROM_ALL)

        target_sources(${IDE_TARGET} PRIVATE ${ALL_SOURCES})

        # 关键：通过 -include 强制注入聚合头，模拟 unity build 的上下文
        target_compile_options(${IDE_TARGET} PRIVATE "SHELL:-include ${AMALGAM_H}")

        # 复制与主目标相同的 include 路径
        target_include_directories(${IDE_TARGET} PRIVATE ${BAKUON_ROOT_DIR}/source
                                                         ${ARG_INCLUDE_DIRS})

        if(ARG_DEPENDS)
            target_link_libraries(${IDE_TARGET} PRIVATE ${ARG_DEPENDS})
        endif()

        message(STATUS "[IDE]  ${IDE_TARGET} compile_commands entries generated")
    endif()

    message(STATUS "[Build] ${MODULE_NAME} (unity build, ${CMAKE_CURRENT_LIST_FILE})")
endfunction()

# ============================================================================
# 注册一个运行时插件（plugins/ 目录下的具体插件实现，例如 plugins/gui/example）
#
# 与 bakuon_add_module()/bakuon_add_amalgamated_module() 的本质区别：
#   - bakuon_add_module 系列产出 STATIC 库，给别的目标 target_link_libraries() 静态链接；
#   - bakuon_add_plugin 产出 MODULE 库（不能被链接，只能被 QPluginLoader/dlopen 在运行时加载），
#     对应 source/gui/b_plugin.h 里 Plugin 接口 + Q_DECLARE_INTERFACE 描述的插件架构。
#
# 调用方需要自行在其 CMakeLists.txt 里于调用本函数之前设置：
#   set(CMAKE_AUTOMOC ON)   # 插件类通常带 Q_OBJECT / Q_PLUGIN_METADATA，需要 moc
#
# 参数：
#   NAME            插件目标名，同时也是输出文件名（不含平台前后缀，见下方 PREFIX 设置）
#   PATH            插件实现代码所在目录，递归 GLOB 其下 .h / .cpp
#   CATEGORY        插件分类子目录（如 gui / core），决定输出到
#                   ${CMAKE_BINARY_DIR}/plugins/<CATEGORY>/ 下；不传则归入 "misc"
#   DEPENDS         该插件需要链接的库，通常至少要有 bakuon::gui
#   INCLUDE_DIRS    额外的 PRIVATE 头文件搜索路径
#   METADATA        可选，插件 JSON 元数据文件路径（对应 b_plugin.h 顶部注释里的
#                   MetaData 示例格式）；若提供，会在插件构建后原样拷贝一份到
#                   输出目录旁（具体是否还需要这份旁置 json，见下方 TODO 2）
#
# 用法示例（plugins/gui/example/CMakeLists.txt）：
#   set(CMAKE_AUTOMOC ON)
#   bakuon_add_plugin(
#       NAME example_plugin
#       PATH ${CMAKE_CURRENT_SOURCE_DIR}
#       CATEGORY gui
#       DEPENDS bakuon::gui
#       METADATA ${CMAKE_CURRENT_SOURCE_DIR}/example_plugin.json)
#
# ----------------------------------------------------------------------------
# TODO（以下几点目前还没有最终拍板，先按最保守的方式实现，不要在没确认前当成定论）：
#  1. 插件发现机制未定：宿主（standalone）是固定扫描某个编译期已知目录，
#     还是运行时可配置路径 + QCoreApplication::addLibraryPath()？
#     这决定了插件产物最终应该躺在哪——当前先输出到
#     ${CMAKE_BINARY_DIR}/plugins/<CATEGORY>/，方便调试期直接找到，
#     是否需要 POST_BUILD 再拷贝一份到 standalone 可执行文件同级目录，
#     等 standalone 里真的写了 QPluginLoader 加载逻辑后再决定。
#  2. JSON 元数据：是通过 Q_PLUGIN_METADATA(FILE "xxx.json") 在编译期直接嵌入插件二进制
#     （Qt 官方推荐、无需额外部署文件），还是同时也需要在 plugins/ 目录下保留一份
#     可读 .json（比如给非 Qt 的启动脚本 / 打包工具解析插件列表用）？
#     如果确认只用 Q_PLUGIN_METADATA 嵌入，下面 METADATA 相关的 add_custom_command
#     可以整段删掉。
#  3. 插件 ABI / CompatVersion 兼容性检查（b_plugin.h 顶部 JSON 示例里的 CompatVersion 字段）
#     目前完全没有落地代码，无论是 host 侧运行时校验还是 CMake 层面的版本号注入，
#     这里都还没有涉及。
#  4. 是否需要 install(TARGETS ...) 把插件也装进最终发布包，取决于产品打包方式，
#     目前跟 bakuon_install_module() 一样按 BAKUON_INSTALL_SDK 关闭，先不处理。
# ============================================================================
function(bakuon_add_plugin)
    cmake_parse_arguments(
        ARG
        ""
        "NAME;PATH;CATEGORY;METADATA"
        "DEPENDS;INCLUDE_DIRS"
        ${ARGN})

    set(PLUGIN_NAME ${ARG_NAME})
    set(PLUGIN_PATH ${ARG_PATH})

    if(NOT ARG_CATEGORY)
        set(ARG_CATEGORY "misc")
    endif()

    _bakuon_glob_module_files("${PLUGIN_PATH}" ALL_HEADERS ALL_SOURCES)

    if(NOT ALL_SOURCES)
        message(FATAL_ERROR "bakuon_add_plugin(${PLUGIN_NAME}): 在 ${PLUGIN_PATH} 下没有找到任何 .cpp 文件。")
    endif()

    # MODULE：只能被动态加载，不能被 target_link_libraries() 静态链接，
    # 这是与 bakuon_add_module()/bakuon_add_amalgamated_module() 的核心区别。
    add_library(${PLUGIN_NAME} MODULE)

    target_sources(${PLUGIN_NAME} PRIVATE ${ALL_SOURCES} ${ALL_HEADERS})

    target_include_directories(${PLUGIN_NAME} PRIVATE ${PLUGIN_PATH} ${ARG_INCLUDE_DIRS})

    if(ARG_DEPENDS)
        target_link_libraries(${PLUGIN_NAME} PRIVATE ${ARG_DEPENDS})
    endif()

    _bakuon_apply_common_target_settings(${PLUGIN_NAME})

    # 按分类输出到统一的 plugins/ 目录下，与源码里 plugins/<CATEGORY>/ 的物理布局对应；
    # LIBRARY_OUTPUT_DIRECTORY 对应 Linux/macOS 的 .so/.dylib，RUNTIME_OUTPUT_DIRECTORY
    # 对应 Windows 的 .dll（Windows 上 MODULE 产物走 RUNTIME 而不是 LIBRARY 属性）。
    set_target_properties(
        ${PLUGIN_NAME}
        PROPERTIES LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins/${ARG_CATEGORY}"
                   RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins/${ARG_CATEGORY}"
                   PREFIX "" # 不加平台默认的 lib 前缀，插件文件名与 NAME 保持一致
    )

    if(ARG_METADATA AND EXISTS "${ARG_METADATA}")
        # 见上方 TODO 2：这里先原样拷贝一份 json 到插件产物旁，是否真的需要、
        # 是否应改为纯 Q_PLUGIN_METADATA 编译期嵌入，等确认后再调整/删除本段。
        add_custom_command(
            TARGET ${PLUGIN_NAME}
            POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${ARG_METADATA}"
                    "$<TARGET_FILE_DIR:${PLUGIN_NAME}>/$<TARGET_FILE_BASE_NAME:${PLUGIN_NAME}>.json"
        )
    endif()

    message(STATUS "[Plugin] ${PLUGIN_NAME} (${ARG_CATEGORY}, ${CMAKE_CURRENT_LIST_FILE})")
endfunction()
