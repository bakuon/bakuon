#pragma once

// ============================================================================
// bakuon::core 门面头文件（facade）—— 参见 Registry.h/Entity.h 顶部的说明。
//
// Hierarchy 是场景/文档树"父子关系"这一常见模式的标准组件 + 自由函数集合，
// 建立在 Registry 的公开接口之上（不需要、也没有 Registry 内部状态的特权），
// 详见 source/core/b_hierarchy.h 的类文档。
// ============================================================================

#include "core/b_hierarchy.h" // IWYU pragma: export
