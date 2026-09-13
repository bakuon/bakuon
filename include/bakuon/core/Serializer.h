#pragma once

// ============================================================================
// bakuon::core 门面头文件（facade）—— 参见 Registry.h/Handle.h 顶部的说明。
//
// DocumentSerializer<Components...> 是基于 entt::snapshot/entt::snapshot_loader
// 的 JSON 文档序列化器（落盘用），与 UndoStack（进程内撤销/重做用）互补，
// 详见 source/core/b_serializer.h 的类文档。
// ============================================================================

#include "core/b_serializer.h" // IWYU pragma: export
