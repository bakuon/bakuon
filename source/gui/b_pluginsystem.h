#pragma once

#include <atomic>
#include <memory>
#include <unordered_map>
#include <vector>

#include <QtCore/QObject>
#include <QtCore/QReadWriteLock>
#include <QtCore/QVector>

namespace bakuon::gui {

class PluginBlock;
class PluginDiscoverer;

/**
 * @brief 门面类 --- PluginBlock (Plugin) 插件生命周期调度
 *
 * ## 当前实现范围
 * discover* / load* / initialize* / shutdown* / unload* 系列已经和 PluginBlock/Plugin 接上，
 * 走的是"按发现顺序"（std::unordered_map 迭代顺序，不保证任何特定顺序）而不是依赖拓扑序——
 * PluginDiscoverer 的依赖解析（DFS 环检测 + Kahn 拓扑排序）还没有实现，这是已知的、
 * 有意为之的限制，不是遗漏。如果插件之间确实有依赖关系，目前只能靠调用方自己控制
 * discoverPlugin()/discoverBuiltIn() 的调用顺序来保证。
 */
class PluginSystem : public QObject
{
    Q_OBJECT
public:
    explicit PluginSystem(QObject *parent = nullptr);
    ~PluginSystem() override; // 定义放在 .cpp（out-of-line），因为 unique_ptr<PluginDiscoverer>
                              // 的析构需要 PluginDiscoverer 的完整类型，这里只有前置声明。

    // ── 注册/发现 ──────────────────────────────────────────────

    /**
     * @brief 分配一个新的、进程内唯一的插件数字 ID。
     * @note discoverBuiltIn() 需要调用方自己先用 PluginBlock::create(id, ...) 构造好 PluginBlock，
     *       这里提供统一的 id 来源，避免调用方各自发明分配策略导致冲突。
     *       discoverPlugin()/discoverPlugins()（动态库路径）内部会自动调用本方法，不需要调用方操心。
     */
    [[nodiscard]] size_t nextId();

    /**
     * @brief 注册内置插件。调用方需先用 PluginBlock::create(nextId(), builtinInstance) 构造好 block。
     * @return 成功返回该插件的数字 id（与传入 block->id() 相同）；
     *         失败（block 为空 / id 冲突 / IPlugin::id() 为空或冲突）返回 0。
     */
    size_t discoverBuiltIn(const std::shared_ptr<PluginBlock> &plugin);

    /**
     * @brief 从动态库文件发现单个插件：只解析元数据、创建 PluginBlock 并注册，不会立即 load()。
     * @return 成功返回新分配的数字 id；失败返回 0（原因见 lastError()）。
     */
    size_t discoverPlugin(const QString &filePath);

    /**
     * @brief 扫描目录批量发现插件（内部对每个候选动态库文件调用 discoverPlugin()）。
     * @return 成功发现的插件 id 列表
     */
    QVector<size_t> discoverPlugins(const QString &directory, bool recursive = false);

    // ── 生命周期 ───────────────────────────────────────────────

    /**
     * @brief 一步完成完整启动流程：loadAll() → initializeAll()。
     * @note "依赖解析"这一步目前是空的（见类头部说明），不要依赖它做依赖排序。
     * @return 全部成功返回 true；任一失败时填充 lastError() 并返回 false
     */
    bool startup();

    /**
     * @brief 停止所有插件并卸载动态库（shutdownAll() + unloadAll()）。
     */
    void shutdown();

    bool load(size_t id);
    bool load(const QString &id);
    /**
     * @brief 按当前注册顺序加载所有已发现插件的动态库。
     * @note 不保证依赖顺序，见类头部说明。
     */
    bool loadAll();

    /**
     * @brief 卸载指定插件。
     */
    bool unload(size_t id);
    /**
     * @brief 通过字符串 ID 卸载插件
     */
    bool unload(const QString &pluginId);
    bool unloadAll();

    bool initializeOne(size_t id);
    /**
     * @brief 初始化所有已加载插件；全部成功后再统一调用每个插件的 extensionsInitialized()。
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

    void pluginLoading(size_t id);
    void pluginLoaded(size_t id);
    void pluginLoadFailed(size_t id);

    void pluginInitializing(size_t id, int index, int total);
    void pluginInitialized(size_t id);
    void pluginInitializeFailed(size_t id);

    /// initialize() + extensionsInitialized() 都成功完成后触发，表示插件已完全进入可用状态。
    void pluginRunning(size_t id);
    /// shutdownOne()/shutdownAll() 调用 Plugin::quit() 之前触发。
    void pluginStopped(size_t id);

    void pluginUnloaded(size_t id);
    void pluginUnloadFailed(size_t id);

private:
    // 内部私有辅助函数：统一加锁查表逻辑，避免每个公开方法各写一份、逐渐漂移。
    [[nodiscard]] std::shared_ptr<PluginBlock> blockFor(size_t id) const;
    [[nodiscard]] size_t numericIdOf(const QString &pluginId) const;
    [[nodiscard]] PluginDiscoverer &discoverer();

    /// initializeOne()/initializeAll() 共用的实际执行逻辑，不负责 pluginInitializing 信号
    /// （那个信号需要调用方提供 index/total 上下文，两个公开方法各自负责发出）。
    bool doInitialize(size_t id);

private:
    std::unique_ptr<PluginDiscoverer> m_discoverer;

    std::atomic<size_t> m_nextId{1}; // 0 保留为“无效 / 分配失败”哨兵值

    QString m_lastError;
    mutable QReadWriteLock m_lock;

    // id → PluginBlock
    std::unordered_map<size_t, std::shared_ptr<PluginBlock>> m_entries;
    std::unordered_map<QString, std::shared_ptr<PluginBlock>> m_namedEntries;
};

} // namespace bakuon::gui
