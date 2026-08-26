#include "gui/b_extensionsystem.h"

// std::unique_lock / std::shared_lock —— <shared_mutex> 只保证 std::shared_mutex
// 本身可用，不保证连带带进 std::unique_lock；MSVC 的实现凑巧传递包含了，
// GCC/Clang 下必须显式 include，否则编译不过。
#include <mutex>

namespace bakuon::gui {

bool ExtensionSystem::registerExtensionPoint(std::shared_ptr<ExtensionPointBase> point)
{
    if (!point)
        return false;

    std::unique_lock<std::shared_mutex> lock(m_mutex);
    const std::string id = point->id();
    if (id.empty() || m_extensionPoints.contains(id)) {
        return false;
    }
    m_extensionPoints.emplace(id, std::move(point));
    return true;
}

bool ExtensionSystem::unregisterExtensionPoint(const std::string& id)
{
    if (id.empty())
        return false;
    std::unique_lock<std::shared_mutex> lock(m_mutex);
    return m_extensionPoints.erase(id) > 0;
}

std::shared_ptr<ExtensionPointBase> ExtensionSystem::extensionPoint(const std::string& id) const
{
    if (id.empty())
        return nullptr;
    std::shared_lock<std::shared_mutex> lock(m_mutex);
    if (auto it = m_extensionPoints.find(id); it != m_extensionPoints.end()) {
        return it->second;
    }
    return nullptr;
}

bool ExtensionSystem::hasExtensionPoint(const std::string& id) const
{
    if (id.empty())
        return false;
    std::shared_lock<std::shared_mutex> lock(m_mutex);
    return m_extensionPoints.contains(id);
}
std::vector<std::string> ExtensionSystem::extensionPointIds()
{
    std::shared_lock<std::shared_mutex> lock(m_mutex);
    std::vector<std::string> keys;
    keys.reserve(m_extensionPoints.size());
    for (const auto& [k, _] : m_extensionPoints) {
        keys.push_back(k);
    }
    return keys;
}
void ExtensionSystem::clear()
{
    std::unique_lock<std::shared_mutex> lock(m_mutex);
    m_extensionPoints.clear();
}

} // namespace bakuon::gui
