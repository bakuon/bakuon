cmake_minimum_required(VERSION 3.20 FATAL_ERROR)

project(gui VERSION 1.0.0)

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

# 收集所有子文件
file(GLOB_RECURSE ALL_HEADERS "${CMAKE_CURRENT_SOURCE_DIR}/*.h")
file(GLOB_RECURSE ALL_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")

add_library(${PROJECT_NAME} STATIC)
add_library(bakuon::${PROJECT_NAME} ALIAS ${PROJECT_NAME})

target_sources(${PROJECT_NAME} PRIVATE ${ALL_SOURCES} ${ALL_HEADERS})

target_include_directories(
    ${PROJECT_NAME}
    PUBLIC ${CMAKE_SOURCE_DIR}/source
    PRIVATE ${CMAKE_SOURCE_DIR}/include)

target_link_libraries(
    ${PROJECT_NAME}
    PUBLIC Qt${QT_VERSION_MAJOR}::Core
           Qt${QT_VERSION_MAJOR}::Gui
           Qt${QT_VERSION_MAJOR}::Widgets
           Qt${QT_VERSION_MAJOR}::Concurrent)

message(STATUS "[Build] ${PROJECT_NAME} (Non unity build, ${CMAKE_CURRENT_LIST_FILE})")
