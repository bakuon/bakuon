cmake_minimum_required(VERSION 3.20 FATAL_ERROR)

# ============================================================================
# bakuon core project configure
# ============================================================================
project(
    bakuon_plugin
    VERSION 1.0.0
    LANGUAGES CXX)

bakuon_add_module(
    NAME
    ${PROJECT_NAME}
    PATH
    ${CMAKE_CURRENT_SOURCE_DIR})

# set_target_properties(${PROJECT_NAME} PROPERTIES POSITION_INDEPENDENT_CODE ON)
