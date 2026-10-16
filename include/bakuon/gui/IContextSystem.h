#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include <bakuon/core/Connection.h>
#include <bakuon/core/ContextActivation.h>
#include <bakuon/core/Handle.h>
#include <bakuon/core/Result.h>

#include "gui/b_gui_export.h"

// ============================================================================
// 上下文系统的"插件侧"稳定门面：纯接口，不依赖 Qt、不依赖 entt。
//
// 上下文 = registry 里的一个实体；"哪些上下文处于激活态、谁压过谁"是它的组件状态：
//   ContextKey（id）/ ContextInfo（owner、描述）/ ContextActivationRefs（激活引用计数）/
//   ContextArbitration（仲裁快照：active、tier、priority、order）/ ContextProperties（属性包）
// 插件通过本接口声明上下文、推入/弹出激活、读写其中允许修改的组件状态。
// 没有全局单例：IContextSystem 由 Workspace/Application 的某个领域（Domain）提供。
// ============================================================================

namespace bakuon::gui {

using ContextTier = core::ContextTier;

struct ContextHandleTag;
using ContextHandle = core::BasicHandle<ContextHandleTag>;

/// 上下文属性包的取值类型（插件用来发布"当前有几个选中项/文档是否已修改"之类的状态）。
using ContextValue = std::variant<bool, std::int64_t, double, std::string>;

/// 一个上下文当前状态的值拷贝快照。
struct ContextState
{
    std::string id;
    std::string owner;
    std::string description;
    int priority{0};
    bool active{false};
    ContextTier tier{ContextTier::Foreground};
    std::uint64_t order{0}; ///< 激活序（单调递增，越大越新）；未激活时保留上一次的值
};

/// 允许插件修改的上下文组件字段（patch 语义：只有设置了的字段才会被修改）。
struct ContextPatch
{
    std::optional<std::string> description;
    std::optional<int> priority;
};

/// 通知观察者"哪一类组件状态变了"。
enum class ContextChange {
    Activation, ///< active / tier / order 变化
    Priority,
    Info,        ///< 描述变化
    Property,    ///< 属性包变化
    Undeclaring, ///< 即将销毁（此时仍可查询）
};
using ContextObserver = std::function<void(ContextHandle, ContextChange)>;

class BAKUON_GUI_EXPORT IContextSystem
{
public:
    virtual ~IContextSystem() = default;

    // ---- 声明 / 查询 ----

    /// 声明上下文。id 为空 → InvalidArgument；id 已被**另一个 owner** 声明 → AlreadyExists；
    /// 同一个 owner 重复声明是幂等的，返回既有句柄。
    [[nodiscard]] virtual core::Result<ContextHandle> declare(std::string_view id,
                                                              std::string_view owner,
                                                              std::string_view description,
                                                              int priority) = 0;
    [[nodiscard]] core::Result<ContextHandle> declare(std::string_view id, std::string_view owner,
                                                      std::string_view description)
    {
        return declare(id, owner, description, 0);
    }
    /// 销毁上下文（先通知 Undeclaring，依赖它的命令绑定会被一并清理）。
    virtual bool undeclare(ContextHandle context) = 0;

    [[nodiscard]] virtual ContextHandle find(std::string_view id) const                  = 0;
    [[nodiscard]] virtual std::optional<ContextState> state(ContextHandle context) const = 0;
    [[nodiscard]] virtual std::vector<ContextHandle> contexts() const                    = 0;
    [[nodiscard]] virtual std::vector<ContextHandle> activeContexts() const              = 0;
    [[nodiscard]] virtual bool isActive(ContextHandle context) const                     = 0;

    // ---- 激活（按 (source, tier) 引用计数）----

    /// source 通常传 this；同一个 source 对同一上下文的重复 push 需要配对的 pop。
    /// @return 句柄有效且 source 非空时为 true。
    virtual bool push(ContextHandle context, const void *source, ContextTier tier) = 0;
    virtual bool pop(ContextHandle context, const void *source, ContextTier tier)  = 0;
    bool push(ContextHandle context, const void *source)
    {
        return push(context, source, ContextTier::Foreground);
    }
    bool pop(ContextHandle context, const void *source)
    {
        return pop(context, source, ContextTier::Foreground);
    }
    /// 释放 source 持有的全部激活引用（插件 shutdown()/窗口销毁时兜底用）。
    virtual void release(const void *source) = 0;

    // ---- 可修改的组件状态 ----

    virtual bool patch(ContextHandle context, const ContextPatch &patch)                      = 0;
    virtual bool setProperty(ContextHandle context, std::string_view key, ContextValue value) = 0;
    virtual bool removeProperty(ContextHandle context, std::string_view key)                  = 0;
    [[nodiscard]] virtual std::optional<ContextValue> property(ContextHandle context,
                                                               std::string_view key) const    = 0;

    /// 订阅状态变化。返回的 Connection 析构即取消订阅，可以比系统活得更久。
    [[nodiscard]] virtual core::Connection onChanged(ContextObserver observer) = 0;
};

/// RAII 激活守卫：构造 push，析构 pop（ContextGuard 的接口版）。
class ScopedContext
{
public:
    ScopedContext() = default;
    ScopedContext(IContextSystem &system, ContextHandle context, const void *source,
                  ContextTier tier = ContextTier::Foreground)
        : m_system(&system)
        , m_context(context)
        , m_source(source)
        , m_tier(tier)
    {
        if (!m_system->push(m_context, m_source, m_tier)) {
            m_system = nullptr; // 推入失败：析构时不要 pop
        }
    }
    ~ScopedContext() { reset(); }

    ScopedContext(const ScopedContext &)            = delete;
    ScopedContext &operator=(const ScopedContext &) = delete;
    ScopedContext(ScopedContext &&other) noexcept
        : m_system(std::exchange(other.m_system, nullptr))
        , m_context(other.m_context)
        , m_source(other.m_source)
        , m_tier(other.m_tier)
    {
    }
    ScopedContext &operator=(ScopedContext &&other) noexcept
    {
        if (this != &other) {
            reset();
            m_system  = std::exchange(other.m_system, nullptr);
            m_context = other.m_context;
            m_source  = other.m_source;
            m_tier    = other.m_tier;
        }
        return *this;
    }

    void reset() noexcept
    {
        if (m_system) {
            m_system->pop(m_context, m_source, m_tier);
            m_system = nullptr;
        }
    }
    [[nodiscard]] bool isActive() const noexcept { return m_system != nullptr; }

private:
    IContextSystem *m_system{nullptr};
    ContextHandle m_context{};
    const void *m_source{nullptr};
    ContextTier m_tier{ContextTier::Foreground};
};

} // namespace bakuon::gui
