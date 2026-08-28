#include "gui/b_contextstatus.h"

#include <algorithm>

#include <QtGui/QAction>

namespace bakuon::gui {

ContextState::ContextState(const ContextId& id, int priority, QObject* parent)
    : QObject(parent)
    , m_id(id)
    , m_priority(priority)
{
}

void ContextState::addAction(const CommandId& cmdId, QAction* action)
{
    if (!action) {
        return;
    }

    if (auto it = m_actions.find(cmdId); it != m_actions.end() && it->second) {
        QObject::disconnect(it->second, &QObject::destroyed, this, nullptr);
    }

    m_actions[cmdId] = action;
    // action 被外部销毁时自动摘除并广播——避免悬空 QAction* 参与后续仲裁。
    // 用 action 自身作为 connect 的 context 对象：destroyed() 是在 ~QObject() 内部发出的，
    // 此时 action 尚未完全析构完毕，槽函数仍能安全执行；用它自己当 context 只是为了让
    // Qt 在正常情况下也能正确管理这条连接的生命周期，不依赖 ContextState 是否先被销毁。
    connect(action, &QObject::destroyed, this, [this, cmdId]() {
        m_actions.erase(cmdId);
        Q_EMIT actionsChanged();
    });

    Q_EMIT actionsChanged();
}

void ContextState::removeAction(const CommandId& cmdId)
{
    auto it = m_actions.find(cmdId);
    if (it == m_actions.end()) {
        return;
    }
    if (it->second) {
        QObject::disconnect(it->second, &QObject::destroyed, this, nullptr);
    }
    m_actions.erase(it);
    Q_EMIT actionsChanged();
}

QAction* ContextState::action(const CommandId& cmdId) const
{
    auto it = m_actions.find(cmdId);
    return it != m_actions.end() ? it->second.data() : nullptr;
}

bool ContextState::hasAction(const CommandId& cmdId) const noexcept
{
    auto it = m_actions.find(cmdId);
    return it != m_actions.end() && !it->second.isNull();
}

std::vector<CommandId> ContextState::commandIds() const
{
    std::vector<CommandId> result;
    result.reserve(m_actions.size());
    for (const auto& [cmdId, action] : m_actions) {
        if (!action.isNull()) {
            result.push_back(cmdId);
        }
    }
    return result;
}

bool ContextState::retain(const void* source, ContextTier tier)
{
    Q_ASSERT(source != nullptr);

    const RefKey key{source, tier};
    int& refCount = m_refCounts[key];
    ++refCount;
    if (refCount > 1) {
        return false; // 同一 (source, tier) 重复 retain，整体激活状态未发生变化
    }

    const bool wasInactive = !anyTierActive(m_tierCounts);
    ++m_tierCounts[static_cast<size_t>(tier)];
    return wasInactive; // true 表示整体从未激活变为激活，调用方需要分配新的 activationOrder
}

bool ContextState::release(const void* source, ContextTier tier)
{
    Q_ASSERT(source != nullptr);

    const RefKey key{source, tier};
    auto it = m_refCounts.find(key);
    if (it == m_refCounts.end() || it->second <= 0) {
        return false; // 未持有该 (source, tier) 引用，忽略非法/多余的 release 调用
    }
    if (--(it->second) > 0) {
        return false; // 该 source 在这个 tier 上仍持有引用
    }
    m_refCounts.erase(it);
    --m_tierCounts[static_cast<size_t>(tier)];

    return !anyTierActive(m_tierCounts); // true 表示整体从激活变为未激活
}

void ContextState::releaseAll(const void* source)
{
    if (!source) {
        return;
    }
    // 收集该 source 持有的全部 (tier, count)，再一次性扣减，避免在遍历 m_refCounts 时修改容器。
    std::vector<std::pair<ContextTier, int>> held;
    for (const auto& [key, count] : m_refCounts) {
        if (key.source == source && count > 0) {
            held.emplace_back(key.tier, count);
        }
    }
    for (const auto& [tier, count] : held) {
        for (int i = 0; i < count; ++i) {
            release(source, tier);
        }
    }
}

bool ContextState::isActive() const noexcept
{
    return anyTierActive(m_tierCounts);
}

ContextTier ContextState::effectiveTier() const noexcept
{
    for (int t = static_cast<int>(kTierCount) - 1; t >= 0; --t) {
        if (m_tierCounts[static_cast<size_t>(t)] > 0) {
            return static_cast<ContextTier>(t);
        }
    }
    return ContextTier::Foreground; // 未激活时的安全默认值，调用方应先用 isActive() 判断
}

bool ContextState::anyTierActive(const TierCounts& counts) noexcept
{
    return std::any_of(counts.begin(), counts.end(), [](int i) { return i > 0; });
}

} // namespace bakuon::gui
