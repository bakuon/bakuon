#include "gui/b_extensionsystem.h"

#include <shared_mutex>
#include <unordered_map>
// std::unique_lock / std::shared_lock —— <shared_mutex> 只保证 std::shared_mutex
// 本身可用，不保证连带带进 std::unique_lock；MSVC 的实现凑巧传递包含了，
// GCC/Clang 下必须显式 include，否则编译不过。
#include <mutex>

namespace bakuon::gui {

class ExtensionSystem::Impl
{
public:
    std::unordered_map<std::string, std::shared_ptr<ExtensionPointBase>> extensionPoints;
    mutable std::shared_mutex mutex;
};

ExtensionSystem::ExtensionSystem()
    : d(new Impl())
{
}

ExtensionSystem::~ExtensionSystem()
{
    delete d;
}

bool ExtensionSystem::registerExtensionPoint(std::shared_ptr<ExtensionPointBase> point)
{
    if (!point)
        return false;

    std::unique_lock<std::shared_mutex> lock(d->mutex);
    const std::string id = point->id();
    if (id.empty() || d->extensionPoints.contains(id)) {
        return false;
    }
    d->extensionPoints.emplace(id, std::move(point));
    return true;
}

bool ExtensionSystem::unregisterExtensionPoint(const std::string& id)
{
    if (id.empty())
        return false;
    std::unique_lock<std::shared_mutex> lock(d->mutex);
    return d->extensionPoints.erase(id) > 0;
}

std::shared_ptr<ExtensionPointBase> ExtensionSystem::extensionPoint(const std::string& id) const
{
    if (id.empty())
        return nullptr;
    std::shared_lock<std::shared_mutex> lock(d->mutex);
    if (auto it = d->extensionPoints.find(id); it != d->extensionPoints.end()) {
        return it->second;
    }
    return nullptr;
}

bool ExtensionSystem::hasExtensionPoint(const std::string& id) const
{
    if (id.empty())
        return false;
    std::shared_lock<std::shared_mutex> lock(d->mutex);
    return d->extensionPoints.contains(id);
}
std::vector<std::string> ExtensionSystem::extensionPointIds()
{
    std::shared_lock<std::shared_mutex> lock(d->mutex);
    std::vector<std::string> keys;
    keys.reserve(d->extensionPoints.size());
    for (const auto& [k, _] : d->extensionPoints) {
        keys.push_back(k);
    }
    return keys;
}
void ExtensionSystem::clear()
{
    std::unique_lock<std::shared_mutex> lock(d->mutex);
    d->extensionPoints.clear();
}

} // namespace bakuon::gui
