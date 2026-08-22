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
    NAMES Qt6 Qt5
    COMPONENTS Core Gui Widgets Concurrent
    REQUIRED)
find_package(
    Qt${QT_VERSION_MAJOR}
    COMPONENTS Core Gui Widgets Concurrent
    REQUIRED)

bakuon_add_module(
    NAME
    ${PROJECT_NAME}
    PATH
    ${CMAKE_CURRENT_SOURCE_DIR}
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
