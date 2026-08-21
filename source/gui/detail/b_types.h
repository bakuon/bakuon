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

// ContextTier：上下文的"层级"，用于在多个同时激活的上下文之间做仲裁时，
// 先于"时序（activationOrder）"比较——层级更高者无条件胜出，层级相同时才比时序。
//
// 引入动机：原本 Command::findAuthoritativeIndex() 只按"谁最近激活"裁决权威源，
// 这在"后台/异步任务反复刷新自己的临时上下文"时会有问题——一个后台滤镜任务
// 只要隔三差五 push 一下它的临时上下文，就能在时序上不断刷新自己、持续压过
// 真正被用户聚焦的实例，导致快捷键/按钮错误地路由到还在后台跑的旧实例上。
// 层级机制把"这个上下文的激活到底代表不代表真实的用户交互意图"显式区分出来，
// 后台任务只要老老实实用 Background 层级注册，无论刷新多少次都不可能压过
// 任何 Foreground 层级的上下文。
//
// 用 Background 层级 push 的上下文，仍然正常进入激活集合（isActiveContext() 为
// true，绑定在它上面的 realAction 依然可以被启用/触发），但不会推进全局的
// activationOrder 时钟——因此在优先级相同的情况下，它永远不可能仅凭"最近
// 激活"就压过任何一个 Foreground 层级的上下文，哪怕它在挂钟时间上确实更晚。
// 这与"给后台任务的 Command 绑定设置更低的 priority"是两道独立的防线，
// 建议同时使用：priority 防的是"优先级配置正确"的情况，ContextTier
// 防的是"忘记配置 priority、或优先级恰好相同"的情况。
enum class ContextTier : quint8 {

    Background = 0, // 后台/异步任务产生的临时上下文（进度提示、后台处理状态等）。
                    // 任何情况下都不应该压过真正的交互上下文，因此数值最低。
    Foreground = 1, // 前台/交互(Interactive)层级：由焦点、鼠标点击等真实用户交互驱动 (默认)。
                    // pushContext() 的默认参数就是这个值。
};

} // namespace bakuon::gui
