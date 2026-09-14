#include "gui/b_registrybridge.h"

#include <bakuon/core/Components.h>

namespace bakuon::gui {

RegistryBridge::RegistryBridge(core::Registry& registry, QObject* parent)
    : QObject(parent)
    , m_registry(registry)
{
}

RegistryBridge::~RegistryBridge() = default;

void RegistryBridge::watchSelection(const QString& tag)
{
    using bakuon::core::components::Selected;

    m_connections.push_back(m_registry.onConstruct<Selected>(
        [this, tag](core::Registry&, core::Handle id) {
            Q_EMIT entityConstructed(tag, id);
            Q_EMIT selectionChanged();
        }));
    m_connections.push_back(m_registry.onUpdate<Selected>(
        [this, tag](core::Registry&, core::Handle id) {
            Q_EMIT entityUpdated(tag, id);
            Q_EMIT selectionChanged();
        }));
    m_connections.push_back(m_registry.onDestroy<Selected>(
        [this, tag](core::Registry&, core::Handle id) {
            Q_EMIT entityDestroyed(tag, id);
            Q_EMIT selectionChanged();
        }));
}

} // namespace bakuon::gui
