#include "gui/b_extensionregistry.h"

#include <QtCore/QDebug>

namespace bakuon::gui {

bool ExtensionRegistry::registerExtensionPoint(const std::shared_ptr<ExtensionPointBase>& point)
{
    QWriteLocker locker(&m_lock);

    if (!point) {
        qWarning() << "ExtensionRegistry: Cannot register null extension point";
        return false;
    }

    const QString& id = point->id();
    if (m_extensionPoints.contains(id)) {
        qWarning() << "ExtensionRegistry: Extension point already registered:" << id;
        return false;
    }

    // 存储为基类以支持不同类型
    m_extensionPoints[id] = point;
    qDebug() << "ExtensionRegistry: Registered extension point:" << id;

    return true;
}

bool ExtensionRegistry::unregisterExtensionPoint(const QString& id)
{
    QWriteLocker locker(&m_lock);

    if (!m_extensionPoints.contains(id)) {
        qWarning() << "ExtensionRegistry: Extension point not found:" << id;
        return false;
    }

    m_extensionPoints.remove(id);
    qDebug() << "ExtensionRegistry: Unregistered extension point:" << id;
    return true;
}

std::shared_ptr<ExtensionPointBase> ExtensionRegistry::extensionPoint(const QString& id) const
{
    QReadLocker locker(&m_lock);
    return m_extensionPoints.value(id, nullptr);
}

bool ExtensionRegistry::hasExtensionPoint(const QString& id) const
{
    QReadLocker locker(&m_lock);
    return m_extensionPoints.contains(id);
}

QStringList ExtensionRegistry::extensionPointIds() const
{
    QReadLocker locker(&m_lock);
    return m_extensionPoints.keys();
}

void ExtensionRegistry::clear()
{
    QWriteLocker locker(&m_lock);
    m_extensionPoints.clear();
    qDebug() << "ExtensionRegistry: Cleared all extension points";
}

bool ExtensionRegistry::contains(const QString& id) const
{
    QReadLocker locker(&m_lock);
    return m_extensionPoints.contains(id);
}

} // namespace bakuon::gui
