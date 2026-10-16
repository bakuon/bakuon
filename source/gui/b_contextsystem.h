#pragma once

#include <memory>
#include <string>
#include <unordered_map>

#include <bakuon/core/ContextActivation.h>
#include <bakuon/core/Entity.h>
#include <bakuon/core/Tagger.h>
#include <bakuon/gui/IContextSystem.h>

#include "core/b_ref.h"

#include "gui/b_gui_export.h"

namespace bakuon::gui::context {

// ============================================================================
// 上下文实体的组件词汇表（普通结构体；本文件不含任何 Qt 类型——上下文仲裁是纯 C++ 逻辑）。
//
//   ContextKey            身份（BasicTagger 以它维护 id → 实体索引）
//   ContextInfo           owner / 描述
//   ContextActivationRefs 按 (source, tier) 的引用计数（core::ContextActivation）
//   ContextArbitration    仲裁快照：命令系统只读这一个组件就能比较"谁压过谁"
//   ContextProperties     插件发布的任意状态
// ============================================================================

struct ContextKey
{
    std::string value;
};

struct ContextInfo
{
    std::string owner;
    std::string description;
};

struct ContextActivationRefs
{
    core::ContextActivation activation;
};

struct ContextArbitration
{
    bool active{false};
    core::ContextTier tier{core::ContextTier::Foreground};
    int priority{0};
    std::uint64_t order{0};

    friend bool operator==(const ContextArbitration &, const ContextArbitration &) = default;
};

struct ContextProperties
{
    std::unordered_map<std::string, ContextValue> values;
};

// ---------------------------------------------------------------------------
// 句柄种类描述 + 带领域方法的内部引用（见 core/b_ref.h）。
// required 同时是"这个实体确实是上下文"的种类校验：命令句柄误传进来会被拒绝。
// ---------------------------------------------------------------------------
struct ContextTraits
{
    using tag      = ContextHandleTag;
    using required = entt::type_list<ContextArbitration>;
};

template<typename Reg>
class BasicContextRef : public core::RefBase<ContextTraits, Reg>
{
    using Base = core::RefBase<ContextTraits, Reg>;

public:
    using Base::Base;
    BasicContextRef() = default;

    [[nodiscard]] decltype(auto) arbitration() const
    {
        return this->template get<ContextArbitration>();
    }
    [[nodiscard]] decltype(auto) info() const { return this->template get<ContextInfo>(); }
    [[nodiscard]] decltype(auto) activation() const
    {
        return (this->template get<ContextActivationRefs>().activation);
    }
    [[nodiscard]] decltype(auto) properties() const
    {
        return (this->template get<ContextProperties>().values);
    }
    [[nodiscard]] bool active() const { return arbitration().active; }
};

using ContextRef      = BasicContextRef<core::Registry>;
using ConstContextRef = BasicContextRef<const core::Registry>;

} // namespace bakuon::gui::context

namespace bakuon::gui {

/**
 * @brief 上下文系统：ECS 里的一个 System——对"上下文实体"的声明、激活仲裁、状态读写。
 *
 * ## 归属与访问（没有单例）
 * 它是某个 Domain 的服务，由 Application/Workspace 创建：
 * @code
 *   domain.emplaceService<ContextSystem>(domain.registry());
 * @endcode
 * 命令系统（CommandSystem）持有它的引用，并通过 onChanged() 订阅激活/优先级变化，
 * 再据此重新仲裁命令——两个 System 之间只通过组件和这条观察者通道协作。
 *
 * ## 单一事实来源
 * 激活的真相是 ContextActivationRefs；ContextArbitration 只是它加上 priority 的"仲裁快照"，
 * 由本类在每次变更后同步，且仅当快照真的变了才 patch + 通知（等价于旧版的 before/after 比较）。
 *
 * ## 约束
 *  - 非线程安全（与 Registry/Domain 一致）；不可拷贝、不可移动。
 *  - 必须先于 Registry 销毁（Domain 的逆序销毁保证）。
 *  - 不要直接 registry.destroy() 上下文实体：请用 undeclare()，它会先通知观察者。
 */
class BAKUON_GUI_EXPORT ContextSystem final : public IContextSystem, private core::RefResolver
{
public:
    using IContextSystem::declare;
    using IContextSystem::pop;
    using IContextSystem::push;

    explicit ContextSystem(core::Registry &registry);
    ~ContextSystem() override;

    ContextSystem(const ContextSystem &)            = delete;
    ContextSystem &operator=(const ContextSystem &) = delete;
    ContextSystem(ContextSystem &&)                 = delete;
    ContextSystem &operator=(ContextSystem &&)      = delete;

    [[nodiscard]] core::Registry &registry() noexcept { return m_registry; }

    [[nodiscard]] core::Result<ContextHandle> declare(std::string_view id, std::string_view owner,
                                                      std::string_view description,
                                                      int priority) override;
    bool undeclare(ContextHandle context) override;
    [[nodiscard]] ContextHandle find(std::string_view id) const override;
    [[nodiscard]] std::optional<ContextState> state(ContextHandle context) const override;
    [[nodiscard]] std::vector<ContextHandle> contexts() const override;
    [[nodiscard]] std::vector<ContextHandle> activeContexts() const override;
    [[nodiscard]] bool isActive(ContextHandle context) const override;

    bool push(ContextHandle context, const void *source, ContextTier tier) override;
    bool pop(ContextHandle context, const void *source, ContextTier tier) override;
    void release(const void *source) override;

    bool patch(ContextHandle context, const ContextPatch &patch) override;
    bool setProperty(ContextHandle context, std::string_view key, ContextValue value) override;
    bool removeProperty(ContextHandle context, std::string_view key) override;
    [[nodiscard]] std::optional<ContextValue> property(ContextHandle context,
                                                       std::string_view key) const override;

    [[nodiscard]] core::Connection onChanged(ContextObserver observer) override;

private:
    struct Observers;

    void syncArbitration(context::ContextRef context);
    void notify(core::Entity context, ContextChange change);

private:
    core::Registry &m_registry;
    core::BasicTagger<context::ContextKey> m_keys;
    core::ActivationClock m_clock;
    std::shared_ptr<Observers> m_observers;
};

} // namespace bakuon::gui
