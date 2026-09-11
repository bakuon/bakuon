set(TARGET_NAME "example_menubar")

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

add_executable(
    ${TARGET_NAME}
    CustomizeMenuDialog.cpp
    CustomizeMenuDialog.h
    EditorSurface.cpp
    EditorSurface.h
    main.cpp
    MainWindow.cpp
    MainWindow.h)

target_link_libraries(${TARGET_NAME} PRIVATE bakuon::gui)
target_compile_features(${TARGET_NAME} PRIVATE cxx_std_20)
bakuon_apply_warnings(${TARGET_NAME})
