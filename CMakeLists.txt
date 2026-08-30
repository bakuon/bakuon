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
option(BAKUON_BUILD_SANDBOX_RUNTIME "构建 source/sandbox/ 下的插件沙箱子进程可执行文件（依赖 BAKUON_BUILD_SANDBOX）"
       ${PROJECT_IS_TOP_LEVEL})
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

# ============================================================================
# 统一产物输出目录
#
# bakuon::gui 现在是 SHARED 库（见 cmake/BakuonUtils.cmake 的 bakuon_add_module(SHARED)
# 和 source/gui/CMakeLists.txt），任何直接链接它的可执行文件在启动时都必须能找到
# 对应的 .dll/.so，因此把所有可执行文件和"运行期"产物统一收拢到同一个 bin/ 目录：
#
#   - Windows：SHARED 库的 .dll 和 .exe 都由 RUNTIME_OUTPUT_DIRECTORY 控制，
#     统一到 bin/ 后，Windows 默认 DLL 搜索顺序里的"应用程序所在目录"就能直接命中，
#     不需要额外拷贝或修改 PATH。
#   - Linux/macOS：.so 由 LIBRARY_OUTPUT_DIRECTORY 控制。可执行文件启动时如何定位
#     "构建目录里的" .so 是 CMake 的默认行为——只要没有显式设置 CMAKE_SKIP_BUILD_RPATH，
#     CMake 会自动为链接了该 .so 的目标写入指向其构建位置的 RPATH，构建树内直接运行
#     天然可用，这里不需要再手动摆弄 RPATH（真正 install() 打包时的 RPATH 策略留给
#     BAKUON_INSTALL_SDK 相关 TODO，见 cmake/BakuonUtils.cmake）。
#
# plugins/ 下用 bakuon_add_plugin() 产出的 MODULE 插件不受这里影响，仍然按分类落在
# ${CMAKE_BINARY_DIR}/plugins/<CATEGORY>/（调试期更容易找到）：插件运行时对 gui 的依赖
# 不需要额外处理——宿主进程（standalone/sandbox_runtime）在 QPluginLoader 加载插件之前
# 已经直接链接并加载过同一份 bakuon::gui，操作系统的动态库加载器在解析插件的隐式依赖时，
# 会按库名/SONAME 复用进程里已加载的那一份，不会再单独去插件所在目录搜索。
# ============================================================================
if(NOT CMAKE_RUNTIME_OUTPUT_DIRECTORY)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
endif()
if(NOT CMAKE_LIBRARY_OUTPUT_DIRECTORY)
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
endif()
if(NOT CMAKE_ARCHIVE_OUTPUT_DIRECTORY)
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
endif()

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
