#pragma once

#include <vector>

#include "gui/b_id.h"

namespace bakuon::gui {

// Tag 类型仅用于区分 Id 的种类，不需要定义具体内容
struct ContextTag
{
};
struct CommandTag
{
};

using ContextId = Id<ContextTag>; // 上下文标识：如 "editor.image.focused"
using CommandId = Id<CommandTag>; // 命令标识：如 "edit.delete"

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
enum class ContextTier : uint8_t {

    Background = 0, // 后台/异步任务产生的临时上下文（进度提示、后台处理状态等）。
                    // 任何情况下都不应该压过真正的交互上下文，因此数值最低。
    Foreground = 1, // 前台/交互(Interactive)层级：由焦点、鼠标点击等真实用户交互驱动 (默认)。
                    // pushContext() 的默认参数就是这个值。

    TierCount // tier 数量，不要在这之后添加枚举值。
};

struct Candidate
{
    ContextId context;
    int priority = 0;
};

/**
 * @brief 上下文仲裁接口：给定一组 (context, priority) 候选，决定哪一个当前应该生效。
 *
 * 引入动机：Command 原本直接依赖 ContextTracker 这个具体类型，自己拿
 * isActiveContext()/effectiveTier()/activationOrder() 三个底层查询做仲裁循环——
 * ContextTracker 的内部数据模型（tier 计数、激活时序）和"仲裁规则"这两件事被拆到了
 * 两个类里，Command 里散落着一份仲裁逻辑的复制。现在把仲裁规则整体收进
 * ContextTracker（它本来就拥有全部相关状态，是更自然的归属），Command 只需要
 * 认识这一个方法，不需要知道 ContextTier/activationOrder 这些实现细节。
 *
 * 好处是可测试性：单元测试 Command 时，可以用一个不依赖 QObject/Qt 信号、
 * 不需要真的维护激活集合的最小假实现（比如固定返回某个下标）来驱动它，
 * 不需要构造一个功能齐全的 ContextTracker。
 */
class IContextArbiter
{
public:
    virtual ~IContextArbiter() = default;

    /**
     * @brief 从候选集合里选出当前应该生效的一个。
     * @param candidates 调用方（通常是 Command）提供的候选列表，下标顺序由调用方决定，
     *                   实现方（通常是 ContextTracker）不关心候选来自哪里，只按
     *                   (是否激活, tier, priority, 激活时序) 的规则挑选。
     * @return candidates 里胜出者的下标；没有任何候选当前处于激活状态则返回 -1。
     * @todo 仲裁结果不再返回"m_bindings 的下标"，而是直接返回"胜出的 ContextId"——
     *       Command 收到后自己 setAuthoritativeContext(ContextId) 即可，下标是
     *       Command 自己的内部实现细节。然后 m_bindings 以 Context 为 key 的无序表存储结构。
     */
    [[nodiscard]] virtual int resolveAuthoritative(
        const std::vector<Candidate>& candidates) const = 0;
};

} // namespace bakuon::gui
