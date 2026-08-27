#include "gui/b_contexttracker.h"

#include <limits> // std::numeric_limits，resolveAuthoritative() 用到

namespace bakuon::gui {

ContextTracker::ContextTracker(QObject* parent)
    : QObject(parent)
{
}

bool ContextTracker::registerContext(const ContextId& id, const QString& owner,
                                     const QString& description)
{
    if (!id.isValid()) {
        qWarning() << "ContextTracker::registerContext: refuse to register an invalid (empty) "
                      "ContextId, owner:"
                   << owner;
        return false;
    }

    auto it = m_contextRegistry.find(id);
    if (it == m_contextRegistry.end()) {
        m_contextRegistry.emplace(id, ContextInfo{owner, description});
        return true;
    }

    if (it->second.owner != owner) {
        // 两个不相关的调用方争用了同一个上下文字符串——这正是"字符串到处敲、
        // 难免重复"想要在开发期就暴露出来的那类真实 bug，而不是等运行时仲裁
        // 出现诡异行为才发现。
        qWarning() << "ContextTracker::registerContext: naming collision on" << id
                   << "-- already "
                      "owned by"
                   << it->second.owner << ", rejected registration attempt from" << owner;
        return false;
    }

    // 同一个 owner 重复登记：视为幂等更新（例如刷新 description），直接放行。
    it->second.description = description;
    return true;
}

std::optional<ContextTracker::ContextInfo> ContextTracker::contextInfo(const ContextId& id) const
{
    auto it = m_contextRegistry.find(id);
    if (it == m_contextRegistry.end()) {
        return std::nullopt;
    }
    return it->second;
}

std::vector<ContextId> ContextTracker::registeredContexts() const
{
    std::vector<ContextId> result;
    result.reserve(m_contextRegistry.size());
    for (const auto& [id, _] : m_contextRegistry) {
        result.push_back(id);
    }
    return result;
}

void ContextTracker::pushContext(const ContextId& context, const void* source, ContextTier tier)
{
    Q_ASSERT(source != nullptr);

    const RefKey key{context, source, tier};
    int& refCount = m_refCounts[key];
    ++refCount;
    if (refCount > 1) {
        // 同一来源(context, source, tier)组合重复 push，只增加自身计数，总激活状态未发生变化
        // Foreground 每次 push 都刷新 activationOrder（体现"最近被交互到"语义）。
        // if (tier == ContextTier::Foreground) {
        //     m_activationOrder[context] = ++m_activationClock;
        // }
        return;
    }

    TierCounts& tierCounts        = m_contextTierCounts[context]; // 首次访问时值初始化为全 0
    const bool wasOverallInactive = !anyTierActive(tierCounts);
    ++tierCounts[static_cast<size_t>(tier)];

    // 全局首次激活
    if (wasOverallInactive) {
        // 只在"整体从未激活变为激活"时刷新时序戳——tier 只是同一整体激活状态下
        // 的"生效层级"计算依据，不应该因为多来一个不同 tier 的引用就被刷新，
        // 否则会削弱"层级比较严格优先于时序"这个仲裁语义的清晰度。
        m_activationOrder[context] = ++m_activationClock;
    }

    // 无条件广播：即使 context 整体早已激活，这次 push 也可能改变 effectiveTier()
    // 的返回值（比如之前只有 Background 层级的持有者，这次新增了 Interactive
    // 层级的持有者，生效层级从 Background 跃升为 Interactive），必须让所有
    // Command 都有机会重新仲裁。
    emit contextChanged();
}

void ContextTracker::popContext(const ContextId& context, const void* source, ContextTier tier)
{
    Q_ASSERT(source != nullptr);

    const RefKey key{context, source, tier};
    auto it = m_refCounts.find(key);
    if (it == m_refCounts.end() || it->second <= 0) {
        qWarning("ContextTracker::popContext: mismatched pop (context=%s, source=%p)",
                 qPrintable(context.name()),
                 source);
        return; // 未持有该上下文(context, source, tier)组合引用，忽略非法/多余的 pop 调用
    }
    --(it->second);
    if (it->second > 0) {
        return; // 该 source 仍持有引用
    }
    m_refCounts.erase(it);

    auto tierIt = m_contextTierCounts.find(context);
    if (tierIt == m_contextTierCounts.end()) {
        return; // 理论上不会发生：能查到 ref counts 就必然有对应的 tierCounts 条目
    }
    TierCounts& tierCounts = tierIt->second;
    --tierCounts[static_cast<size_t>(tier)];

    if (!anyTierActive(tierCounts)) {
        // 整体失活，清理；m_activationOrder 的记录保留，
        // 下次重新激活时会被覆盖，不需要在这里主动清理
        m_contextTierCounts.erase(tierIt);
    }

    // 同 pushContext：无条件广播，因为这次 pop 也可能改变 effectiveTier() 的返回值
    // （即便 context 整体仍然激活——比如刚好是最后一个 Foreground 层级持有者
    // 释放了，生效层级从 Foreground 回落到 Background）。
    emit contextChanged();
}

void ContextTracker::releaseContext(const void* source)
{
    if (source == nullptr)
        return;

    // 先收集该 source 持有引用的全部 (context, tier)组合上下文，避免遍历过程中修改容器
    std::vector<std::pair<ContextId, ContextTier>> owned;
    for (const auto& [key, count] : m_refCounts) {
        if (key.source == source && count > 0) {
            owned.emplace_back(key.context, key.tier);
        }
    }
    for (const auto& [ctx, tier] : owned) {
        auto it = m_refCounts.find(RefKey{ctx, source, tier});
        while (it != m_refCounts.end() && it->second > 0) {
            popContext(ctx, source, tier);
            it = m_refCounts.find(RefKey{ctx, source, tier});
        }
    }
}

std::unordered_set<ContextId> ContextTracker::activeContexts() const
{
    std::unordered_set<ContextId> result;
    for (const auto& [ctx, counts] : m_contextTierCounts) {
        if (anyTierActive(counts)) {
            result.insert(ctx);
        }
    }
    return result;
}

bool ContextTracker::isActiveContext(const ContextId& context) const noexcept
{
    auto it = m_contextTierCounts.find(context);
    return it != m_contextTierCounts.end() && anyTierActive(it->second);
}

ContextTier ContextTracker::effectiveTier(const ContextId& context) const noexcept
{
    auto it = m_contextTierCounts.find(context);
    if (it == m_contextTierCounts.end()) {
        // 未激活时的安全默认值，调用方应先用 isActiveContext() 判断
        return ContextTier::Foreground;
    }
    const TierCounts& counts = it->second;
    for (int t = static_cast<int>(TierCount) - 1; t >= 0; --t) {
        if (counts[static_cast<size_t>(t)] > 0) {
            return static_cast<ContextTier>(t);
        }
    }
    return ContextTier::Foreground; // 理论上不会走到这里：isActive() 为真必然有某个 tier > 0
}

uint64_t ContextTracker::activationOrder(const ContextId& context) const noexcept
{
    if (!isActiveContext(context)) {
        return 0; // 当前未激活，不参与仲裁
    }
    auto it = m_activationOrder.find(context);
    return it != m_activationOrder.end() ? it->second : 0;
}

int ContextTracker::resolveAuthoritative(const std::vector<Candidate>& candidates) const
{
    ContextTier bestTier = ContextTier::Foreground; // 会被第一个候选无条件覆盖，初值不重要
    int bestIndex        = -1;
    int bestPriority     = std::numeric_limits<int>::min();
    uint64_t bestOrder   = 0;

    for (int i = 0; i < static_cast<int>(candidates.size()); ++i) {
        const Candidate& c = candidates[i];
        if (!isActiveContext(c.context)) {
            continue; // 只在"当前激活"的候选里挑选
        }

        const ContextTier tier = effectiveTier(c.context);
        const uint64_t order   = activationOrder(c.context);

        const bool betterTier            = tier > bestTier;
        const bool sameTierBetterPrio    = (tier == bestTier) && (c.priority > bestPriority);
        const bool sameTierSamePrioNewer = (tier == bestTier) && (c.priority == bestPriority)
                                           && (order > bestOrder);
        if (betterTier || sameTierBetterPrio || sameTierSamePrioNewer) {
            bestIndex    = i;
            bestTier     = tier;
            bestPriority = c.priority;
            bestOrder    = order;
        }
    }
    return bestIndex;
}

bool ContextTracker::anyTierActive(const TierCounts& counts) noexcept
{
    return std::ranges::any_of(counts, [](int i) { return i > 0; });
}

} // namespace bakuon::gui
