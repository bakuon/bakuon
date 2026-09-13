cmake_minimum_required(VERSION 3.20 FATAL_ERROR)

project(
    gui
    VERSION 1.0.0
    LANGUAGES CXX)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(
    QT
    NAMES
    Qt6
    Qt5
    COMPONENTS Core Gui Widgets
    REQUIRED)
find_package(
    Qt${QT_VERSION_MAJOR}
    COMPONENTS Core Gui Widgets
    REQUIRED)

# SHARED: 从静态库改为动态库（.dll/.so/.dylib），是插件 / 沙箱进程 / Host 应用程序
# 三方能"动态连接"到同一份 bakuon 的前提——STATIC 库分别静态链接进多个二进制时，
# 每个二进制各自持有一份独立的进程内单例。
# 导出宏（BAKUON_GUI_EXPORT）由 bakuon_add_module(SHARED) 自动生成，落在
# "bakuon/b_gui_export.h"；哪些类/自由函数需要标注见各自声明处。
bakuon_add_module(
    NAME
    ${PROJECT_NAME}
    PATH
    ${CMAKE_CURRENT_SOURCE_DIR}
    SHARED
    DEPENDS
    Qt${QT_VERSION_MAJOR}::Core
    Qt${QT_VERSION_MAJOR}::Gui
    Qt${QT_VERSION_MAJOR}::Widgets
    # RegistryBridge（b_registrybridge.h）把 core::Registry 的观察者转成 Qt 信号，
    # 是 gui 目前唯一真正依赖 core 的地方——core 本身仍然完全不知道 Qt 的存在，
    # 依赖方向是单向的（gui -> core，core 不反向依赖 gui）。见根 source/CMakeLists.txt
    # 里对应的 BAKUON_BUILD_GUI 需要 BAKUON_BUILD_CORE=ON 的显式校验。
    bakuon::core
    # 面向插件开发者的稳定门面头文件根目录：include/bakuon/gui/*.h，
    # 以 PUBLIC 方式传播给 target_link_libraries(... bakuon::gui) 的消费者，
    # 让第三方插件写 #include "bakuon/gui/Plugin.h" 而不是 #include "gui/b_plugin.h"。
    PUBLIC_INCLUDE_DIRS
    ${BAKUON_ROOT_DIR}/include)
