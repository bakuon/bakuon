cmake_minimum_required(VERSION 3.20 FATAL_ERROR)

# ============================================================================
# bakuon core project configure
# ============================================================================
project(
    bakuon_core
    VERSION 1.0.0
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

bakuon_add_module(
    NAME
    ${PROJECT_NAME}
    PATH
    ${CMAKE_CURRENT_SOURCE_DIR})

target_compile_definitions(
    ${PROJECT_NAME}
    PRIVATE BAKUON_LIBRARY
    INTERFACE BAKUON_IMPORT)

# ============================================================================
# bakuon core testing
# ============================================================================
# enable_testing()
