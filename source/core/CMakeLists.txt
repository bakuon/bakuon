cmake_minimum_required(VERSION 3.20 FATAL_ERROR)

# ============================================================================
# bakuon core project configure
# ============================================================================
project(
    core
    VERSION 1.0.0
    LANGUAGES CXX)

file(GLOB_RECURSE ALL_HEADERS "${CMAKE_CURRENT_SOURCE_DIR}/*.h")
file(GLOB_RECURSE ALL_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")

add_library(${PROJECT_NAME} STATIC)
add_library(bakuon::${PROJECT_NAME} ALIAS ${PROJECT_NAME})

target_sources(${PROJECT_NAME} PRIVATE ${ALL_SOURCES} ${ALL_HEADERS})

target_include_directories(
    ${PROJECT_NAME}
    PUBLIC ${CMAKE_SOURCE_DIR}/source
    PRIVATE ${CMAKE_SOURCE_DIR}/include)

target_compile_definitions(
    ${PROJECT_NAME}
    PRIVATE BAKUON_LIBRARY
    INTERFACE BAKUON_IMPORT)

# ============================================================================
# bakuon core testing
# ============================================================================
# enable_testing()
