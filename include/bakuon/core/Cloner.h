#pragma once

// ============================================================================
// bakuon::core 门面头文件（facade）—— 参见 Entity.h 顶部的说明。
//
// Cloner: 运行时、类型擦除的组件克隆，
// 插件在自己的编译单元里 addComponent<T>()，core 完全不需要在编译期
// 认识 T。详见 source/core/b_cloner.h 的类文档。
// ============================================================================

#include "core/b_cloner.h" // IWYU pragma: export
