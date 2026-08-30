#pragma once

#include <array>
#include <unordered_map>
#include <vector>

#include <QtCore/QObject>
#include <QtCore/QPointer>

#include "gui/b_gui_export.h"
#include "gui/b_types.h"

QT_BEGIN_NAMESPACE
class QAction;
QT_END_NAMESPACE

namespace bakuon::gui {

/**
 * @brief 一个具名上下文的运行期状态：它注册了哪些命令的实际 QAction，以及当前是否激活。
 *
 * 命名说明：这不是 b_context.h 里那个"ContextId 集合"的 Context（那个类专门用于
 * ContextFocusRouter 的部件属性标签，语义是"一组 id"，与本类完全不同），
 * 为避免和它撞名，本类叫 ContextState —— "一个具名上下文自己的状态容器"。
 *
 * 一个 ContextState 对象同时承担两件事（对应旧设计里分散在 ContextTracker 全局
 * map 和 Command::m_bindings 里的两份状态，现在合并到"这个上下文自己"名下）：
 *   1. 动作注册表：CommandId -> QAction*，"在这个上下文里，某条命令对应哪个实际动作"
 *   2. 激活引用计数：按 (source, tier) 维度计数，"当前有哪些来源正持有这个上下文"
 *
 * 动作注册可以由持有 shared_ptr<ContextState>（从 ContextArbiter::registerContext()/
 * context() 拿到）的调用方直接调用 addAction()/removeAction()，不需要经过
 * ContextArbiter 转发；但激活状态变更（retain()/release()）通常应该只由
 * ContextArbiter 调用，因为它涉及全局递增的 activationOrder 时钟，需要
 * ContextArbiter 统一分配。
 */
class BAKUON_GUI_EXPORT ContextState : public QObject
{
    Q_OBJECT
public:
    explicit ContextState(const ContextId& id, int priority = 0, QObject* parent = nullptr);
    ~ContextState() override = default;

    ContextId id() const noexcept { return m_id; }

    int priority() const noexcept { return m_priority; }
    void setPriority(int priority) noexcept { m_priority = priority; }

    // ── 动作注册 ───────────────────────────────────────────────

    /**
     * @brief 为该上下文注册某条命令的实际 QAction。action 生命周期由调用方管理，
     * 本类只持有 QPointer 弱引用；action 销毁时自动从表中摘除并发出 actionsChanged()。
     * 同一个 cmdId 重复注册视为替换旧绑定(override action)。
     */
    void addAction(const CommandId& cmdId, QAction* action);
    void removeAction(const CommandId& cmdId);
    QAction* action(const CommandId& cmdId) const;
    bool hasAction(const CommandId& cmdId) const noexcept;
    // 该上下文下当前注册了动作的全部命令 id，用于"某条命令在哪些上下文里出现"之类的反向查询。
    std::vector<CommandId> commandIds() const;

    // ── 激活状态 ───────────────
    bool isActive() const noexcept;
    ContextTier effectiveTier() const noexcept;
    uint64_t activationOrder() const noexcept { return m_activationOrder; }

Q_SIGNALS:
    // 动作注册表发生变化（addAction/removeAction，或已注册的 action 被外部销毁）时发出，
    // ContextArbiter 会监听这个信号来触发重新仲裁。
    void actionsChanged();

private:
    // ── 激活状态（通常由 ContextArbiter 代为调用） ───────────────

    friend class ContextArbiter; // retain / release / releaseAll / setActivationOrder 仅仲裁器可调用
    // 激活 API 私有化：retain / release / releaseAll / setActivationOrder
    // 仅 friend ContextArbiter 可调，避免外部破坏全局时钟语义。

    /**
     * @brief 引用源计数增加
     * @return true 表示这次 retain 让本上下文从"整体未激活"变为"整体激活"——
     *         调用方（ContextArbiter）应据此分配一个新的 activationOrder。
     */
    bool retain(const void* source, ContextTier tier);
    /**
     * @brief 引用源计数减少
     * @return true 表示这次 release 让本上下文从"整体激活"变为"整体未激活"。
     */
    bool release(const void* source, ContextTier tier);
    /**
     * @brief 释放所有引用源
     * @param source 在本上下文里持有的全部 tier 引用一次性释放（部件析构时收尾清理）。
     */
    void releaseAll(const void* source);
    /**
     * @brief 由 ContextArbiter 在 retain() 返回 true 时调用，传入全局递增时钟的下一个值。
     */
    void setActivationOrder(uint64_t order) noexcept { m_activationOrder = order; }

    static constexpr size_t kTierCount = static_cast<size_t>(ContextTier::TierCount);
    using TierCounts                   = std::array<int, kTierCount>;
    static bool anyTierActive(const TierCounts& counts) noexcept;

    struct RefKey
    {
        const void* source                   = nullptr;
        ContextTier tier                     = ContextTier::Foreground;
        bool operator==(const RefKey&) const = default;
    };
    struct RefKeyHash
    {
        size_t operator()(const RefKey& key) const noexcept
        {
            size_t seed = std::hash<const void*>{}(key.source);
            seed ^= std::hash<int>{}(static_cast<int>(key.tier)) + 0x9e3779b97f4a7c15ULL
                    + (seed << 6) + (seed >> 2);
            return seed;
        }
    };

    ContextId m_id;
    int m_priority;
    uint64_t m_activationOrder = 0;

    std::unordered_map<CommandId, QPointer<QAction>> m_actions;
    std::unordered_map<RefKey, int, RefKeyHash> m_refCounts;
    TierCounts m_tierCounts{};
};

} // namespace bakuon::gui
