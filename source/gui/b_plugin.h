#pragma once

#include <memory>

#include <QtCore/QPluginLoader>
#include <QtCore/QString>

#include <bakuon/gui/IPlugin.h>
#include <bakuon/gui/PluginContext.h>

namespace bakuon::gui {

/**
 * @brief 该类是对 IPlugin 接口的一层包装（不是 IPlugin 本身），持有一个具体的 IPlugin 实例。
 * 不做 MetaData 元数据校验，能实例化就说明插件是有效的。
 * 此类只做插件 load/unload 和运行期状态跟踪，组合内存分配 + 生命周期由 PluginBlock 管理
 * （见 b_pluginblock.h：本类不应该被单独 new/delete，只能通过 PluginBlock::create() 构造）。
 */
class Plugin
{
public:
    /**
     * @brief 动态库插件构造。
     * @param filepath 插件动态库文件路径；为空表示这是一个尚未绑定实例的占位（当前用不到，保留）。
     */
    Plugin(size_t id, QString filepath = {});

    /**
     * @brief 内置插件构造：直接绑定一个已经构造好的 IPlugin 实例，没有对应的动态库文件。
     */
    Plugin(size_t id, std::shared_ptr<IPlugin> builtin);

    ~Plugin();

    // 仅内部使用的原子自增 ID，用于快速表查询，而不是使用字符串的 IPlugin::id() 作为查询Key，但可保留支持。
    [[nodiscard]] size_t id() const { return m_id; }
    [[nodiscard]] const QString& filePath() const { return m_filepath; }
    [[nodiscard]] bool isLoaded() const { return static_cast<bool>(m_instance); }
    // 元数据和状态访问
    // 启动/关闭时间戳
    // 错误消息

    /**
     * @brief 加载插件。
     * 动态库插件：通过 QPluginLoader 加载动态库并 qobject_cast<IPlugin*>()。
     * 内置插件：构造时已经绑定了实例，这里直接返回 true（幂等）。
     * @return 成功返回 true。
     */
    bool load();

    /**
     * @brief 卸载插件。释放 IPlugin 实例；动态库插件同时卸载动态库。
     */
    bool unload();

    /**
     * @brief 调用 IPlugin::initialize()。
     * @return 转发 IPlugin::initialize() 的返回值；若尚未 load() 成功，直接返回 false。
     * @note 之前的实现会忽略 IPlugin::initialize() 的返回值、总是返回 true，
     *       与 IPlugin::initialize() 文档里"失败时插件系统应回滚"的约定矛盾，这里已修正为如实转发。
     */
    bool initialize();

    void reactExtensions();
    void quit();

    // 其他更多操作
    // ...

private:
    size_t m_id; // 仅内部使用的原子自增 ID，由外部创建时提供。
    QString m_filepath;

    // 插件实例。动态库插件：指向 QPluginLoader::instance() 返回的对象，用空操作删除器包装
    // （真正的释放通过 m_loader->unload() 完成，不能让这个 shared_ptr 去 delete 它）。
    // 内置插件：直接持有构造函数传入的实例，拥有所有权。
    std::shared_ptr<IPlugin> m_instance;

    // 插件动态库实例生命周期管理；内置插件（builtin 构造函数）时为 nullptr。
    std::unique_ptr<QPluginLoader> m_loader;
};

} // namespace bakuon::gui

// 注：不要在这里对 Plugin 声明 Q_DECLARE_INTERFACE ——Plugin 不是 QObject 派生类，
// 也不是插件需要实现的接口；真正的接口声明在 include/bakuon/gui/IPlugin.h 里的 IPlugin。
// 之前这里错误地对 Plugin 也声明了同一个 IID "com.bakuon.plugin"，已移除。
