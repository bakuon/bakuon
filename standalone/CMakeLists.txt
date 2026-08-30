cmake_minimum_required(VERSION 3.20 FATAL_ERROR)

project(standalone VERSION 1.0.0)

add_executable(standalone main.cpp)

target_link_libraries(${PROJECT_NAME} PRIVATE bakuon::gui)
target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_20)
bakuon_apply_warnings(${PROJECT_NAME})

# 沙箱子系统是可选构建的（BAKUON_BUILD_SANDBOX，见 source/CMakeLists.txt）；
# standalone 用 bakuon::sandbox target 是否存在来判断，而不是直接读那个 cache 变量——
# target 存在与否才是"这次构建到底有没有真的把它编出来"的唯一可靠依据。
if(TARGET bakuon::sandbox)
    target_link_libraries(${PROJECT_NAME} PRIVATE bakuon::sandbox)
    target_compile_definitions(${PROJECT_NAME} PRIVATE BAKUON_STANDALONE_HAVE_SANDBOX)
endif()
