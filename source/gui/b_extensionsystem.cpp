#include "gui/b_extensionsystem.h"

namespace bakuon::gui {

ExtensionSystem::ExtensionSystem(core::Registry &registry)
    : m_points(std::make_unique<core::ExtensionPoint>(registry))
    , m_extensions(std::make_unique<core::Extension>(registry))
{
}

ExtensionSystem::~ExtensionSystem() = default;

ExtensionPointHandle ExtensionSystem::registerPoint(std::string_view id,
                                                    std::string_view description,
                                                    std::uint64_t type)
{
    auto result = m_points->create(id, description, type);
    // TODO: report error result
    // ...
    return core::handleOf<ExtensionPointHandle>(result.value());
}

ExtensionPointHandle ExtensionSystem::ensurePoint(std::string_view id, std::string_view description,
                                                  std::uint64_t type)
{
    auto result = m_points->ensure(id, description, type);
    // TODO: report error result
    // ...
    return core::handleOf<ExtensionPointHandle>(result.value());
}

bool ExtensionSystem::unregisterPoint(std::string_view id)
{
    return m_points->erase(id);
}

bool ExtensionSystem::hasPoint(std::string_view id) const
{
    return m_points->contains(id);
}

ExtensionPointHandle ExtensionSystem::findPoint(std::string_view id) const
{
    return core::handleOf<ExtensionPointHandle>(m_points->find(id));
}

std::string ExtensionSystem::pointId(ExtensionPointHandle point) const
{
    return m_points->id(core::entityOf(point));
}

std::uint64_t ExtensionSystem::pointType(ExtensionPointHandle point) const
{
    return m_points->type(core::entityOf(point));
}

std::vector<std::string> ExtensionSystem::pointIds() const
{
    return m_points->list();
}

ExtensionHandle ExtensionSystem::add(ExtensionPointHandle point, std::shared_ptr<void> object,
                                     int priority)
{
    auto result = m_extensions->add(core::entityOf(point), object, priority);
    // TODO: report error result
    return core::handleOf<ExtensionHandle>(result.value());
}

ExtensionHandle ExtensionSystem::add(ExtensionPointHandle point,
                                     std::function<std::shared_ptr<void>()> factory, int priority,
                                     bool singleton)
{
    auto result = m_extensions->addFactory(core::entityOf(point), factory, priority, singleton);
    // TODO: report error result
    return core::handleOf<ExtensionHandle>(result.value());
}

bool ExtensionSystem::remove(ExtensionHandle extension)
{
    return m_extensions->remove(core::entityOf(extension));
}

bool ExtensionSystem::setPriority(ExtensionHandle extension, int priority)
{
    return m_extensions->setPriority(core::entityOf(extension), priority);
}

std::size_t ExtensionSystem::count(ExtensionPointHandle point) const
{
    return m_extensions->count(core::entityOf(point));
}

std::vector<ExtensionHandle> ExtensionSystem::list(ExtensionPointHandle point) const
{
    auto items = m_extensions->list(core::entityOf(point));
    std::vector<ExtensionHandle> result;
    result.reserve(items.size());
    for (auto entity : items) {
        result.push_back(core::handleOf<ExtensionHandle>(entity));
    }
    return result;
}

std::shared_ptr<void> ExtensionSystem::resolve(ExtensionHandle extension)
{
    return m_extensions->resolve(core::entityOf(extension));
}

} // namespace bakuon::gui
