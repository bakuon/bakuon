#pragma once

#include <QtCore/QObject>
#include <QtCore/QPluginLoader>
#include <QtCore/QReadWriteLock>

namespace bakuon::gui {

class PluginBlock;
class PluginDiscoverer;

/**
 * @brief 门面类 --- PluginBlock (Plugin) 插件生命周期调度
 */
class PluginSystem : public QObject
{
    Q_OBJECT
public:
    explicit PluginSystem(QObject *parent = nullptr);
    ~PluginSystem() override = default;

    // ── 注册/发现 ──────────────────────────────────────────────

    /**
     * @brief 注册内置插件
     * @return 成功返回有效实体；失败返回 Entity::null
     */
    size_t discoverBuiltIn(std::shared_ptr<PluginBlock> plugin);

    /**
     * @brief 从动态库文件发现单个插件
     * @return 成功返回有效实体；失败返回 Entity::null
     */
    size_t discoverPlugin(const QString &filePath);

    /**
     * @brief 扫描目录批量发现插件
     * @return 成功发现的实体列表
     */
    QVector<size_t> discoverPlugins(const QString &directory, bool recursive = false);

    // ── 生命周期 ───────────────────────────────────────────────

    /**
     * @brief 一步完成完整启动流程：依赖解析 → 加载 → 初始化 → 运行
     * @return 全部成功返回 true；任一失败时填充 lastError() 并返回 false
     */
    bool startup();

    /**
     * @brief 按反向依赖顺序停止所有运行中插件，并卸载动态库
     */
    void shutdown();

    bool load(size_t id);
    bool load(const QString &id);
    /**
     * @brief 按拓扑顺序加载所有已解析插件的动态库
     * @note  需先检查依赖关系
     */
    bool loadAll();

    /**
     * @brief 卸载指定插件（需已处于非运行状态）
     */
    bool unload(size_t id);
    /**
     * @brief 通过字符串 ID 卸载插件
     */
    bool unload(const QString &pluginId);
    bool unloadAll();

    bool initializeOne(size_t id);
    /**
     * @brief 初始化所有已加载插件
     * @note  需先调用 loadAll()
     */
    bool initializeAll();
    /**
     * @brief 检查插件是否已初始化
     */
    bool isInitialized(size_t id) const;
    /**
     * @brief 检查所有插件是否都已初始化
     */
    bool isAllInitialized() const;

    void shutdownOne(size_t id);
    void shutdownAll();

    // ── 访问插件数据 ───────────────────────────────────────────────

    [[nodiscard]] bool hasPlugin(size_t id) const;
    [[nodiscard]] bool hasPlugin(const QString &id) const;
    [[nodiscard]] size_t pluginCount() const;
    [[nodiscard]] std::vector<std::shared_ptr<PluginBlock>> plugins() const;
    [[nodiscard]] std::shared_ptr<PluginBlock> plugin(size_t id) const;
    [[nodiscard]] std::shared_ptr<PluginBlock> plugin(const QString &id) const;

    [[nodiscard]] QString lastError() const { return m_lastError; }

Q_SIGNALS:
    void pluginDiscovered(const QString &filePath);
    void pluginDiscoveryFailed(const QString &filePath, const QString &reason);

    // 使用 QString 的 id ?
    void pluginLoading(size_t id);
    void pluginLoaded(size_t id);
    void pluginLoadFailed(size_t id);

    void pluginInitializing(size_t id, int index, int total);
    void pluginInitialized(size_t id);
    void pluginInitializeFailed(size_t id);

    void pluginRunning(size_t id);
    void pluginStopped(size_t id);

    void pluginUnloaded(size_t id);
    void pluginUnloadFailed(size_t id);

private:
    // 内部私有辅助函数

private:
    std::unique_ptr<PluginDiscoverer> m_discoverer;

    QString m_lastError;
    mutable QReadWriteLock m_lock;

    // id → PluginBlock
    std::unordered_map<size_t, std::shared_ptr<PluginBlock>> m_entries;
    std::unordered_map<QString, std::shared_ptr<PluginBlock>> m_namedEntries;
};

} // namespace bakuon::gui
