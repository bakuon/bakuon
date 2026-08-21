cmake_minimum_required(VERSION 3.20 FATAL_ERROR)

project(
    bakuon
    VERSION 1.0.0
    LANGUAGES CXX)

set(BAKUON_CMAKE_DIR "${CMAKE_CURRENT_LIST_DIR}/cmake")
list(APPEND CMAKE_MODULE_PATH "${BAKUON_CMAKE_DIR}")

# include the master CMake module
include(BakuonCMake)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_subdirectory(vendor)
add_subdirectory(source)
add_subdirectory(standalone)
add_subdirectory(examples)
add_subdirectory(tests)

if(MSVC)
    add_compile_options("/source-charset:utf-8" "/execution-charset:utf-8")
endif()
