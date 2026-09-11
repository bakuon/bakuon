set(module "core")

bakuon_add_module(
    NAME
    ${module}
    PATH
    ${CMAKE_CURRENT_SOURCE_DIR}
    DEPENDS
    EnTT::EnTT
    INCLUDE_DIRS
    ${BAKUON_ROOT_DIR}/include
    # 面向消费者（gui/plugin/sandbox）的稳定门面头文件根目录：include/bakuon/core/*.h，
    # 约定与 source/gui/CMakeLists.txt 里 bakuon::gui 的 PUBLIC_INCLUDE_DIRS 完全一致。
    PUBLIC_INCLUDE_DIRS
    ${BAKUON_ROOT_DIR}/include)
