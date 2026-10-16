#include "core/b_extension.h"

#include <algorithm>

namespace bakuon::core {

using namespace extension;

namespace {

/// a 是否应排在 b 之前：priority 降序，同优先级按注册序升序。
[[nodiscard]] bool comesBefore(const ExtensionPriority &a, const ExtensionPriority &b) noexcept
{
    return a.value != b.value ? a.value > b.value : a.order < b.order;
}

} // namespace

// ---------------------------------------------------------------------------
// 扩展点
// ---------------------------------------------------------------------------

ExtensionPoint::ExtensionPoint(Registry &registry)
    : m_registry(registry)
    , m_points(registry)
{
    m_extensionDestroyed = m_registry.on_destroy<ExtensionOf>()
                               .connect<&ExtensionPoint::extensionDestroyed>(*this);
}

ExtensionPoint::~ExtensionPoint() = default;

Result<Entity> ExtensionPoint::create(std::string_view id, std::string_view description,
                                      std::uint64_t type)
{
    // 先挂好全部数据组件，最后才挂身份组件（Tagger::assign）：监听身份组件构造的观察者
    // 看到的是一个完整的扩展点。assign 失败（空 id / 重名）时整个回滚，不留游离实体。
    const Entity entity = m_registry.create();
    m_registry.emplace<ExtensionPointInfo>(entity,
                                           ExtensionPointInfo{std::string(description), type});
    m_registry.emplace<ExtensionList>(entity);

    if (auto assigned = m_points.assign(entity, id); assigned.error()) {
        m_registry.destroy(entity);
        return Fail<Entity>(assigned.status());
    }
    return Ok(entity);
}

Result<Entity> ExtensionPoint::ensure(std::string_view id, std::string_view description,
                                      std::uint64_t type)
{
    if (const Entity existing = m_points.find(id); m_registry.valid(existing)) {
        if (m_registry.get<ExtensionPointInfo>(existing).type != type) {
            return Fail<Entity>(StatusCode::FailedPrecondition,
                                "extension point exists with a different interface "
                                "type: "
                                    + std::string(id));
        }
        return Ok(existing);
    }
    return create(id, description, type);
}

bool ExtensionPoint::erase(std::string_view id)
{
    const Entity point = m_points.find(id);
    if (!m_registry.valid(point)) {
        return false;
    }
    // 拷贝一份再逐个销毁：extensionDestroyed() 会同步修改原列表。
    const std::vector<Entity> owned = m_registry.get<ExtensionList>(point).ordered;
    for (const Entity extensionEntity : owned) {
        if (m_registry.valid(extensionEntity)) {
            m_registry.destroy(extensionEntity);
        }
    }
    return m_points.destroy(id);
}

bool ExtensionPoint::contains(std::string_view id) const
{
    return m_points.contains(id);
}

Entity ExtensionPoint::find(std::string_view id) const
{
    return m_points.find(id);
}

std::string ExtensionPoint::id(Entity point) const
{
    return std::string(m_points.tag(point));
}

std::uint64_t ExtensionPoint::type(Entity point) const
{
    if (!m_registry.valid(point)) {
        return 0;
    }
    const auto *info = m_registry.try_get<ExtensionPointInfo>(point);
    return info ? info->type : 0;
}

std::vector<std::string> ExtensionPoint::list() const
{
    return m_points.tags();
}

void ExtensionPoint::extensionDestroyed(Registry &registry, Entity extension)
{
    // on_destroy 触发时组件仍在。
    // registry.clear() 时扩展点可能已先被销毁，需判空。
    const Entity point = registry.get<ExtensionOf>(extension).point;
    if (registry.valid(point)) {
        if (auto *list = registry.try_get<ExtensionList>(point)) {
            std::erase(list->ordered, extension);
        }
    }
}

// ---------------------------------------------------------------------------
// 扩展
// ---------------------------------------------------------------------------

Extension::Extension(Registry &registry)
    : m_registry(registry)
{
}

Result<Entity> Extension::add(Entity point, std::shared_ptr<void> object, int priority)
{
    if (!object) {
        return Fail<Entity>(StatusCode::InvalidArgument, "extension object is null");
    }

    const ExtensionList *list = listOf(point);
    if (!list) {
        return Fail<Entity>(StatusCode::NotFound, "invalid extension point");
    }
    for (const Entity existing : list->ordered) {
        const auto *instance = m_registry.try_get<ExtensionInstance>(existing);
        if (instance && instance->object == object) {
            return Fail<Entity>(StatusCode::AlreadyExists,
                                "extension already registered on this point");
        }
    }

    auto created = createExtensionEntity(point, priority);
    if (created.error()) {
        return Fail<Entity>(created.status());
    }
    const Entity entity = created.value();
    m_registry.emplace<ExtensionInstance>(entity, ExtensionInstance{std::move(object)});
    insertSorted(point, entity);
    return Ok(entity);
}

Result<Entity> Extension::addFactory(Entity point, std::function<std::shared_ptr<void>()> factory,
                                     int priority, bool singleton)
{
    if (!factory) {
        return Fail<Entity>(StatusCode::InvalidArgument, "extension factory is empty");
    }
    auto created = createExtensionEntity(point, priority);
    if (created.error()) {
        return Fail<Entity>(created.status());
    }
    const Entity entity = created.value();
    m_registry.emplace<ExtensionFactory>(entity, ExtensionFactory{std::move(factory), singleton});
    insertSorted(point, entity);
    return Ok(entity);
}

bool Extension::remove(Entity extension)
{
    if (!m_registry.valid(extension) || !m_registry.all_of<ExtensionOf>(extension)) {
        return false;
    }
    m_registry.destroy(extension); // 监听器负责从扩展点列表摘除
    return true;
}

bool Extension::setPriority(Entity extension, int priority)
{
    if (!m_registry.valid(extension)
        || !m_registry.all_of<ExtensionOf, ExtensionPriority>(extension)) {
        return false;
    }
    const Entity point = m_registry.get<ExtensionOf>(extension).point;
    if (ExtensionList *list = listOf(point)) {
        std::erase(list->ordered, extension);
    }
    m_registry.patch<ExtensionPriority>(extension, [priority](ExtensionPriority &p) {
        p.value = priority; // order 保持不变：同优先级内仍按原注册序
    });
    insertSorted(point, extension);
    return true;
}

std::size_t Extension::count(Entity point) const
{
    const ExtensionList *list = listOf(point);
    return list ? list->ordered.size() : 0;
}

std::vector<Entity> Extension::list(Entity point) const
{
    std::vector<Entity> result;
    if (const ExtensionList *list = listOf(point)) {
        return list->ordered;
    }
    return result;
}

std::shared_ptr<void> Extension::resolve(Entity extension)
{
    if (!m_registry.valid(extension)) {
        return nullptr;
    }
    if (const auto *instance = m_registry.try_get<ExtensionInstance>(extension)) {
        return instance->object;
    }
    const auto *factory = m_registry.try_get<ExtensionFactory>(extension);
    if (!factory || !factory->make) {
        return nullptr;
    }

    // 先把工厂拷出来再调用：工厂体内可能注册/注销扩展，使指向组件池的指针失效。
    const auto make              = factory->make;
    const auto singleton         = factory->singleton;
    std::shared_ptr<void> object = make();
    if (object && singleton && m_registry.valid(extension)) {
        m_registry.emplace_or_replace<ExtensionInstance>(extension, ExtensionInstance{object});
    }
    return object;
}

ExtensionList *Extension::listOf(Entity point) const
{
    return m_registry.valid(point) ? m_registry.try_get<ExtensionList>(point) : nullptr;
}

Result<Entity> Extension::createExtensionEntity(Entity point, int priority)
{
    if (!listOf(point)) {
        return Fail<Entity>(StatusCode::NotFound, "invalid extension point");
    }
    const Entity entity = m_registry.create();
    m_registry.emplace<ExtensionOf>(entity, ExtensionOf{point});
    m_registry.emplace<ExtensionPriority>(entity, ExtensionPriority{priority, m_nextOrder++});
    return Ok(entity);
}

void Extension::insertSorted(Entity point, Entity extension)
{
    auto &ordered = m_registry.get<ExtensionList>(point).ordered;
    const auto position
        = std::upper_bound(ordered.begin(),
                           ordered.end(),
                           extension,
                           [this](Entity value, Entity element) {
                               return comesBefore(m_registry.get<ExtensionPriority>(value),
                                                  m_registry.get<ExtensionPriority>(element));
                           });
    ordered.insert(position, extension);
}

} // namespace bakuon::core
