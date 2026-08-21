#include "gui/b_extensionpoint.h"
#include "gui/b_extensionregistry.h"

#include <QHash>

namespace bakuon::gui {

// Q_GLOBAL_STATIC(ExtensionRegistry, registry)
// static ExtensionRegistryPrivate& reg()
// {
//     return *registry();
// }

/**
 * @brief 获取进程单例（Construct-On-First-Use，无静态析构风险）
 *
 * 使用函数局部静态变量，C++11 起保证线程安全的初始化；
 * 堆分配后永不 delete（intentional leak），避免 exit-time destructor。
 */
static ExtensionRegistry& reg()
{
    // 指针本身是 trivial，不触发 exit-time destructor。
    static auto* d = new ExtensionRegistry;
    return *d;
}

bool ExtensionSystem::registerExtensionPoint(const std::shared_ptr<ExtensionPointBase>& point)
{
    return reg().registerExtensionPoint(point);
}

bool ExtensionSystem::unregisterExtensionPoint(const QString& id)
{
    return reg().unregisterExtensionPoint(id);
}

std::shared_ptr<ExtensionPointBase> ExtensionSystem::extensionPoint(const QString& id)
{
    return reg().extensionPoint(id);
}

bool ExtensionSystem::hasExtensionPoint(const QString& id)
{
    return reg().hasExtensionPoint(id);
}

QStringList ExtensionSystem::extensionPointIds()
{
    return reg().extensionPointIds();
}

bool ExtensionSystem::contains(const QString& id)
{
    return reg().contains(id);
}

void ExtensionSystem::clear()
{
    reg().clear();
}

} // namespace bakuon::gui
