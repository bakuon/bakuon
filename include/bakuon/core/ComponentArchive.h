#pragma once

// ============================================================================
// bakuon::core 门面头文件（facade）—— 参见 Entity.h 顶部的说明。
//
// ComponentArchive：运行时、类型擦除的组件归档注册表，是
// Serializer<Components...>（编译期模板包）的长期替代方案——
// 插件在自己的编译单元里 registerType<T>()，core 完全不需要在编译期
// 认识 T。详见 source/core/b_componentarchive.h 的类文档。
// ============================================================================

#include "core/b_componentarchive.h" // IWYU pragma: export
