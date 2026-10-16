#include "gui/b_contextsystem.h"

#include <algorithm>

namespace bakuon::gui {

using namespace context;
using core::Entity;
using core::entityOf;
using core::handleOf;
using core::NullEntity;

struct ContextSystem::Observers
{
    std::vector<std::pair<std::size_t, ContextObserver>> list;
    std::size_t nextId = 1;
};

ContextSystem::ContextSystem(core::Registry &registry)
    : core::RefResolver(registry)
    , m_registry(registry)
    , m_keys(registry)
    , m_observers(std::make_shared<Observers>())
{
}

ContextSystem::~ContextSystem() = default;

// ---------------------------------------------------------------------------
// 声明 / 查询
// ---------------------------------------------------------------------------

core::Result<ContextHandle> ContextSystem::declare(std::string_view id, std::string_view owner,
                                                   std::string_view description, int priority)
{
    if (const Entity existing = m_keys.find(id); m_registry.valid(existing)) {
        if (m_registry.get<ContextInfo>(existing).owner == owner) {
            return core::Ok(handleOf<ContextHandle>(existing)); // 同 owner 重复声明：幂等
        }
        return core::Fail<ContextHandle>(core::StatusCode::AlreadyExists,
                                         "context id naming collision: " + std::string(id));
    }

    // 数据组件先挂好，身份组件最后挂（见 ExtensionSystem::registerPoint 的同一约定）。
    const Entity entity = m_registry.create();
    m_registry.emplace<ContextInfo>(entity,
                                    ContextInfo{std::string(owner), std::string(description)});
    m_registry.emplace<ContextActivationRefs>(entity);
    m_registry.emplace<ContextArbitration>(entity,
                                           ContextArbitration{false,
                                                              core::ContextTier::Foreground,
                                                              priority,
                                                              0});
    m_registry.emplace<ContextProperties>(entity);

    if (auto assigned = m_keys.assign(entity, id); assigned.error()) {
        m_registry.destroy(entity);
        return core::Fail<ContextHandle>(assigned.status());
    }
    return core::Ok(handleOf<ContextHandle>(entity));
}

bool ContextSystem::undeclare(ContextHandle context)
{
    const auto ctx = resolve<ContextRef>(context);
    if (!ctx) {
        return false;
    }
    notify(ctx.entity(), ContextChange::Undeclaring); // 观察者此时仍能查询到它
    ctx.destroy();
    return true;
}

ContextHandle ContextSystem::find(std::string_view id) const
{
    return handleOf<ContextHandle>(m_keys.find(id));
}

std::optional<ContextState> ContextSystem::state(ContextHandle context) const
{
    const auto ctx = resolve<ConstContextRef>(context);
    if (!ctx) {
        return std::nullopt;
    }
    const auto &info = ctx.info();
    const auto &arb  = ctx.arbitration();

    ContextState result;
    result.id          = std::string(m_keys.tag(ctx.entity()));
    result.owner       = info.owner;
    result.description = info.description;
    result.priority    = arb.priority;
    result.active      = arb.active;
    result.tier        = arb.tier;
    result.order       = arb.order;
    return result;
}

std::vector<ContextHandle> ContextSystem::contexts() const
{
    std::vector<ContextHandle> result;
    for (const Entity entity : m_keys.entities()) {
        result.push_back(handleOf<ContextHandle>(entity));
    }
    return result;
}

std::vector<ContextHandle> ContextSystem::activeContexts() const
{
    std::vector<ContextHandle> result;
    for (const Entity entity : m_keys.entities()) {
        if (core::wrap<ConstContextRef>(std::as_const(m_registry), entity).active()) {
            result.push_back(handleOf<ContextHandle>(entity));
        }
    }
    return result;
}

bool ContextSystem::isActive(ContextHandle context) const
{
    const auto ctx = resolve<ConstContextRef>(context);
    return ctx && ctx.active();
}

// ---------------------------------------------------------------------------
// 激活
// ---------------------------------------------------------------------------

bool ContextSystem::push(ContextHandle context, const void *source, ContextTier tier)
{
    const auto ctx = resolve<ContextRef>(context);
    if (!ctx || source == nullptr) {
        return false;
    }
    auto &activation = ctx.activation();
    if (activation.retain(source, tier)) {
        // 从未激活 → 激活：分配本层单调序（与旧版 ContextArbiter 语义一致）。
        activation.setActivationOrder(m_clock.next(tier));
    }
    syncArbitration(ctx);
    return true;
}

bool ContextSystem::pop(ContextHandle context, const void *source, ContextTier tier)
{
    const auto ctx = resolve<ContextRef>(context);
    if (!ctx || source == nullptr) {
        return false;
    }
    ctx.activation().release(source, tier);
    syncArbitration(ctx);
    return true;
}

void ContextSystem::release(const void *source)
{
    if (source == nullptr) {
        return;
    }
    // 先快照实体列表再逐个处理：syncArbitration() 会同步触发观察者，观察者可能再改 registry。
    for (const Entity entity : m_keys.entities()) {
        const auto ctx = resolve<ContextRef>(entity);
        if (!ctx) {
            continue;
        }
        ctx.activation().releaseAll(source);
        syncArbitration(ctx);
    }
}

// ---------------------------------------------------------------------------
// 可修改的组件状态
// ---------------------------------------------------------------------------

bool ContextSystem::patch(ContextHandle context, const ContextPatch &patch)
{
    const auto ctx = resolve<ContextRef>(context);
    if (!ctx) {
        return false;
    }
    if (patch.description) {
        ctx.patch<ContextInfo>([&](ContextInfo &info) { info.description = *patch.description; });
        notify(ctx.entity(), ContextChange::Info);
    }
    if (patch.priority && *patch.priority != ctx.arbitration().priority) {
        ctx.patch<ContextArbitration>(
            [&](ContextArbitration &arb) { arb.priority = *patch.priority; });
        notify(ctx.entity(), ContextChange::Priority);
    }
    return true;
}

bool ContextSystem::setProperty(ContextHandle context, std::string_view key, ContextValue value)
{
    const auto ctx = resolve<ContextRef>(context);
    if (!ctx || key.empty()) {
        return false;
    }
    bool changed = false;
    ctx.patch<ContextProperties>([&](ContextProperties &props) {
        auto [it, inserted] = props.values.try_emplace(std::string(key), value);
        changed             = inserted || it->second != value;
        it->second          = std::move(value);
    });
    if (changed) {
        notify(ctx.entity(), ContextChange::Property);
    }
    return true;
}

bool ContextSystem::removeProperty(ContextHandle context, std::string_view key)
{
    const auto ctx = resolve<ContextRef>(context);
    if (!ctx) {
        return false;
    }
    bool removed = false;
    ctx.patch<ContextProperties>(
        [&](ContextProperties &props) { removed = props.values.erase(std::string(key)) > 0; });
    if (removed) {
        notify(ctx.entity(), ContextChange::Property);
    }
    return removed;
}

std::optional<ContextValue> ContextSystem::property(ContextHandle context,
                                                    std::string_view key) const
{
    const auto ctx = resolve<ConstContextRef>(context);
    if (!ctx) {
        return std::nullopt;
    }
    const auto &values = ctx.properties();
    const auto it      = values.find(std::string(key));
    return it != values.end() ? std::optional<ContextValue>(it->second) : std::nullopt;
}

core::Connection ContextSystem::onChanged(ContextObserver observer)
{
    const std::size_t id = m_observers->nextId++;
    m_observers->list.emplace_back(id, std::move(observer));
    // 只持 weak_ptr：Connection 比系统活得久时，断开是安全的空操作。
    return core::Connection([weak = std::weak_ptr<Observers>(m_observers), id] {
        if (const auto locked = weak.lock()) {
            auto &list = locked->list;
            list.erase(std::remove_if(list.begin(),
                                      list.end(),
                                      [id](const auto &entry) { return entry.first == id; }),
                       list.end());
        }
    });
}

// ---------------------------------------------------------------------------
// 私有辅助
// ---------------------------------------------------------------------------

void ContextSystem::syncArbitration(ContextRef context)
{
    const auto &activation  = context.activation();
    ContextArbitration next = context.arbitration();
    next.active             = activation.isActive();
    next.tier               = activation.effectiveTier();
    next.order              = activation.activationOrder();

    // 仅当仲裁相关快照真的变了才 patch + 通知（重复 retain 同一 (source, tier) 不会触发）。
    if (next == context.arbitration()) {
        return;
    }
    context.patch<ContextArbitration>([&](ContextArbitration &arb) { arb = next; });
    notify(context.entity(), ContextChange::Activation);
}

void ContextSystem::notify(Entity context, ContextChange change)
{
    // 拷贝快照：观察者里允许 disconnect / 新增订阅。
    const auto snapshot = m_observers->list;
    const auto handle   = handleOf<ContextHandle>(context);
    for (const auto &entry : snapshot) {
        if (entry.second) {
            entry.second(handle, change);
        }
    }
}

} // namespace bakuon::gui
