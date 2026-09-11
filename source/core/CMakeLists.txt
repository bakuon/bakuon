# ============================================================================
# bakuon::core —— 纯标准 C++ 静态库，不依赖 Qt，可被 gui / plugin / sandbox 复用。
#
# 首批实现：基于 EnTT（header-only、MIT 协议、零依赖的 ECS 库）的通用实体-组件
# 状态容器（Registry / EntityId / Connection，见 b_registry.h 的类文档），
# 用作"GUI 复杂状态"的底层数据驱动——文档/场景里的每个对象对应一个 EntityId，
# 具体属性以组件形式挂载/摘除，gui 层通过 Registry::onConstruct/onUpdate/
# onDestroy 监听变化、自行决定如何映射成界面刷新，core 本身完全不知道 Qt 的存在。
#
# 选 EnTT 而不是自研一套观察者/属性系统：契合"避免造轮子、已有 STL 不重复造"的
# 项目原则；EnTT 本身零外部依赖，与 core "不依赖 Qt" 的定位天然契合。
# ============================================================================
set(TARGET_NAME "core")

bakuon_add_module(
    NAME
    ${TARGET_NAME}
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
