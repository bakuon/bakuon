cmake_minimum_required(VERSION 3.21 FATAL_ERROR)

project(
    bakuon
    VERSION 1.0.0
    LANGUAGES CXX)

# 记录仓库真正的根目录。一旦 bakuon 被上层工程通过 add_subdirectory() 集成，
# CMAKE_SOURCE_DIR 会指向上层工程的根目录而非本仓库根目录，
# 因此所有子模块的 PUBLIC include 路径都必须引用 BAKUON_ROOT_DIR，而不是 CMAKE_SOURCE_DIR。
set(BAKUON_ROOT_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
set(BAKUON_CMAKE_DIR "${BAKUON_ROOT_DIR}/cmake")
list(APPEND CMAKE_MODULE_PATH "${BAKUON_CMAKE_DIR}")

# 加载 bakuon 自身的 cmake 模块（函数 / 第三方模块 / 包管理）
include(BakuonCMake)

# ============================================================================
# 顶层可选项
# 当 bakuon 被作为子目录集成进其他工程时（PROJECT_IS_TOP_LEVEL 为 FALSE），
# 默认关闭 tests / examples / standalone，只暴露 core / gui / plugin 三个库。
# ============================================================================
option(BAKUON_BUILD_TESTS "构建 bakuon 单元测试" ${PROJECT_IS_TOP_LEVEL})
option(BAKUON_BUILD_EXAMPLES "构建 bakuon 示例程序" ${PROJECT_IS_TOP_LEVEL})
option(BAKUON_BUILD_STANDALONE "构建 standalone 主程序" ${PROJECT_IS_TOP_LEVEL})
option(BAKUON_BUILD_PLUGINS "构建 plugins/ 下的运行时插件（MODULE/SHARED）" ${PROJECT_IS_TOP_LEVEL})
option(BAKUON_ENABLE_WARNINGS "启用 -Wall -Wextra / W4 等编译警告" ON)
option(BAKUON_WARNINGS_AS_ERRORS "将编译警告视为错误" OFF)
option(BAKUON_INSTALL_SDK "安装 core/gui/plugin 的头文件与导出 target（打包方案未定，默认关闭）" OFF)

# ----------------------------------------------------------------------------
# C++ 标准通过 target_compile_features(cxx_std_20) 按目标下发（见
# cmake/BakuonUtils.cmake），刻意不设置全局 CMAKE_CXX_STANDARD，
# 以免连带影响 vendor 下第三方依赖（如 googletest）的编译标准。
# ----------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# 框架内所有静态库都以位置无关代码编译，
# 以便未来被链接进以共享库/插件形式加载的模块（plugins/ 下的运行时插件）。
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# 必须在 add_subdirectory() 之前设置，否则 add_compile_options 是目录属性，
# 不会回溯应用到之前已经 add_subdirectory 进来的目标。
# 注意：
# 与 gtest-all 或其他第三方库中的 /utf-8 冲突
# /utf-8 和 /source-charset:utf-8 都用于指定源文件的字符集为 UTF-8，但这两个选项实际上是重复的，
# 并且在同一个编译命令中同时使用它们会导致冲突。将原有的分开设置的选项替换为单一的
# /utf-8 选项。/utf-8 等效于同时设置 /source-charset:utf-8 和 /execution-charset:utf-8，且能避免冲突。
if(MSVC)
    add_compile_options("/utf-8")
    # 可选：如果希望更严格地控制，可以使用 target_compile_options 针对特定目标
    # target_compile_options(your_target_name PRIVATE "/utf-8")
endif()

# ============================================================================
# 子目录
# ============================================================================
add_subdirectory(vendor)
add_subdirectory(source)

if(BAKUON_BUILD_STANDALONE)
    add_subdirectory(standalone)
endif()

if(BAKUON_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

if(BAKUON_BUILD_PLUGINS)
    add_subdirectory(plugins)
endif()

if(BAKUON_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
