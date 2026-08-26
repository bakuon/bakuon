#pragma once

#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

/**
 * @file IExtensionPoint.h
 * @brief 通用扩展点框架
 *
 * 设计要点（开闭原则 OCP）：
 *  - 对扩展开放：
 *      1) 任意接口 T 都可通过 ExtensionSystem::registerExtensionPoint 注册一个扩展点；
 *      2) 插件只需实现接口 T 并调用 registerExtension 即可，框架对新增接口零修改；
 *      3) 可以通过继承 IExtensionPoint<T> 自定义存储/分发策略（如带权限控制、延迟创建等），
 *         框架侧无需改动。
 *  - 对修改关闭：
 *      ExtensionPointBase / IExtensionPoint<T> 抽象层稳定，注册中心 ExtensionSystem 只依赖
 *      抽象接口，具体存储策略由派生类决定。
 *
 * 线程模型：
 *  - 读写分离：写操作用 std::unique_lock，读操作用 std::shared_lock；
 *  - 返回快照：extensions() 返回 vector<shared_ptr<T>> 的副本，调用方在遍历期间不会
 *    因并发注册/注销而失效；
 *  - tryExtensions 内部在快照上遍历，无锁段遍历，保证异常安全。
 *
 * 若项目使用 Qt，可使用 qobject_interface_iid<T*>() 作为 id，
 * 也可通过 BAKUON_DECLARE_EXTENSION_IID 宏声明纯 C++ IID。
 */

/**
+--------------------------------------------------------+
|                         Host                           |
|  +---------------------+      +---------------------+  |
|  |      Logic Slot     | ---> |   ExtensionSystem   |  |
|  +---------------------+      +---------------------+  |
+-----------|-----------------------------^--------------+
            | Call Abstract               | Dynamic
            | Interface                   | Registration
+-----------|-----------------------------|--------------+
|           v            Plugins          |              |
|  +---------------------------+          |              |
|  |    IExtension Interface   | <--------+              |
|  +---------------------------+                         |
|            ^                                           |
|            | (Implement)                               |
|  +----------------------------+                        |
|  | Concrete Realization A, B  |                        |
|  +----------------------------+                        |
+--------------------------------------------------------+
*/

namespace bakuon::gui {

/* ============================================================
 *  IID traits：可移植的接口唯一标识符机制
 * ============================================================*/

/**
 * @brief 扩展接口 IID traits
 * @tparam T 扩展接口类型
 *
 * 使用方式：
 *  - 方式一（推荐，纯 C++）：使用 BAKUON_DECLARE_EXTENSION_IID 宏在命名空间内特化；
 *  - 方式二（Qt 环境）：手动特化 extension_iid<T>::value()，返回 qobject_interface_iid<T*>()；
 *  - 方式三：注册扩展点时显式传入 id 字符串，不依赖该 traits。
 *
 * 默认实现返回空串，提示用户必须显式声明或传入 id。
 */
template<typename T>
struct extension_iid
{
    /**
     * @brief 返回接口 T 的唯一 IID
     * @return 若未特化则返回空 std::string_view
     */
    static constexpr std::string_view value() noexcept { return {}; }
};

/**
 * @brief 声明扩展接口的 IID（纯 C++，不依赖 Qt）
 * @param Type       接口类型（完全限定名，如 ::bakuon::gui::IEditor）
 * @param IidString  唯一标识符字符串，推荐反向域名格式
 *
 * 该宏必须在全局作用域或与 Type 同命名空间中使用。
 *
 * 示例：
 * @code
 *   namespace myapp { class IEditor { ... }; }
 *   BAKUON_DECLARE_EXTENSION_IID(::myapp::IEditor, "com.myapp.IEditor")
 * @endcode
 */
#define BAKUON_DECLARE_EXTENSION_IID(Type, IID) \
    template<> \
    struct bakuon::gui::extension_iid<Type> \
    { \
        static constexpr std::string_view value() noexcept { return IID; } \
    };

/* ============================================================
 *  ExtensionPointBase：类型擦除基类
 * ============================================================*/

/**
 * @brief 扩展点类型擦除基类
 *
 * 仅描述"一个扩展点"这一抽象概念，不暴露具体接口 T。注册中心以该基类
 * 的 shared_ptr 存储，使不同 T 的扩展点可以共存于同一容器。
 */
class ExtensionPointBase
{
public:
    ExtensionPointBase()          = default;
    virtual ~ExtensionPointBase() = default;

    ExtensionPointBase(const ExtensionPointBase&)            = delete;
    ExtensionPointBase& operator=(const ExtensionPointBase&) = delete;

    /**
     * @brief 扩展点唯一标识符 IID
     * @example "com.bakuon.extension.IEditor"
     */
    [[nodiscard]] virtual std::string id() const = 0;

    /**
     * @brief 扩展点描述信息
     */
    [[nodiscard]] virtual std::string description() const = 0;
};

/* ============================================================
 *  IExtensionPoint<T>：强类型扩展点抽象接口
 * ============================================================*/

/**
 * @brief 强类型扩展点接口
 * @tparam T 扩展接口类型
 * @note 扩展点是一个"能力槽位"，定义了主程序需要什么样的扩展
 *
 * 扩展点（Extension Point）是插件架构中的反向依赖注入机制：
 *  - 传统依赖：主程序 → 具体实现（硬编码）
 *  - 扩展点：  主程序 → 抽象接口 ← 插件实现（动态注册）
 *
 * 核心思想：主程序定义"能力槽位"（扩展点），插件填充具体实现；主程序对修改
 * 关闭、对扩展开放。
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
 * // 定义编辑器扩展点
 * using EditorExtension = IExtensionPoint<Editor>;
 * // 获取扩展点
 * EditorExtension extensionPoint = ExtensionSystem::extensionPoint<Editor>();
 * // 向扩展点注册扩展（插件）
 * extensionPoint->registerExtension(std::make_shared<MyEditor>());
 */
template<typename T>
class IExtensionPoint : public ExtensionPointBase
{
public:
    using ExtensionPtr  = std::shared_ptr<T>;
    using ExtensionList = std::vector<ExtensionPtr>;
    using FilterFunc    = std::function<bool(const ExtensionPtr&)>;

    ~IExtensionPoint() override = default;

    /**
     * @brief 扩展点唯一标识符
     * @example "com.bakuon.extension.IEditor"
     */
    [[nodiscard]] std::string id() const override = 0;

    /**
     * @brief 扩展点描述
     */
    [[nodiscard]] std::string description() const override = 0;

    /**
     * @brief 注册扩展实现
     * @param extension 扩展对象
     * @param priority  优先级（数值越大越优先；并列优先级按注册先后）
     * @return true 注册成功；false 若 extension 为空或已存在
     */
    virtual bool registerExtension(ExtensionPtr extension, int priority) = 0;

    /**
     * @brief 注销扩展
     * @param extension 待注销的扩展对象
     * @return true 注销成功；false 未找到
     */
    virtual bool unregisterExtension(const ExtensionPtr& extension) = 0;

    /**
     * @brief 获取所有扩展（按优先级降序排列，同优先级按注册顺序）
     * @return 扩展列表的线程安全快照
     */
    [[nodiscard]] virtual ExtensionList extensions() const = 0;

    /**
     * @brief 按条件过滤扩展
     * @param filter 过滤谓词
     * @return 满足条件的扩展列表（同样按优先级排序）
     */
    [[nodiscard]] virtual ExtensionList extensions(FilterFunc filter) const = 0;

    /**
     * @brief 责任链式查找：依次尝试每个扩展，返回第一个"非默认值"的结果
     * @tparam Result 处理结果类型
     * @param handler       对每个扩展执行的回调；返回值不等于 defaultValue 即命中
     * @param defaultValue  未命中时返回的默认值
     * @return 第一个命中的结果，或 defaultValue
     *
     * 示例：
     * @code
     * IEditor* editor = editorPoint->tryExtensions<IEditor*>(
     *     [&](const auto& ed) -> IEditor* {
     *         return ed->canHandle(path) ? ed.get() : nullptr;
     *     },
     *     nullptr
     * );
     * @endcode
     */
    template<typename Result, typename Handler>
    Result tryExtensions(Handler&& handler, Result defaultValue = {}) const
    {
        // 在快照上遍历，避免遍历时持锁导致回调中再次取锁产生死锁
        const auto snapshot = this->extensions();
        for (const auto& ext : snapshot) {
            Result r = std::invoke(std::forward<Handler>(handler), ext);
            if (r != defaultValue) {
                return r;
            }
        }
        return defaultValue;
    }

    /**
     * @brief non-const 重载，允许 handler 接收非 const ExtensionPtr&（兼容旧接口）
     */
    template<typename Result, typename Handler>
    Result tryExtensions(Handler&& handler, Result defaultValue = {})
    {
        const auto snapshot = this->extensions();
        for (const auto& ext : snapshot) {
            Result r = std::invoke(std::forward<Handler>(handler), ext);
            if (r != defaultValue) {
                return r;
            }
        }
        return defaultValue;
    }

    /**
     * @brief 获取指定扩展的优先级
     * @param extension 扩展对象
     * @return 优先级；若未找到返回 -1
     */
    [[nodiscard]] virtual int priority(const ExtensionPtr& extension) const = 0;

    /**
     * @brief 更新扩展优先级
     * @param extension   扩展对象
     * @param newPriority 新优先级
     * @return true 更新成功；false 未找到
     */
    virtual bool setPriority(const ExtensionPtr& extension, int newPriority) = 0;

    /**
     * @brief 扩展数量
     */
    [[nodiscard]] virtual std::size_t count() const = 0;

    /**
     * @brief 清空所有扩展
     */
    virtual void clear() = 0;
};

/* ============================================================
 *  RAII 注册守卫：方便插件生命周期管理
 * ============================================================ */

/**
 * @brief ExtensionRegistrar 扩展注册守卫：构造时注册，析构时自动注销，异常安全
 * @tparam T 接口类型
 *
 * 示例：
 * @code
 *   auto ep = ExtensionSystem::extensionPoint<IEditor>();
 *   ExtensionRegistrar<IEditor> guard(ep, std::make_shared<TextEditor>(), 100);
 * @endcode
 */
template<typename T>
class ExtensionRegistrar
{
public:
    using ExtensionPtr = IExtensionPoint<T>::ExtensionPtr;

    ExtensionRegistrar() = default;

    /**
     * @brief 构造并立即注册
     * @param point     目标扩展点
     * @param extension 扩展实现
     * @param priority  优先级
     */
    ExtensionRegistrar(std::shared_ptr<IExtensionPoint<T>> point, ExtensionPtr extension,
                       int priority = 0)
        : m_point(std::move(point))
        , m_extension(std::move(extension))
    {
        if (m_point && m_extension) {
            m_registered = m_point->registerExtension(m_extension, priority);
        }
    }

    ~ExtensionRegistrar() { reset(); }

    ExtensionRegistrar(const ExtensionRegistrar&)            = delete;
    ExtensionRegistrar& operator=(const ExtensionRegistrar&) = delete;

    ExtensionRegistrar(ExtensionRegistrar&& o) noexcept
        : m_point(std::move(o.m_point))
        , m_extension(std::move(o.m_extension))
        , m_registered(o.m_registered)
    {
        o.m_registered = false;
    }

    ExtensionRegistrar& operator=(ExtensionRegistrar&& o) noexcept
    {
        if (this != &o) {
            reset();
            m_point        = std::move(o.m_point);
            m_extension    = std::move(o.m_extension);
            m_registered   = o.m_registered;
            o.m_registered = false;
        }
        return *this;
    }

    /**
     * @brief 手动注销并清空持有的引用
     */
    void reset()
    {
        if (m_registered && m_point && m_extension) {
            m_point->unregisterExtension(m_extension);
        }
        m_registered = false;
        m_extension.reset();
        m_point.reset();
    }

    /**
     * @brief 是否处于已注册状态
     */
    [[nodiscard]] bool isRegistered() const noexcept { return m_registered; }

private:
    std::shared_ptr<IExtensionPoint<T>> m_point;
    ExtensionPtr m_extension;
    bool m_registered = false;
};

} // namespace bakuon::gui
