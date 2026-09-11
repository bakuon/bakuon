cpmaddpackage(
    NAME googletest
    GITHUB_REPOSITORY google/googletest
    GIT_TAG release-1.12.1 # 建议指定具体版本标签，如 v1.14.0 或 release-1.12.1
    VERSION 1.12.1
    OPTIONS "INSTALL_GTEST OFF" # 禁止安装 GTest 到系统目录，加速构建
            "gtest_force_shared_crt ON" # Windows MSVC 下强制使用动态运行时库（可选，视项目需求而定）
)

target_link_libraries(bakuon_vendor INTERFACE gtest gtest_main)
