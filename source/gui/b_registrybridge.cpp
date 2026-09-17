#include "gui/b_registrybridge.h"

#include <bakuon/core/Components.h>

namespace bakuon::gui {

RegistryBridge::RegistryBridge(core::Container& container, QObject* parent)
    : QObject(parent)
    , m_container(container)
{
}

RegistryBridge::~RegistryBridge() = default;

void RegistryBridge::watchSelection(const QString& tag)
{
    using bakuon::core::components::Selected;

    m_connections.push_back(
        m_container.onConstruct<Selected>([this, tag](core::Container&, core::Entity id) {
            Q_EMIT entityConstructed(tag, id);
            Q_EMIT selectionChanged();
        }));
    m_connections.push_back(
        m_container.onUpdate<Selected>([this, tag](core::Container&, core::Entity id) {
            Q_EMIT entityUpdated(tag, id);
            Q_EMIT selectionChanged();
        }));
    m_connections.push_back(
        m_container.onDestroy<Selected>([this, tag](core::Container&, core::Entity id) {
            Q_EMIT entityDestroyed(tag, id);
            Q_EMIT selectionChanged();
        }));
}

} // namespace bakuon::gui
