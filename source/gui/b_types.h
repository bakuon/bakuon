#pragma once

#include <vector>

#include "core/b_contextactivation.h"
#include "gui/b_id.h"

namespace bakuon::gui {

struct ContextTag
{
};
struct CommandTag
{
};

using ContextId = Id<ContextTag>;
using CommandId = Id<CommandTag>;

// 从 core 再导出：gui 与 core 共用同一枚举，避免双份定义漂移。
using ContextTier = core::ContextTier;

struct Candidate
{
    ContextId context;
    int priority = 0;
};

} // namespace bakuon::gui
