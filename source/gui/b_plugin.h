#pragma once

#include <memory>

#include <QtCore/QJsonObject>
#include <QtCore/QPluginLoader>
#include <QtCore/QString>
#include <QtCore/QVariantMap>

#include <bakuon/gui/IPlugin.h>
#include <bakuon/gui/PluginContext.h>

namespace bakuon::gui {

class PluginBlock;

/**
 * @brief 该类是对 IPlugin 接口一层包装，但接口并不相同，引用 IPlugin 的具体实例。
 * 不做 MetaData 元数据校验，能实例化就说明插件是有效的，但支持元数据访问。
 * 此类只做插件 load/unload 和运行期状态跟踪，生命周期自我管理（配合 PluginBlock）。
 */
class Plugin : public std::enable_shared_from_this<Plugin>
{
public:
    /**
     * @param filepath 如果文件路径为空，表示非动态库文件的内置插件
     */
    Plugin(size_t id, QString filepath = {});
    ~Plugin();

    // 仅内部使用的原子自增 ID，用于快速表查询，而不是使用字符串的 IPlugin::id() 作为查询Key，但可保留支持。
    [[nodiscard]] size_t id() const { return m_id; }
    // 元数据和状态访问
    // 启动/关闭时间戳
    // 错误消息

    bool load();
    bool unload();

    bool initialize();
    void reactExtensions();
    void quit();

    // 其他更多操作
    // ...

private:
    size_t m_id; // 仅内部使用的原子自增 ID，由外部创建时提供。
    QString m_filepath;

    // 强引用自身指针来“锁住”生命周期
    std::shared_ptr<Plugin> m_keepAlive;

    // 插件动态库实例
    std::shared_ptr<IPlugin> m_instance;

    // 插件动态库实例生命周期管理
    std::unique_ptr<QPluginLoader> m_loader;
};

} // namespace bakuon::gui

Q_DECLARE_INTERFACE(bakuon::gui::Plugin, "com.bakuon.plugin")
