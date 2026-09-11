cmake_minimum_required(VERSION 3.20 FATAL_ERROR)

project(host VERSION 1.0.0)

set(TARGET_NAME "bakuon_${PROJECT_NAME}")

add_executable(
    ${TARGET_NAME}
    Constants.h
    main.cpp
    MainWindow.cpp
    MainWindow.h
    TabContentWidget.cpp
    TabContentWidget.h)

target_link_libraries(${TARGET_NAME} PRIVATE bakuon::gui)
target_compile_features(${TARGET_NAME} PRIVATE cxx_std_20)
bakuon_apply_warnings(${TARGET_NAME})

# host/ 的存在意义就是"一个标签一个进程"的真实 GUI 外壳，TabSandboxManager
# 不是可选项——和 standalone/ 里"没有沙箱也能退化演示"不同，这里没有沙箱
# 这个应用程序本身就没有意义，所以直接 FATAL_ERROR 而不是安静地降级。
if(NOT TARGET bakuon::sandbox)
    message(FATAL_ERROR "host/ 需要 bakuon::sandbox（即 BAKUON_BUILD_SANDBOX=ON，默认开启）"
                        "才能构建：真正的 GUI TabHost 离不开进程外沙箱基础设施。")
endif()
target_link_libraries(${TARGET_NAME} PRIVATE bakuon::sandbox)

# 沙箱子进程 sandbox_runtime 是运行期硬依赖（New Tab 全靠它），生产者产物落在
# 同一个 bin/ 输出目录，构建期显式声明依赖关系，避免并行构建时 host 先于
# sandbox_runtime 完工导致运行时找不到文件的假象。
if(TARGET sandbox_runtime)
    add_dependencies(${TARGET_NAME} sandbox_runtime)
endif()
