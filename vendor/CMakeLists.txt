# ============================================================================
# vendor —— 第三方依赖的统一入口
# ============================================================================

# 仅在需要构建测试时才拉取 googletest，避免非测试场景下的无谓下载/编译开销。
if(BAKUON_BUILD_TESTS)
    cpmaddpackage(
        NAME googletest
        GITHUB_REPOSITORY google/googletest
        GIT_TAG release-1.12.1 # 建议指定具体版本标签，如 v1.14.0 或 release-1.12.1
        VERSION 1.12.1
        OPTIONS "INSTALL_GTEST OFF" # 禁止安装 GTest 到系统目录，加速构建
                "gtest_force_shared_crt ON" # Windows MSVC 下强制使用动态运行时库（可选，视项目需求而定）
        EXCLUDE_FROM_ALL YES
    )
endif()

# 仅在 BAKUON_BUILD_CORE 打开时才拉取 EnTT——虽然 EnTT 本身是 header-only、
# 拉取/配置开销很小，但保持"按需拉取第三方依赖"这一条一致的原则，避免
# 不需要 core 的下游集成（比如只想要 gui/plugin 两个库）也被迫多一次网络请求。
if(BAKUON_BUILD_CORE)
    cpmaddpackage(
        NAME EnTT
        GITHUB_REPOSITORY skypjack/entt
        GIT_TAG v3.16.0 # 钉 tag，不要追 main
        VERSION 3.16.0
        OPTIONS "ENTT_INSTALL OFF" "ENTT_BUILD_TESTING OFF"
        EXCLUDE_FROM_ALL YES
    )

    # nlohmann::json —— header-only、MIT 协议的 JSON 库，用于 core::Serializer
    # 的落盘格式（entt::snapshot/snapshot_loader 的 JSON 归档器实现），见
    # source/core/b_serializer.h 的类文档。与 EnTT 同样按需拉取，不强加给不需要
    # core 的下游集成。
    cpmaddpackage(
        NAME nlohmann_json
        GITHUB_REPOSITORY nlohmann/json
        GIT_TAG v3.11.3
        VERSION 3.11.3
        OPTIONS "JSON_BuildTests OFF" "JSON_Install OFF"
        EXCLUDE_FROM_ALL YES
    )
endif()

bakuon_apply_targets_folder(${CMAKE_CURRENT_LIST_DIR} ALIAS "vendor")
