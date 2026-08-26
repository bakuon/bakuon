#pragma once

#include <bakuon/gui/IExtensionPoint.h>

namespace bakuon::gui {

class IExtensionSystem
{
public:
    virtual ~IExtensionSystem() = default;

    /* ---------- 扩展点注册 / 注销 ---------- */

    /**
     * @brief 注册扩展点
     * @param point 扩展点基类指针
     * @return true 注册成功；false 为空或 ID 已存在
     * @note 通常由核心库内部调用，插件无需手动注册扩展点
     */
    virtual bool registerExtensionPoint(std::shared_ptr<ExtensionPointBase> point) = 0;

    /**
     * @brief 注销扩展点
     * @param id 扩展点 IID
     */
    virtual bool unregisterExtensionPoint(const std::string& id) = 0;

    /**
     * @brief 按 ID 获取类型擦除的扩展点
     * @param id 扩展点 IID
     *
     * @note 获取接口唯一 IID 便捷方法：
     *       1.如果使用 BAKUON_DECLARE_EXTENSION_IID 注册的使用 bakuon::gui::extension_iid<T>() 
     *       2.如果使用 Q_DECLARE_INTERFACE 定注册的使用 qobject_interface_iid<T>()      
     */
    virtual std::shared_ptr<ExtensionPointBase> extensionPoint(const std::string& id) const = 0;

    /**
     * @brief 类型安全地获取指定 T 的扩展点
     * @tparam T 接口类型
     * @param id IID；为空时使用 extension_iid<T>::value()
     * @return 转换成功返回 shared_ptr<IExtensionPoint<T>>；失败返回 nullptr
     */
    template<typename T>
    std::shared_ptr<IExtensionPoint<T>> extensionPoint(const std::string& id = {}) const
    {
        std::string iid = id.empty() ? std::string(extension_iid<T>::value()) : id;
        return std::dynamic_pointer_cast<IExtensionPoint<T>>(extensionPoint(iid));
    }

    /**
     * @brief 是否存在指定 ID 的扩展点
     */
    virtual bool hasExtensionPoint(const std::string& id) const = 0;

    /**
     * @brief 列出所有已注册扩展点 ID
     */
    virtual std::vector<std::string> extensionPointIds() = 0;

    /**
     * @brief hasExtensionPoint 的别名
     */
    virtual bool contains(const std::string& id) const { return hasExtensionPoint(id); }

    /**
     * @brief 清空所有扩展点（通常仅用于测试/卸载）
     */
    virtual void clear() = 0;

    /**
     * @brief 获取接口 T 的 IID
     * @tparam T 接口类型
     * @return extension_iid<T>::value()，若未特化返回空串
     */
    template<typename T>
    static std::string IID()
    {
        return std::string(extension_iid<T>::value());
    }
};

} // namespace bakuon::gui
