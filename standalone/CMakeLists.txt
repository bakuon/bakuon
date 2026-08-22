cmake_minimum_required(VERSION 3.20 FATAL_ERROR)

project(standalone VERSION 1.0.0)

add_executable(standalone main.cpp)

target_link_libraries(${PROJECT_NAME} PRIVATE bakuon::gui)
target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_20)
bakuon_apply_warnings(${PROJECT_NAME})
