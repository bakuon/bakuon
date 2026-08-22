#pragma once

#include <QtCore/QDebug>

#include "gui/b_extensionpoint.h"

// 仅内部注册接口扩展点使用

namespace bakuon::gui {

/**
 * @brief 扩展点接口 - immediate / advanced
 * @note  用于立即实例化扩展对象。与之相对的延迟加载实例对象为 DeferredExtensionPoint
 * @tparam T 扩展接口类型,不允许从 QObject 继承，且必须使用 Q_DECLARE_INTERFACE 定义接口 IID
 * 
 * 设计思想：
 * - 开闭原则：对扩展开放，对修改关闭
 * - 依赖倒置：主程序依赖抽象，插件实现抽象
 * - 单一职责：仅负责扩展的注册和查询
 * 
 * 使用场景：
 * - 编辑器工厂扩展点
 * - 语法高亮提供者扩展点
 * - 代码补全提供者扩展点
 * - 主题提供者扩展点
 * - 更多...
 */
template<typename T>
class AdvancedExtensionPoint : public IExtensionPoint<T>
{
    static_assert(!std::is_base_of<QObject, T>::value,
                  "Extension interface T must NOT inherit QObject. "
                  "Define T as a pure C++ abstract interface. "
                  "Implement QObject in the concrete class if Qt features are needed.");

public:
    using typename IExtensionPoint<T>::ExtensionPtr;
    using typename IExtensionPoint<T>::ExtensionList;
    using typename IExtensionPoint<T>::FilterFunc;

    // 禁止 T 继承 QObject

    explicit AdvancedExtensionPoint(const QString& id, const QString& description = {});
    ~AdvancedExtensionPoint() override;

    QString id() const override { return m_id; }
    QString description() const override { return m_description; }

    bool registerExtension(ExtensionPtr extension, int priority) override;
    bool unregisterExtension(ExtensionPtr extension) override;

    ExtensionList extensions() const override;
    ExtensionList extensions(FilterFunc filter) const override;

    int priority(ExtensionPtr extension) const override;
    bool setPriority(ExtensionPtr extension, int newPriority) override;

    int count() const override;
    void clear() override;

    QString diagnostics() const;

private:
    /**
     * @brief 扩展对象被销毁时自动注销
     * @note 当支持 QObject 类型的扩展点时
     */
    void onExtensionDestroyed(QObject* obj);

private:
    QString m_id; // 接口唯一标识符IID
    QString m_description;
    /**
     * @todo 修改隐患
     * 
     * QMap<int, ExtensionPtr> 同优先级只能存一个扩展（隐患）
     * // 当前：QMap<int, ExtensionPtr> m_extensions; key = -priority
     * // 两个 priority=100 的工厂，第二个会覆盖第一个
     * m_extensions.insert(-priority, extension);
     * 
     * 应改为 QMultiMap 或 std::vector<Entry> + 排序：
     *
     * // 修复同优先级多扩展的存储问题
     * struct Entry {
     *     int priority;
     *     ExtensionPtr extension;
     * };
     * 
     * // 替换 QMap<int, ExtensionPtr>
     * std::vector<Entry> m_extensions; // 按 priority 降序插入保持有序
     */
    QMultiMap<int, ExtensionPtr> m_extensions; // key: -priority
    mutable QReadWriteLock m_lock;
};

template<typename T>
AdvancedExtensionPoint<T>::AdvancedExtensionPoint(const QString& id, const QString& description)
    : m_id(id) // 调用处可使用 qobject_interface_iid<T*>() 获取由Q_DECLARE_INTERFACE定义的接口IID
    , m_description(description)
{
    qDebug() << "AdvancedExtensionPoint: Created" << m_id << m_description;
}

template<typename T>
inline AdvancedExtensionPoint<T>::~AdvancedExtensionPoint()
{
    qDebug() << "AdvancedExtensionPoint: Destroyed" << m_id;
}

template<typename T>
inline bool AdvancedExtensionPoint<T>::registerExtension(ExtensionPtr extension, int priority)
{
    if (!extension) {
        qWarning() << "AdvancedExtensionPoint: Cannot register null extension";
        return false;
    }

    QWriteLocker locker(&m_lock);

    // 检查是否已注册
    for (const auto& ext : m_extensions) {
        if (ext == extension) {
            qWarning() << "AdvancedExtensionPoint: Extension already registered in" << m_id;
            return false;
        }
    }

    // 检查依赖的扩展点是否存在
    // auto deps = extension->dependencies(); // TODO 增加依赖方法
    // for (const QString& depId : deps) {
    //     if (!ExtensionRegistry::hasExtensionPoint(depId)) {
    //         throw std::runtime_error(QString("Missing dependency: %1").arg(depId).toStdString());
    //     }
    // }

    // 使用负数实现降序排序
    // priority 100 → key -100 (排在前面)
    // priority 50  → key -50  (排在后面)
    m_extensions.insert(-priority, extension);

    // 注：扩展点注册的都是扩展接口类，并非继承 QOjbect 。
    QString extName = typeid(T).name();
    int count       = m_extensions.size();
    locker.unlock();

    Q_EMIT ExtensionPointBase::extensionRegistered(extName, priority);
    Q_EMIT ExtensionPointBase::countChanged(count);

    qDebug() << "AdvancedExtensionPoint: Extension registered" << extName << "priority:" << priority
             << "in" << m_id;
    return true;
}

template<typename T>
inline bool AdvancedExtensionPoint<T>::unregisterExtension(ExtensionPtr extension)
{
    if (!extension) {
        return false;
    }

    QWriteLocker locker(&m_lock);

    auto it    = m_extensions.begin();
    bool found = false;

    while (it != m_extensions.end()) {
        if (it.value() == extension) {
            it    = m_extensions.erase(it);
            found = true;
            break; // 假设每个扩展只注册一次
        }
        ++it;
    }

    if (!found) {
        return false;
    }

    QString extName = typeid(T).name();
    int count       = m_extensions.size();
    locker.unlock();

    Q_EMIT ExtensionPointBase::extensionUnregistered(extName);
    Q_EMIT ExtensionPointBase::countChanged(count);

    qDebug() << "AdvancedExtensionPoint: Extension unregistered" << extName << "from" << m_id;
    return true;
}

template<typename T>
inline typename AdvancedExtensionPoint<T>::ExtensionList AdvancedExtensionPoint<T>::extensions() const
{
    QReadLocker locker(&m_lock);
    return m_extensions.values();
}

template<typename T>
inline typename AdvancedExtensionPoint<T>::ExtensionList AdvancedExtensionPoint<T>::extensions(
    FilterFunc filter) const
{
    QReadLocker locker(&m_lock);
    ExtensionList result;
    for (const auto& ext : m_extensions) {
        if (filter(ext)) {
            result.append(ext);
        }
    }
    return result;
}

template<typename T>
inline int AdvancedExtensionPoint<T>::priority(ExtensionPtr extension) const
{
    QReadLocker locker(&m_lock);
    for (auto it = m_extensions.constBegin(); it != m_extensions.constEnd(); ++it) {
        if (it.value() == extension) {
            return -it.key(); // 恢复原始优先级（key 是负数）
        }
    }
    return -1;
}

template<typename T>
inline bool AdvancedExtensionPoint<T>::setPriority(ExtensionPtr extension, int newPriority)
{
    QWriteLocker locker(&m_lock);

    auto it    = m_extensions.begin();
    bool found = false;
    while (it != m_extensions.end()) {
        if (it.value() == extension) {
            it    = m_extensions.erase(it);
            found = true;
            break;
        }
        ++it;
    }

    if (!found)
        return false;

    // 重新插入新优先级
    m_extensions.insert(-newPriority, extension);

    qDebug() << "AdvancedExtensionPoint: Updated extension priority to" << newPriority;

    return true;
}

template<typename T>
inline int AdvancedExtensionPoint<T>::count() const
{
    QReadLocker locker(&m_lock);
    return m_extensions.size();
}

template<typename T>
inline void AdvancedExtensionPoint<T>::clear()
{
    QWriteLocker locker(&m_lock);

    m_extensions.clear();
    locker.unlock();

    Q_EMIT ExtensionPointBase::cleared();
    Q_EMIT ExtensionPointBase::countChanged(0);

    qDebug() << "AdvancedExtensionPoint: Cleared all extensions from" << m_id;
}

template<typename T>
inline QString AdvancedExtensionPoint<T>::diagnostics() const
{
    QString info;
    info += QString("Extension Point: %1\n").arg(id());

    QReadLocker locker(&m_lock);

    info += QString("Extension Point: %1\n").arg(m_id);
    info += QString("Description: %1\n").arg(m_description);
    info += QString("Extension Count: %1\n").arg(m_extensions.size());
    info += "Extensions:\n";

    for (auto it = m_extensions.constBegin(); it != m_extensions.constEnd(); ++it) {
        int priority = -it.key();
        info += QString("  - Priority %1: %2\n")
                    .arg(priority)
                    .arg(reinterpret_cast<quintptr>(it.value().get()), 0, 16);
    }

    info += QString("Extension Count: %1\n").arg(count());
    return info;
}

template<typename T>
inline void AdvancedExtensionPoint<T>::onExtensionDestroyed(QObject* obj)
{
    qDebug() << "ExtensionPoint: Extension destroyed, auto-unregistering:" << obj;

    QWriteLocker locker(&m_lock);

    // 从列表中移除（obj 已经在析构中，不能使用成员函数）
    auto it    = m_extensions.begin();
    bool found = false;

    while (it != m_extensions.end()) {
        if (it.value().get() == obj) {
            it    = m_extensions.erase(it);
            found = true;
            break;
        } else {
            ++it;
        }
    }

    if (found) {
        int count = m_extensions.size();
        locker.unlock();

        Q_EMIT ExtensionPointBase::extensionUnregistered(QString());
        Q_EMIT ExtensionPointBase::countChanged(count);
    }
}

} // namespace bakuon::gui
