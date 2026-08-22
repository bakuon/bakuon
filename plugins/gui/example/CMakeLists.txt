cmake_minimum_required(VERSION 3.20 FATAL_ERROR)

# ============================================================================
# example_plugin —— bakuon_add_plugin() 的最小可用示例/脚手架。
# 除了验证 bakuon_add_plugin() 本身能跑通之外，也给以后新增插件的人当模板抄。
# ============================================================================
project(
    example_plugin
    VERSION 1.0.0
    LANGUAGES CXX)

# 插件类使用 Q_OBJECT / Q_PLUGIN_METADATA，需要在调用 bakuon_add_plugin() 之前打开 AUTOMOC。
set(CMAKE_AUTOMOC ON)

bakuon_add_plugin(
    NAME
    ${PROJECT_NAME}
    PATH
    ${CMAKE_CURRENT_SOURCE_DIR}
    CATEGORY
    gui
    DEPENDS
    bakuon::gui
    METADATA
    ${CMAKE_CURRENT_SOURCE_DIR}/example_plugin.json)
