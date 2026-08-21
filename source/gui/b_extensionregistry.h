#pragma once

#include <QtCore/QHash>
#include <QtCore/QReadWriteLock>

#include "gui/b_extensionpoint.h"

namespace bakuon::gui {

class ExtensionRegistry
{
public:
    /**
     * @brief 注册扩展点
     * @param point 扩展点智能指针
     * @note 只接受基类指针
     */
    bool registerExtensionPoint(const std::shared_ptr<ExtensionPointBase>& point);

    /**
     * @brief 注销扩展点
     * @param id 扩展点ID
     */
    bool unregisterExtensionPoint(const QString& id);

    /**
     * @brief 解析指定ID的扩展点
     * @param id 扩展点ID
     *
     * @note 如果使用了 Q_DECLARE_INTERFACE 定义可使用 qobject_interface_iid<T>() 获取接口唯一 ID
     */
    std::shared_ptr<ExtensionPointBase> extensionPoint(const QString& id) const;

    /**
     * @brief 解析指定类型接口的扩展点
     * @note 使用 Q_DECLARE_INTERFACE 定义并通过 qobject_interface_iid<T>() 获取接口唯一 ID
     */
    template<typename T>
    std::shared_ptr<IExtensionPoint<T>> tryExtensionPoint(const QString& id) const
    {
        // 类型安全转换
        return std::dynamic_pointer_cast<IExtensionPoint<T>>(ExtensionRegistry::extensionPoint(id));
    }

    /**
     * @brief 检查扩展点是否存在
     * @param id 扩展点ID
     */
    bool hasExtensionPoint(const QString& id) const;

    /**
     * @brief 列出所有扩展点ID
     */
    QStringList extensionPointIds() const;

    /**
     * @brief 是否有扩展点
     * @param id 扩展点ID
     */
    bool contains(const QString& id) const;

    /**
     * @brief 清空所有扩展点
     */
    void clear();

private:
    QHash<QString, std::shared_ptr<ExtensionPointBase>> m_extensionPoints;
    mutable QReadWriteLock m_lock;
};

} // namespace bakuon::gui
