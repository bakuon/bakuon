#pragma once

// ============================================================================
// bakuon::core 门面头文件（facade）—— 参见 Registry.h/Entity.h 顶部的说明。
//
// Serializer<Components...> 是基于 entt::snapshot/entt::snapshot_loader
// 的 编译期已知组件集合的整体序列化器，基于 IArchiveWriter/IArchiveReader（
// 详见 Archive.h/ComponentArchive.h），与 UndoStack（进程内撤销/重做用）互补，
// 详见 source/core/b_serializer.h 的类文档。
// ============================================================================

#include "core/b_serializer.h" // IWYU pragma: export
