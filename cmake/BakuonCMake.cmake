# ============================================================================
# bakuon cmake modules
# ============================================================================
include(BakuonUtils)

# ============================================================================
# Native cmake modules
# ============================================================================
include(CMakeParseArguments)
include(CPM)
include(GNUInstallDirs) # 为将来的 install()/export() 预留，当前不强制使用
include(GenerateExportHeader) # bakuon_add_module(... SHARED) 用它生成 dllexport/dllimport 导出宏

# ============================================================================
# third_party cmake modules
# ============================================================================
include(GoogleTest) # 提供 gtest_discover_tests()，仅在 BAKUON_BUILD_TESTS 时实际用到
