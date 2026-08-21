#pragma once

#include "gui/detail/b_strongid.h"

namespace bakuon::gui {

// Tag 类型仅用于区分 StrongId 的种类，不需要定义具体内容
struct ContextTag
{
};
struct CommandTag
{
};

using ContextId = StrongId<ContextTag>; // 上下文标识：如 "editor.image.focused"
using CommandId = StrongId<CommandTag>; // 命令标识：如 "edit.delete"

} // namespace bakuon::gui
