#pragma once

// ============================================================================
// bakuon::core 门面头文件（facade）—— 参见 Entity.h 顶部的说明。
//
// Registry 是 core 模块目前的核心入口：GUI 无关的实体-组件状态容器，详见
// source/core/b_registry.h 的类文档（设计动机、使用示例、线程模型）。
// ============================================================================

#include "core/b_connection.h" // IWYU pragma: export
#include "core/b_handle.h"     // IWYU pragma: export
#include "core/b_registry.h"   // IWYU pragma: export
