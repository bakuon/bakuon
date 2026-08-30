cmake_minimum_required(VERSION 3.20 FATAL_ERROR)

# ============================================================================
# bakuon::gui —— 基于 Qt 的 GUI 框架层：command / context / plugin 架构主体。
# ============================================================================
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
    COMPONENTS Core
               Gui
               Widgets
               Concurrent
    REQUIRED)
find_package(
    Qt${QT_VERSION_MAJOR}
    COMPONENTS Core
               Gui
               Widgets
               Concurrent
    REQUIRED)

# SHARED：gui 从静态库改为动态库（.dll/.so/.dylib），是插件 / 沙箱进程 / Host 应用程序
# 三方能"动态连接"到同一份 bakuon::gui 的前提——STATIC 库分别静态链接进多个二进制时，
# 每个二进制各自持有一份独立的 ExtensionSystem::instance() 之类的进程内单例，见
# include/bakuon/gui/PluginContext.h 顶部关于这一点的详细说明（那段注释也已同步更新）。
# 导出宏（BAKUON_GUI_EXPORT）由 bakuon_add_module(SHARED) 自动生成，落在
# "gui/b_gui_export.h"；哪些类/自由函数需要标注见各自声明处。
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
    Qt${QT_VERSION_MAJOR}::Concurrent
    # 面向插件开发者的稳定门面头文件根目录：include/bakuon/gui/*.h，
    # 以 PUBLIC 方式传播给 target_link_libraries(... bakuon::gui) 的消费者，
    # 让第三方插件写 #include "bakuon/gui/Plugin.h" 而不是 #include "gui/b_plugin.h"。
    PUBLIC_INCLUDE_DIRS
    ${BAKUON_ROOT_DIR}/include)
