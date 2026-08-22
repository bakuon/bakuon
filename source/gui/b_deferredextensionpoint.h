#pragma once

#include <QtCore/QDebug>
#include <QtCore/QMap>
#include <QtCore/QMetaClassInfo>
#include <QtCore/QPointer>
#include <QtCore/QReadWriteLock>

#include "gui/b_extensionpoint.h"

// 仅内部注册延迟加载接口扩展点使用

namespace bakuon::gui {

/**
 * @brief 扩展描述符（元数据）
 * @note 用于延迟加载，只在需要时才实例化扩展对象。
 *
 */
template<typename T>
struct DeferredExtensionDescriptor
{
    QString id;                                 // 扩展ID
    QString pluginId;                           // 所属插件ID
    int priority{0};                            // 优先级
    QVariantMap metadata;                       // 元数据
    std::function<std::shared_ptr<T>()> loader; // 延迟加载器

    bool loaded = false;         // 是否已加载
    std::shared_ptr<T> instance; // 加载后的实例(使用 QPointer 自动跟踪生命周期)
};

/**
 * @brief 延迟加载的扩展点 
 * 
 * 设计目标：
 * 1. 减少启动时间 - 不立即创建所有扩展
 * 2. 减少内存占用 - 只加载需要的扩展
 * 3. 支持条件加载 - 根据配置决定是否加载
 * 
 * 使用场景：
 * - 大量插件的系统
 * - 扩展创建成本高（需要初始化大量资源）
 * - 某些扩展不常用
 * 
 */
template<typename T>
class DeferredExtensionPoint : public IExtensionPoint<T>
{
public:
    using typename IExtensionPoint<T>::ExtensionPtr;
    using typename IExtensionPoint<T>::ExtensionList;
    using typename IExtensionPoint<T>::FilterFunc;

    explicit DeferredExtensionPoint(const QString& id, const QString& description = {});
    ~DeferredExtensionPoint() override;

    QString id() const override { return m_id; }
    QString description() const override { return m_description; }

    /**
     * @brief 注册扩展描述符（不立即创建实例）
     * @param descriptor 扩展描述符
     */
    void registerDescriptor(const DeferredExtensionDescriptor<T>& descriptor);

    /**
     * @brief 便捷注册方法
     */
    void registerDescriptor(const QString& extensionId, const QString& pluginId, int priority,
                            std::function<ExtensionPtr()> loader,
                            const QVariantMap& metadata = QVariantMap());

    void registerExtension(ExtensionPtr extension, int priority) override;
    bool unregisterExtension(ExtensionPtr extension) override;

    /**
     * @brief 扩展访问
     * @note 会触发加载扩展
     */
    ExtensionList extensions() const override;
    ExtensionList extensions(FilterFunc filter) const override;

    int count() const override;
    void clear() override;

    /**
     * @brief 预加载所有扩展
     */
    void preloadAll();

    /**
     * @brief 预加载指定插件的扩展
     */
    void preloadPlugin(const QString& pluginId);

    /**
     * @brief 卸载未使用的扩展（释放内存）
     */
    void unloadUnused();

    /**
     * @brief 获取扩展元数据（不触发加载）
     */
    QVariantMap metadata(const QString& extensionId) const;

    /**
     * @brief 检查扩展是否已加载
     */
    bool isLoaded(const QString& extensionId) const;

    /**
     * @brief 获取所有扩展ID
     */
    QStringList extensionIds() const;

    /**
     * @brief 获取加载统计
     */
    struct LoadStats
    {
        int total    = 0;
        int loaded   = 0;
        int unloaded = 0;

        QString toString() const
        {
            return QString("Total: %1, Loaded: %2, Unloaded: %3 (%.1f%%)")
                .arg(total)
                .arg(loaded)
                .arg(unloaded)
                .arg(total > 0 ? (loaded * 100.0 / total) : 0.0);
        }
    };

    LoadStats loadStats() const;

    /**
     * @brief 诊断信息
     */
    QString diagnostics() const;

private:
    /**
     * @brief 加载所有扩展
     */
    void loadAll() const;

    /**
     * @brief 加载单个扩展
     */
    void loadExtension(DeferredExtensionDescriptor<T>& desc) const;

    /**
     * @brief 重建优先级
     */
    void rebuildSortedIds();

private:
    QString m_id;
    QString m_description;

    mutable int m_loadedCount = 0;
    mutable QHash<QString, DeferredExtensionDescriptor<T>> m_descriptors;
    mutable QStringList m_sortedIds; // 按优先级排序的ID列表
    mutable QReadWriteLock m_lock;
};

template<typename T>
DeferredExtensionPoint<T>::DeferredExtensionPoint(const QString& id, const QString& description)
    : m_id(id)
    , m_description(description)
{
    qDebug() << "DeferredExtensionPoint: Created with" << id;
}

template<typename T>
DeferredExtensionPoint<T>::~DeferredExtensionPoint()
{
    qDebug() << "DeferredExtensionPoint: Destroyed" << m_id << "(" << m_loadedCount << "/"
             << m_descriptors.size() << " loaded)";
}

template<typename T>
void DeferredExtensionPoint<T>::registerDescriptor(const DeferredExtensionDescriptor<T>& descriptor)
{
    QWriteLocker locker(&m_lock);

    if (m_descriptors.contains(descriptor.id)) {
        qWarning() << "DeferredExtensionPoint: Descriptor already registered:" << descriptor.id;
        return;
    }

    m_descriptors.insert(descriptor.id, descriptor);
    rebuildSortedIds();

    qDebug() << "DeferredExtensionPoint: Registered descriptor" << descriptor.id << "with priority"
             << descriptor.priority;
}

template<typename T>
void DeferredExtensionPoint<T>::registerDescriptor(const QString& extensionId,
                                                   const QString& pluginId, int priority,
                                                   std::function<ExtensionPtr()> loader,
                                                   const QVariantMap& metadata)
{
    DeferredExtensionDescriptor<T> desc;
    desc.id       = extensionId;
    desc.pluginId = pluginId;
    desc.priority = priority;
    desc.loader   = loader;
    desc.metadata = metadata;
    desc.loaded   = false;

    registerDescriptor(desc);
}

template<typename T>
void DeferredExtensionPoint<T>::registerExtension(ExtensionPtr extension, int priority)
{
    if (!extension) {
        qWarning() << "DeferredExtensionPoint: Cannot register null extension";
        return;
    }

    // 创建一个"已加载"的描述符
    DeferredExtensionDescriptor<T> desc;
    desc.id       = QString("immediate_%1").arg(reinterpret_cast<quintptr>(extension.get()));
    desc.pluginId = "immediate";
    desc.priority = priority;
    desc.loader   = [extension]() { return extension; };
    desc.loaded   = true;
    desc.instance = extension;

    QWriteLocker locker(&m_lock);
    m_descriptors[desc.id] = desc;
    rebuildSortedIds();
    m_loadedCount++;
    int count = m_descriptors.size();
    locker.unlock();

    Q_EMIT ExtensionPointBase::extensionRegistered(desc.id, priority);
    Q_EMIT ExtensionPointBase::countChanged(count);
}

template<typename T>
bool DeferredExtensionPoint<T>::unregisterExtension(ExtensionPtr extension)
{
    if (!extension) {
        return false;
    }

    QWriteLocker locker(&m_lock);

    QString targetId = extension->objectName();
    if (!m_descriptors.contains(targetId)) {
        return false;
    }

    m_descriptors.remove(targetId);
    rebuildSortedIds();

    int count = m_descriptors.size();
    locker.unlock();

    Q_EMIT ExtensionPointBase::extensionUnregistered(targetId);
    Q_EMIT ExtensionPointBase::countChanged(count);

    return true;
}

template<typename T>
typename DeferredExtensionPoint<T>::ExtensionList DeferredExtensionPoint<T>::extensions() const
{
    QReadLocker locker(&m_lock);

    // 加载所有扩展
    loadAll();

    ExtensionList result;
    for (const QString& id : m_sortedIds) {
        const auto& desc = m_descriptors[id];
        if (desc.loaded && desc.instance) {
            result.append(desc.instance);
        }
    }

    return result;
}

template<typename T>
typename DeferredExtensionPoint<T>::ExtensionList DeferredExtensionPoint<T>::extensions(
    FilterFunc filter) const
{
    QReadLocker locker(&m_lock);

    // 加载所有扩展
    loadAll();

    ExtensionList result;
    for (const QString& id : m_sortedIds) {
        const auto& desc = m_descriptors[id];
        if (desc.loaded && desc.instance && filter(desc.instance)) {
            result.append(desc.instance);
        }
    }

    return result;
}

template<typename T>
int DeferredExtensionPoint<T>::count() const
{
    QReadLocker locker(&m_lock);
    return m_descriptors.size();
}

template<typename T>
void DeferredExtensionPoint<T>::clear()
{
    QWriteLocker locker(&m_lock);
    m_descriptors.clear();
    m_sortedIds.clear();
    locker.unlock();

    Q_EMIT ExtensionPointBase::cleared();
    Q_EMIT ExtensionPointBase::countChanged(0);
}

template<typename T>
void DeferredExtensionPoint<T>::preloadAll()
{
    QWriteLocker locker(&m_lock);
    loadAll();
}
template<typename T>
void DeferredExtensionPoint<T>::preloadPlugin(const QString& pluginId)
{
    QWriteLocker locker(&m_lock);

    for (auto& desc : m_descriptors) {
        if (desc.pluginId == pluginId && !desc.loaded) {
            loadExtension(desc);
        }
    }
}
template<typename T>
void DeferredExtensionPoint<T>::unloadUnused()
{
    QWriteLocker locker(&m_lock);

    for (auto& desc : m_descriptors) {
        if (desc.loaded && !desc.instance.isNull()) {
            desc.instance.clear();
            desc.loaded = false;
            m_loadedCount--;

            qDebug() << "DeferredExtensionPoint: Unloaded unused extension" << desc.id;
        }
    }
}
template<typename T>
QVariantMap DeferredExtensionPoint<T>::metadata(const QString& extensionId) const
{
    QReadLocker locker(&m_lock);

    auto it = m_descriptors.find(extensionId);
    if (it != m_descriptors.end()) {
        return it->metadata;
    }

    return QVariantMap();
}
template<typename T>
bool DeferredExtensionPoint<T>::isLoaded(const QString& extensionId) const
{
    QReadLocker locker(&m_lock);

    auto it = m_descriptors.find(extensionId);
    return it != m_descriptors.end() && it->loaded;
}

template<typename T>
QStringList DeferredExtensionPoint<T>::extensionIds() const
{
    QReadLocker locker(&m_lock);
    return m_sortedIds;
}

template<typename T>
typename DeferredExtensionPoint<T>::LoadStats DeferredExtensionPoint<T>::loadStats() const
{
    QReadLocker locker(&m_lock);

    LoadStats stats;
    stats.total    = m_descriptors.size();
    stats.loaded   = m_loadedCount;
    stats.unloaded = stats.total - stats.loaded;

    return stats;
}

template<typename T>
QString DeferredExtensionPoint<T>::diagnostics() const
{
    QReadLocker locker(&m_lock);

    QString info;
    info += QString("Deferred Extension Point: %1\n").arg(m_id);
    info += QString("Description: %1\n").arg(m_description);
    info += QString("Total Extensions: %1\n").arg(m_descriptors.size());
    info += QString("Loaded Extensions: %1\n").arg(m_loadedCount);
    info += QString("Load Rate: %.1f%%\n")
                .arg(m_descriptors.size() > 0 ? (m_loadedCount * 100.0 / m_descriptors.size())
                                              : 0.0);
    info += "\nExtensions:\n";

    for (const QString& id : m_sortedIds) {
        const auto& desc = m_descriptors[id];
        info += QString("  [%1] %2 (priority: %3, plugin: %4)\n")
                    .arg(desc.loaded ? "✓" : " ")
                    .arg(desc.id)
                    .arg(desc.priority)
                    .arg(desc.pluginId);
    }

    return info;
}

template<typename T>
void DeferredExtensionPoint<T>::loadAll() const
{
    for (const QString& id : m_sortedIds) {
        auto& desc = m_descriptors[id];
        if (!desc.loaded) {
            loadExtension(desc);
        }
    }
}

template<typename T>
void DeferredExtensionPoint<T>::loadExtension(DeferredExtensionDescriptor<T>& desc) const
{
    if (desc.loaded) {
        return;
    }

    qDebug() << "DeferredExtensionPoint: Loading extension" << desc.id;

    try {
        desc.instance = desc.loader();
        desc.loaded   = true;

        m_loadedCount++;

        qDebug() << "DeferredExtensionPoint: Loaded extension" << desc.id;
    } catch (const std::exception& e) {
        qCritical() << "DeferredExtensionPoint: Failed to load extension" << desc.id << ":"
                    << e.what();
    } catch (...) {
        qCritical() << "DeferredExtensionPoint: Failed to load extension" << desc.id
                    << ": unknown error";
    }
}

template<typename T>
void DeferredExtensionPoint<T>::rebuildSortedIds()
{
    m_sortedIds.clear();

    QList<QString> ids = m_descriptors.keys();
    std::sort(ids.begin(), ids.end(), [this](const QString& a, const QString& b) {
        return m_descriptors[a].priority > m_descriptors[b].priority;
    });

    m_sortedIds = ids;
}

} // namespace bakuon::gui
