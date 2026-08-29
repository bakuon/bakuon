#pragma once

#include <shared_mutex>
#include <unordered_map>

#include <bakuon/gui/IExtensionSystem.h>

namespace bakuon::gui {

/* ============================================================
 *  ExtensionSystem：全局扩展点注册中心（Facade）
 * ============================================================*/

template<typename T>
class DefaultExtensionPoint;

/**
 * @brief 扩展点注册中心 - extension facade
 * @note 扩展点是依据主程序支持的扩展接口(内部定义)而在内部创建并注册的，无必要外部不应手动注册扩展点。
 * @todo 在 IPlugin::initialize 中注入 IExtensionSystem 让插件注册扩展，
 *       明示插件作者这是一种契约，以便所有插件在 IPlugin::extensionsInitialized 能相互连接扩展。
 * 
 * 职责：
 *  - 管理所有扩展点的注册、注销与查询；
 *  - 提供类型安全的扩展点访问（模板方法）；
 *  - 读写锁保护并发访问。
 *
 * 使用说明：
 *  - 主程序/核心库：创建 DefaultExtensionPoint<T>（或自定义 IExtensionPoint<T>），
 *    调用 registerExtensionPoint 注册到中心；
 *  - 插件：通过 extensionPoint<T>() 获取扩展点，并 registerExtension 注入实现；
 *  - 消费者：通过 tryExtensions / extensions 获取扩展并调用。
 *
 * 示例：
 * /// 在内部：
 * // 定义扩展接口
 * class IEditor
 * {
 * public:
 *     // some interfaces
 *     // ...
 * };
 * Q_DECLARE_INTERFACE(bakuon::gui::IEditor, "com.bakuon.extension.IEditor")
 *
 * // 创建接口扩展点
 * auto editorExtensionPoint = std::make_shared<IExtensionPoint<IEditor>>(
 *     qobject_interface_iid<IEditor*>(),//"com.bakuon.extension.editor",
 *     "Editor extension interface"
 * );
 * // 注册到全局注册表
 * ExtensionSystem::registerExtensionPoint(editorExtensionPoint);
 * // 监听扩展变化
 * connect(editorExtensionPoint.get(), &ExtensionPointBase::extensionRegistered,
 *         this, &SomeQObject::onEditorRegistered);
 *
 * // 使用接口扩展实例
 * // IEditor* editor = ... // 获取接口扩展方式与同外部一致
 * // 将编辑器的部件置于主窗口编辑器布局
 * QWidget *widget = editor->widget();
 * editorArea->addWidget(widget);
 * 
 * ----------------------------------------------------------------------
 *
 * /// 在外部（插件 A）：
 * // 实现扩展接口
 * class TextEditor : public TextEditor{};
 * // 使用扩展点
 * auto extensionPoint = ExtensionSystem::extensionPoint<IEditor>();
 * // 向扩展点注册接口扩展实例
 * extensionPoint->registerExtension(std::make_shared<TextEditor>());
 *
 * // 使用扩展接口（插件 B）：
 * IEditor* editor = editorExtensionPoint->tryExtensions<IEditor*>(
 *     [&](auto editor) -> IEditor* {
 *         return editor->canHandle(filePath) ? editor : nullptr;
 *     },
 *     nullptr  // 默认值
 * );
 * editor->open(filePath);
 */
class ExtensionSystem : public IExtensionSystem
{
public:
    /**
     * @brief 获取全局单例（Meyers' Singleton，线程安全、自动析构）
     */
    static ExtensionSystem& instance()
    {
        static ExtensionSystem s;
        return s;
    }

    ExtensionSystem(const ExtensionSystem&)            = delete;
    ExtensionSystem& operator=(const ExtensionSystem&) = delete;

    /* ---------- 扩展点注册 / 注销 ---------- */

    /**
     * @brief 注册扩展点
     * @param point 扩展点基类指针
     * @return true 注册成功；false 为空或 ID 已存在
     * @note 通常由核心库内部调用，插件无需手动注册扩展点
     */
    bool registerExtensionPoint(std::shared_ptr<ExtensionPointBase> point) override;

    /**
     * @brief 便捷注册：直接构造 DefaultExtensionPoint<T> 并注册
     * @tparam T           扩展接口类型
     * @param description  描述
     * @param id           可选 IID；为空时使用 extension_iid<T>::value()
     * @return 注册成功返回 shared_ptr<DefaultExtensionPoint<T>>；失败返回 nullptr
     */
    template<typename T>
    std::shared_ptr<DefaultExtensionPoint<T>> registerDefaultExtensionPoint(
        std::string description = {}, std::string id = {});

    /**
     * @brief 注销扩展点
     * @param id 扩展点 IID
     */
    bool unregisterExtensionPoint(const std::string& id) override;

    /**
     * @brief 按 ID 获取类型擦除的扩展点
     * @param id 扩展点 IID
     *
     * @note 获取接口唯一 IID 便捷方法：
     *       1.如果使用 BAKUON_DECLARE_EXTENSION_IID 注册的使用 bakuon::gui::extension_iid<T>() 
     *       2.如果使用 Q_DECLARE_INTERFACE 定注册的使用 qobject_interface_iid<T>()      
     */
    std::shared_ptr<ExtensionPointBase> extensionPoint(const std::string& id) const override;

    /**
     * @brief 类型安全地获取指定 T 的扩展点
     * @note 需重新定义同名模板函数，或调用基类模板函数： this->IExtensionSystem::template extensionPoint<T>(id);
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
    bool hasExtensionPoint(const std::string& id) const override;

    /**
     * @brief 列出所有已注册扩展点 ID
     */
    std::vector<std::string> extensionPointIds() override;

    /**
     * @brief 清空所有扩展点（通常仅用于测试/卸载）
     */
    void clear() override;

private:
    ExtensionSystem()           = default;
    ~ExtensionSystem() override = default;

    std::unordered_map<std::string, std::shared_ptr<ExtensionPointBase>> m_extensionPoints;
    mutable std::shared_mutex m_mutex;
};

template<typename T>
inline std::shared_ptr<DefaultExtensionPoint<T>> ExtensionSystem::registerDefaultExtensionPoint(
    std::string description, std::string id)
{
    if (id.empty()) {
        id = std::string(extension_iid<T>::value());
    }
    if (id.empty()) {
        return nullptr;
    }
    auto point = std::make_shared<DefaultExtensionPoint<T>>(std::move(id), std::move(description));
    if (!registerExtensionPoint(point)) {
        return nullptr;
    }
    return point;
}

} // namespace bakuon::gui
