cmake_minimum_required(VERSION 3.20 FATAL_ERROR)

project(example_menubar VERSION 1.0.0)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

add_executable(
    example_menubar
    CustomizeMenuDialog.cpp
    CustomizeMenuDialog.h
    EditorSurface.cpp
    EditorSurface.h
    main.cpp
    MainWindow.cpp
    MainWindow.h)

target_link_libraries(${PROJECT_NAME} PRIVATE bakuon::gui)
