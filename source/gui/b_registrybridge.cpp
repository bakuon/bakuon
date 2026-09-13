#include "gui/b_registrybridge.h"

namespace bakuon::gui {

RegistryBridge::RegistryBridge(core::Registry& registry, QObject* parent)
    : QObject(parent)
    , m_registry(registry)
{
}

RegistryBridge::~RegistryBridge() = default;

} // namespace bakuon::gui
