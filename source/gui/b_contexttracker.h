#pragma once

#include <unordered_set>

#include <QtCore/QObject>

#include "gui/b_types.h"

namespace bakuon::gui {

/**
 * @brief 上下文激活仲裁/协调 Coordinator
 */
class ContextTracker : public QObject
{
    Q_OBJECT
public:
    explicit ContextTracker(QObject* parent = nullptr);
    ~ContextTracker() override = default;

    // 一个上下文 id 的元信息：由谁登记、用于人类阅读的描述。
    struct ContextInfo
    {
        QString owner; // 登记者标识，通常是模块/插件的唯一 id，如 "core"、"plugin.image"
        QString description;
    };

    /**
     * 显式登记一个上下文 id。这是解决"上下文本质是字符串、散落各处容易重复/
     * 拼写不一致"问题的第一道防线：约定业务代码不允许凭空 new 一个 ContextId
     * 字面量，必须先在某处（通常是模块自己的一个 XxxContexts.h）调用一次
     * registerContext()/CommandSystem::declareContext()，取得的 ContextId
     * 常量再分发给模块内部各处使用。
     *
     * 冲突判定规则：
     *   - id 尚未登记                 -> 正常登记，返回 true
     *   - id 已登记，owner 相同        -> 视为幂等更新（如刷新 description），返回 true
     *   - id 已登记，owner 不同        -> 真实的命名冲突（两个不相关模块争用了
     *                                     同一个字符串），拒绝注册，qWarning 报出
     *                                     双方 owner 以便定位，返回 false
     *
     * 未登记的 id 在 pushContext() 时只会 qWarning 提示、不阻断运行——保持行为
     * 宽松，是为了不因为漏注册就让整个功能不可用，但足以在开发期第一时间暴露问题。
     */
    bool registerContext(const ContextId& id, const QString& owner, const QString& description);

    // 查询某个上下文 id 的登记信息；从未登记过则返回 std::nullopt。
    std::optional<ContextInfo> contextInfo(const ContextId& id) const;

    // 枚举当前进程内全部已登记的上下文 id，用于生成"系统全部上下文"清单/调试面板
    // （类似 VSCode 的 "Inspect Context Keys"）。
    std::vector<ContextId> registeredContexts() const;

    /** 维护当前"激活上下文集合"（而非简单的单一上下文/栈）。 */
    void pushContext(const ContextId& context, const void* source,
                     ContextTier tier = ContextTier::Foreground);
    void popContext(const ContextId& context, const void* source,
                    ContextTier tier = ContextTier::Foreground);

    // 某个 source 生命周期结束时（如控件析构），一次性释放其持有的全部上下文引用
    void releaseContext(const void* source);

    // 当前活动的上下文
    std::unordered_set<ContextId> activeContexts() const;
    bool isActiveContext(const ContextId& context) const noexcept;

    // 该上下文当前的"生效层级"：所有仍持有引用的来源里，层级最高的那个。
    ContextTier effectiveTier(const ContextId& context) const noexcept;

    // 最近一次被 Foreground 层级 push 时记录下的全局递增序号；
    // 从未被 Foreground 激活过，或当前未激活，则返回 0。
    uint64_t activationOrder(const ContextId& context) const noexcept;

signals:
    // 激活集合发生变化（有上下文从"无引用"变为"有引用"，或反之）时发出
    void contextChanged();

private:
    struct RefKey
    {
        ContextId context;
        const void* source                   = nullptr;
        ContextTier tier                     = ContextTier::Foreground;
        bool operator==(const RefKey&) const = default;
    };

    struct RefKeyHash
    {
        size_t operator()(const RefKey& key) const noexcept
        {
            size_t seed = std::hash<ContextId>{}(key.context);
            seed ^= std::hash<const void*>{}(key.source) + 0x9e3779b97f4a7c15ULL + (seed << 6)
                    + (seed >> 2);
            seed ^= std::hash<int>{}(static_cast<int>(key.tier)) + 0x9e3779b97f4a7c15ULL
                    + (seed << 6) + (seed >> 2);
            return seed;
        }
    };

    static constexpr size_t TierCount = static_cast<size_t>(ContextTier::TierCount);
    using TierCounts                  = std::array<int, TierCount>;
    static bool anyTierActive(const TierCounts& counts) noexcept;

    // 已登记的上下文 id -> 元信息（描述、owner），见 registerContext()
    std::unordered_map<ContextId, ContextInfo> m_contextRegistry;

    // 每个 (context, source, tier) 组合各自的引用计数
    std::unordered_map<RefKey, int, RefKeyHash> m_refCounts;
    // 每个 context 各 tier 当前的总引用数
    std::unordered_map<ContextId, TierCounts> m_contextTierCounts;
    // 每个 context 最近一次由"无引用"变为"有引用"时记录下的全局递增序号
    std::unordered_map<ContextId, uint64_t> m_activationOrder;
    uint64_t m_activationClock = 0;
};

} // namespace bakuon::gui
