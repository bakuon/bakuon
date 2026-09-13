#pragma once

/**
 * @file b_contextactivation.h
 * @brief 上下文层级、激活引用计数、仲裁序 —— 纯标准 C++，零 Qt。
 *
 * 从 gui::ContextState / ContextArbiter 中抽出与 QAction / QObject 无关的核心：
 *  - ContextTier：Foreground 无条件压过 Background；
 *  - ContextActivation：按 (source, tier) 引用计数，维护 isActive / effectiveTier /
 *    activationOrder；
 *  - ContextArbitrationKey + beats()：tier > priority > order 的纯比较；
 *  - ActivationClock：Foreground / Background 两套独立单调时钟。
 *
 * gui 层 ContextState 内嵌 ContextActivation，ContextArbiter 用 ActivationClock
 * 分配序；命令路由仍留在 gui（依赖 QAction）。
 */

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <utility>
#include <vector>

namespace bakuon::core {

enum class ContextTier : std::uint8_t {
    Background = 0,
    Foreground = 1,
    TierCount
};

class ContextActivation
{
public:
    static constexpr std::size_t kTierCount = static_cast<std::size_t>(ContextTier::TierCount);
    using TierCounts = std::array<int, kTierCount>;

    [[nodiscard]] bool isActive() const noexcept { return anyTierActive(m_tierCounts); }

    [[nodiscard]] ContextTier effectiveTier() const noexcept
    {
        for (int t = static_cast<int>(kTierCount) - 1; t >= 0; --t) {
            if (m_tierCounts[static_cast<std::size_t>(t)] > 0) {
                return static_cast<ContextTier>(t);
            }
        }
        return ContextTier::Foreground;
    }

    [[nodiscard]] std::uint64_t activationOrder() const noexcept { return m_activationOrder; }

    void setActivationOrder(std::uint64_t order) noexcept { m_activationOrder = order; }

    bool retain(const void* source, ContextTier tier)
    {
        if (!source) {
            return false;
        }

        const RefKey key{source, tier};
        int& refCount = m_refCounts[key];
        ++refCount;
        if (refCount > 1) {
            return false;
        }

        const bool wasInactive = !anyTierActive(m_tierCounts);
        ++m_tierCounts[static_cast<std::size_t>(tier)];
        return wasInactive;
    }

    bool release(const void* source, ContextTier tier)
    {
        if (!source) {
            return false;
        }

        const RefKey key{source, tier};
        auto it = m_refCounts.find(key);
        if (it == m_refCounts.end() || it->second <= 0) {
            return false;
        }
        if (--(it->second) > 0) {
            return false;
        }
        m_refCounts.erase(it);
        --m_tierCounts[static_cast<std::size_t>(tier)];
        return !anyTierActive(m_tierCounts);
    }

    void releaseAll(const void* source)
    {
        if (!source) {
            return;
        }
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

    [[nodiscard]] const TierCounts& tierCounts() const noexcept { return m_tierCounts; }

private:
    struct RefKey
    {
        const void* source = nullptr;
        ContextTier tier   = ContextTier::Foreground;

        bool operator==(const RefKey&) const = default;
    };

    struct RefKeyHash
    {
        std::size_t operator()(const RefKey& key) const noexcept
        {
            std::size_t seed = std::hash<const void*>{}(key.source);
            seed ^= std::hash<int>{}(static_cast<int>(key.tier)) + 0x9e3779b97f4a7c15ULL
                    + (seed << 6) + (seed >> 2);
            return seed;
        }
    };

    static bool anyTierActive(const TierCounts& counts) noexcept
    {
        return std::any_of(counts.begin(), counts.end(), [](int c) { return c > 0; });
    }

    std::uint64_t m_activationOrder = 0;
    std::unordered_map<RefKey, int, RefKeyHash> m_refCounts;
    TierCounts m_tierCounts{};
};

struct ContextArbitrationKey
{
    ContextTier tier    = ContextTier::Foreground;
    int priority        = 0;
    std::uint64_t order = 0;
};

[[nodiscard]] inline constexpr bool beats(const ContextArbitrationKey& candidate,
                                          const ContextArbitrationKey& incumbent,
                                          bool incumbentValid) noexcept
{
    if (!incumbentValid) {
        return true;
    }
    if (candidate.tier != incumbent.tier) {
        return candidate.tier > incumbent.tier;
    }
    if (candidate.priority != incumbent.priority) {
        return candidate.priority > incumbent.priority;
    }
    return candidate.order > incumbent.order;
}

class ActivationClock
{
public:
    [[nodiscard]] std::uint64_t next(ContextTier tier)
    {
        return (tier == ContextTier::Background) ? ++m_background : ++m_foreground;
    }

    [[nodiscard]] std::uint64_t foreground() const noexcept { return m_foreground; }
    [[nodiscard]] std::uint64_t background() const noexcept { return m_background; }

private:
    std::uint64_t m_foreground = 0;
    std::uint64_t m_background = 0;
};

} // namespace bakuon::core
