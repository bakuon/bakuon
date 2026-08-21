#pragma once

#include <QtCore/QList>
#include <QtCore/QObject>
#include <QtCore/QReadWriteLock>

/// 此头文件暴露于外部提供注册接口扩展点与获取接口扩展点

namespace bakuon::gui {

/**
 * @brief 扩展点基类
 *
 */
class ExtensionPointBase : public QObject
{
    Q_OBJECT
public:
    virtual ~ExtensionPointBase() override = default;

    /**
     * @brief 扩展点唯一标识符IID
     * @example "com.bakuon.IEditor"
     */
    [[nodiscard]] virtual QString id() const = 0;

    /**
     * @brief 扩展点描述
     */
    [[nodiscard]] virtual QString description() const = 0;

Q_SIGNALS:
    /**
     * @brief 扩展已注册
     * @param extensionName 扩展的名称
     * @param priority 优先级
     */
    void extensionRegistered(const QString& extensionName, int priority);

    /**
     * @brief 扩展已注销
     * @param extensionName 扩展的名称
     */
    void extensionUnregistered(const QString& extensionName);

    /**
     * @brief 扩展数量已改变
     */
    void countChanged(int count);

    /**
     * @brief 已清空
     */
    void cleared();
};

/**
 * @brief 扩展点接口
 * @tparam T 扩展接口类型
 * @note 扩展点是一个"能力槽位"，定义了主程序需要什么样的扩展
 *
 * 扩展点（Extension Point）是插件架构中的反向依赖注入机制：
 *  - 传统依赖：主程序 → 具体实现（硬编码）
 *  - 扩展点：  主程序 → 抽象接口 ← 插件实现（动态注册）
 * 核心思想：主程序定义"能力槽位"（扩展点），插件填充具体实现。
 *
 * 设计特点：
 *  - 只依赖接口
 *  - 插件实现接口即可
 *  - 运行时加载/卸载
 *  - 自然支持多个实现
 *  - 开放扩展，关闭修改： 插件只需注册，主程序无需修改
 *
 * 使用场景：
 * - 编辑器工厂扩展点
 * - 语法高亮提供者扩展点
 * - 代码补全提供者扩展点
 * - 主题提供者扩展点
 *
 * 使用示例：
 * // 定义编辑器工厂扩展点
 * using EditorFactoryExtension = IExtensionPoint<IEditorFactory>;
 * // 获取扩展点
 * EditorFactoryExtension extensionPoint = ExtensionSystem::extensionPoint<IEditorFactory>();
 * // 向扩展点注册工厂扩展（插件）
 * extensionPoint->registerExtension(std::make_shared<MyEditorFactory>());
 */
template<typename T>
class IExtensionPoint : public ExtensionPointBase
{
public:
    using ExtensionPtr  = std::shared_ptr<T>;
    using ExtensionList = QList<ExtensionPtr>;
    using FilterFunc    = std::function<bool(const ExtensionPtr&)>;

    virtual ~IExtensionPoint() override = default;

    /**
     * @brief 扩展点唯一标识符
     * @note 是扩展接口唯一IID，通常定义接口时由 Q_DECLARE_INTERFACE 定义，
     *  可以通过 qobject_interface_iid<IMyInterface*>() 来获取接口的 IID
     * @example "com.bakuon.extension.IEditor"
     */
    [[nodiscard]] QString id() const override = 0;

    /**
     * @brief 扩展点描述
     */
    [[nodiscard]] QString description() const override = 0;

    /**
     * @brief 注册扩展实现
     * @param extension 扩展对象
     * @param priority 优先级（数值越大越优先）
     */
    virtual bool registerExtension(ExtensionPtr extension, int priority) = 0;

    /**
     * @brief 注销扩展
     */
    virtual bool unregisterExtension(ExtensionPtr extension) = 0;

    /**
     * @brief 获取所有扩展（按优先级排序）
     */
    virtual ExtensionList extensions() const = 0;

    /**
     * @brief 按条件过滤扩展
     */
    virtual ExtensionList extensions(FilterFunc filter) const = 0;

    /**
     * @brief 责任链式查找
     *
     * 示例：
     * @code
     * IEditor* editor = editorExtensionPoint->tryExtensions<IEditor*>(
     *     [&](auto editor) -> IEditor* {
     *         return editor->canHandle(filePath) ? editor : nullptr;
     *     },
     *     nullptr  // 默认值
     * );
     * @endcode
     */
    template<typename Result>
    Result tryExtensions(std::function<Result(ExtensionPtr)> handler, Result defaultValue = {})
    {
        auto extensions = this->extensions();
        for (auto ext : extensions) {
            Result result = handler(ext);
            if (result != defaultValue) {
                return result;
            }
        }
        return defaultValue;
    }

    /**
     * @brief 获取扩展的优先级
     */
    virtual int priority(ExtensionPtr extension) const
    {
        Q_UNUSED(extension)
        return -1;
    }

    /**
     * @brief 更新优先级
     */
    virtual bool setPriority(ExtensionPtr extension, int newPriority)
    {
        Q_UNUSED(extension)
        Q_UNUSED(newPriority)
        return false;
    }

    /**
     * @brief 获取扩展数量
     */
    [[nodiscard]] virtual int count() const = 0;

    /**
     * @brief 清空所有扩展
     */
    virtual void clear() = 0;
};

/**
 * @brief 扩展点注册中心 - extension facade
 * @note 扩展点是依据主程序支持的扩展接口(内部定义)而在内部创建并注册的，无必要外部不应手动注册扩展点。
 * @todo 使用命名空间 extension 替代 ExtensionRegistry 静态类
 * 
 * 职责：
 * - 管理所有扩展点的注册和查询
 * - 提供类型安全的扩展点访问
 * - 支持扩展点的生命周期管理
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
 * auto editorExtensionPoint = std::make_shared<ExtensionPoint<IEditor>>(
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
class ExtensionSystem
{
public:
    /**
     * @brief 注册扩展点
     * @param point 扩展点智能指针
     * @note 只接受基类指针
     */
    static bool registerExtensionPoint(const std::shared_ptr<ExtensionPointBase>& point);

    /**
     * @brief 注销扩展点
     * @param id 扩展点ID
     */
    static bool unregisterExtensionPoint(const QString& id);

    /**
     * @brief 解析指定ID的扩展点
     * @param id 扩展点ID
     *
     * @note 如果使用了 Q_DECLARE_INTERFACE 定义可使用 qobject_interface_iid<T>() 获取接口唯一 ID
     */
    static std::shared_ptr<ExtensionPointBase> extensionPoint(const QString& id);

    /**
     * @brief 解析指定类型接口的扩展点
     * @note 使用 Q_DECLARE_INTERFACE 定义并通过 qobject_interface_iid<T>() 获取接口唯一 ID
     */
    template<typename T>
    static std::shared_ptr<IExtensionPoint<T>> tryExtensionPoint(const QString& id)
    {
        // 类型安全转换
        return std::dynamic_pointer_cast<IExtensionPoint<T>>(ExtensionSystem::extensionPoint(id));
    }

    /**
     * @brief 检查扩展点是否存在
     * @param id 扩展点ID
     */
    static bool hasExtensionPoint(const QString& id);

    /**
     * @brief 列出所有扩展点ID
     */
    static QStringList extensionPointIds();

    /**
     * @brief 是否有扩展点
     * @param id 扩展点ID
     */
    static bool contains(const QString& id);

    /**
     * @brief 清空所有扩展点
     */
    static void clear();

    /**
     * @brief 扩展接口 IID 辅助方法
     */
    template<typename T>
    static QString IID()
    {
        return QString::fromLatin1(qobject_interface_iid<T*>());
    }
};

} // namespace bakuon::gui
