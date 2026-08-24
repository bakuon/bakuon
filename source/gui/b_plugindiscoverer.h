#pragma once

#include <optional>

#include <QtCore/QHash>
#include <QtCore/QObject>
#include <QtCore/QStringList>

namespace bakuon::gui {

/**
 * @brief 单个插件的元数据（解析自 Q_PLUGIN_METADATA 内嵌的 JSON，见 include/bakuon/gui/IPlugin.h
 *        头部注释里的 "MetaData" JSON 示例）。
 * @note 只在 discover() 阶段读取 QPluginLoader::metaData()，不会触发 dlopen/instance()，
 *       因此这一步是廉价、可以对整个目录批量做的。
 */
struct PluginMetadata
{
    QString id;                // MetaData.Id，用作字符串查询 key
    QString name;              // MetaData.Name
    QString version;           // MetaData.Version
    QString compatVersion;     // MetaData.CompatVersion
    QString category;          // MetaData.Category
    QString description;       // MetaData.Description
    QString vendor;            // MetaData.Vendor
    QString copyright;         // MetaData.Copyright
    QString license;           // MetaData.License
    QString url;               // MetaData.Url
    QString platform;          // MetaData.Platform
    bool experimental = false; // MetaData.Experimental
    bool required     = false; // MetaData.Required

    QString filePath; // 来源动态库文件路径

    // MetaData.Dependencies[].Id 的原始列表，目前只是提取出来，还没有被任何地方消费。
    // TODO(依赖解析): DFS 环检测 + Kahn 拓扑排序还没有实现，PluginSystem::loadAll() 目前
    // 只按发现顺序加载，不保证依赖顺序，见 b_pluginsystem.cpp 里的对应 TODO。
    QStringList dependencyIds;
};

/**
 * @brief 插件发现器 —— 插件动态库元数据解析与拓扑依赖关系解析中心
 *
 * ## 核心职责
 *   1. **发现**：扫描动态库文件，解析 JSON 元数据（当前已实现）
 *   2. **注册内置插件**：无需 JSON，直接从 Plugin 接口读取元数据（由 PluginSystem::discoverBuiltIn() 负责，
 *      不经过本类——内置插件本来就不是动态库，没有元数据文件可解析）
 *   3. **依赖解析**：循环依赖检测（DFS）+ 拓扑排序（Kahn），返回合法加载顺序（尚未实现，见上方 TODO）
 *
 * ## 不负责
 *   - 动态库的实际加载/卸载（那是 Plugin::load()/unload() 的职责）
 *   - Plugin 生命周期（那是 PluginSystem 的职责）
 *
 * ## 扩展
 *   - watchDirectory() / unwatchDirectory()：目录实时监控（尚未实现）
 *
 * ## 线程安全
 *   所有方法应在同一线程调用（通常为主线程）。
 */
class PluginDiscoverer : public QObject
{
    Q_OBJECT
public:
    explicit PluginDiscoverer(QObject *parent = nullptr);
    ~PluginDiscoverer() override = default;

    // ── 插件发现 ───

    /**
     * @brief 解析单个动态库文件的插件元数据（不加载动态库本身）。
     * @return 成功返回 true，并可通过 metadata(filePath) 取回解析结果；
     *         文件不是有效动态库 / IID 不匹配 / MetaData.Id 缺失，均视为失败。
     */
    bool discover(const QString &filePath);

    /**
     * @brief 扫描目录，对其中每个动态库文件调用 discover()。
     * @return 成功发现的插件数量
     */
    size_t discoverDirectory(const QString &directory, bool recursive = false);

    /**
     * @brief 取回 discover() 成功解析出的元数据。
     */
    [[nodiscard]] std::optional<PluginMetadata> metadata(const QString &filePath) const;

    // ── 依赖解析 ───
    // TODO: resolveOrder() —— 输入一组 PluginMetadata，DFS 检测循环依赖 + Kahn 拓扑排序，
    // 输出合法加载顺序（或报告参与循环的插件 id 列表）。PluginSystem::loadAll()/startup()
    // 目前没有调用任何依赖排序逻辑，是已知的、有意为之的限制。

Q_SIGNALS:
    // 插件成功发现并通过元数据验证（包括内置插件注册）
    void discovered(const QString &filePath);

    // 插件发现失败（文件损坏 / IID 不匹配 / JSON 缺失必要字段等）
    void discoveryFailed(const QString &filePath, const QString &reason);

    // ── 目录监控扩展 ────────────────────────────
    /// 目录监控：检测到新增插件文件
    void pluginFileAdded(const QString &filePath);
    /// 目录监控：检测到插件文件被删除
    void pluginFileRemoved(const QString &filePath);

private:
    QStringList m_watchedDirectories;

    // filePath → 解析出的元数据；discover() 成功后写入，metadata() 从这里取。
    QHash<QString, PluginMetadata> m_metadata;
};

} // namespace bakuon::gui
