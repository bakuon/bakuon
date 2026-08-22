# vendor —— 第三方依赖的统一入口
add_library(bakuon_vendor INTERFACE)

# 仅在需要构建测试时才拉取 googletest，避免非测试场景下的无谓下载/编译开销。
if(BAKUON_BUILD_TESTS)
    add_subdirectory(gtest EXCLUDE_FROM_ALL)
endif()
