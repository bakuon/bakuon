cmake_minimum_required(VERSION 3.20 FATAL_ERROR)

project(
    sandbox_example
    VERSION 1.0.0
    LANGUAGES CXX)

find_package(
    Qt6
    COMPONENTS Core RemoteObjects
    REQUIRED)

add_executable(${PROJECT_NAME} main.cpp)
target_link_libraries(${PROJECT_NAME} PRIVATE bakuon::sandbox Qt6::Core Qt6::RemoteObjects)
target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_20)
bakuon_apply_warnings(${PROJECT_NAME})
