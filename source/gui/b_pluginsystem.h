#pragma once

#include <atomic>
#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>

#include <QtCore/QObject>
#include <QtCore/QReadWriteLock>
#include <QtCore/QSet>
#include <QtCore/QVector>

#include "gui/b_pluginpipeline.h"

namespace bakuon::gui {

class IPlugin;

/**
 * @brief 门面类 —— 插件注册表 + 批量编排。
 *
 * 每个插件对应一个独立的 PluginPipeline（见 b_pluginpipeline.h），本类不重复实现任何
 * 状态机逻辑，只做三件事：
 *   1. 分配 id、创建/持有 PluginPipeline，维护"数字 id"和"字符串 id"两张查询表
 *   2. 给每个 PluginPipeline 注入依赖解析回调（跨插件的存在性检查 + 循环依赖检测，
 *      这是唯一一件单个 PluginPipeline 自己做不到、必须由持有全局视角的本类来做的事）
 *   3. 批量编排：launchAll()/runAll()/stopAll()/unloadAll()，本质上就是对着已注册的
 *      pipeline 挨个调用它们自己的 launch()/run()/stop()/unloadNow()
 *
 * ## 依赖解析为什么不需要拓扑排序
 * IPlugin::initialize() 的约定是不允许访问其他插件；只有 extensionsInitialized()（对应进入
 * Running）才允许跨插件交互，而 Running 本身就要求"所有插件都到达 Initialized" 才会被触发
 * （见 runAll()）。也就是说 Load/Initialize 阶段的先后顺序完全不影响正确性，Resolving 阶段
 * 只需要做存在性 + 循环依赖校验，不需要计算一个全局加载顺序。
 *
 * ## registerDirectory() 为什么要在 launchAll() 里重试一次 ResolveFailed
 * launchAll() 是逐个调用每个 pipeline 的 launch()、同步跑到底（本系统不是多线程调度），
 * 所以如果插件 A 依赖插件 B、而 B 在注册顺序上排在 A 后面，A 第一次 resolve 时 B 还完全没有
 * 被发现过（不在命名表里），resolve 会失败。等所有插件都跑过一轮 launch() 之后，命名表已经
 * 收全了当前批次里所有插件的 id（哪怕它们自己的 resolve 失败了，Validated 阶段已经把 id
 * 登记进去了），这时候重试一次 resolve 就有充分信息了。详见 launchAll() 实现里的注释。
 */
class PluginSystem : public QObject
{
    Q_OBJECT
public:
    explicit PluginSystem(QObject *parent = nullptr);
    ~PluginSystem() override;

    // ── 注册/注销 ──

    /**
     * @brief 注册内建实例
     * @details 只创建 pipeline，不驱动它跑起来；跑起来是 launchAll()/launch(id) 的事
     * @return 新分配的数字 id；instance 为空时返回 0。
     */
    size_t registerBuiltIn(std::shared_ptr<IPlugin> instance);

    /**
     * @brief 注册动态库文件
     * @return 新分配的数字 id。文件是否真的合法要等 launch() 才知道，这里只是登记。
     */
    size_t registerFile(const QString &filePath);

    /**
     * @brief 批量注册动态库文件
     * @return 扫描目录发现的所有候选动态库文件对应的 id 列表（同样只登记，不 launch）。
     */
    QVector<size_t> registerDirectory(const QString &directory, bool recursive = false);

    /**
     * @brief 从注册表中彻底移除一个插件、释放它的 PluginPipeline。
     * @note 只有处于 Unloaded 态才允许移除（避免误删还在使用中的插件）；unloadNow()/unloadAll()
     *       本身不会自动做这一步——卸载后仍然可以查询 pipeline(id) 的最终状态/lastError() 用于
     *       诊断，是否要真正释放交给调用方自己决定。
     * @return 成功返回 true；id 不存在或还没到 Unloaded 返回 false（原因见 lastError()）。
     */
    bool unregisterPlugin(size_t id);

    /**
     * @brief 批量清理所有当前处于 Unloaded 态的插件。
     * @return 实际清理掉的数量。
     */
    size_t unregisterUnloaded();

    // ── 批量编排 ───────────────────────────────────────────────

    /**
     * @brief 对所有已注册插件调用 launch()，外加一次 ResolveFailed 重试（见类头部说明）。
     * @return 全部插件都成功跑到 Initialized（或更后面）才是 true；个别失败不影响其它插件继续。
     */
    bool launchAll();

    /**
     * @brief 对所有当前处于 Initialized 的插件调用 run()。没到 Initialized 的插件直接跳过
     * @return （不计入失败——它有自己的失败状态可查），不会因为某一个没就绪就卡住其它插件运行。
     */
    bool runAll();

    /**
     * @brief launchAll() + runAll()。
     */
    bool startup();

    /**
     * @brief 对所有 Running/RunFailed 的插件调用 stop()（按注册顺序的逆序，近似"后启动先停止"）。
     */
    bool stopAll();

    /**
     * @brief 对所有 Stopped 的插件调用 unloadNow()（同样逆序）
     */
    bool unloadAll();

    /**
     * @brief stopAll() + unloadAll()
     */
    void shutdown();

    // ── 单个编排 ───────────────────────────────────────────────

    bool launch(size_t id);
    bool run(size_t id);
    bool stop(size_t id);
    bool unloadNow(size_t id);

    // ── 查询 ───────────────────────────────────────────────────

    [[nodiscard]] bool hasPlugin(size_t id) const;
    [[nodiscard]] bool hasPlugin(const QString &pluginId) const;
    [[nodiscard]] size_t pluginCount() const;
    [[nodiscard]] std::shared_ptr<PluginPipeline> pipeline(size_t id) const;
    [[nodiscard]] std::shared_ptr<PluginPipeline> pipeline(const QString &pluginId) const;
    [[nodiscard]] std::vector<std::shared_ptr<PluginPipeline>> pipelines() const;

    [[nodiscard]] QString lastError() const { return m_lastError; }

Q_SIGNALS:
    void pluginStateChanged(size_t id, PluginState state);
    void pluginRunning(size_t id);
    void pluginFailed(size_t id, PluginState failedState, const QString &reason);

private:
    size_t nextId();
    [[nodiscard]] std::vector<size_t> idSnapshot() const;

    void registerPipeline(size_t id, const std::shared_ptr<PluginPipeline> &pipeline);
    void onPipelineStateChanged(size_t id, PluginState state);

    /// 自动链式执行在 Initialized 阶段停止，不会进入 Running 阶段。
    /// 根据 IPlugin::initialize() 的契约规定，插件在initialize() 过程
    /// 中不得与其他插件交互——跨插件的协作必须留待 extensionsInitialized()
    /// 阶段进行。因此，运行（即调用extensionsInitialized()）必须是一个由
    /// 主机触发、受批量控制的转换过程，而不是由某个独立的管道自行决定。
    /// 在此之前的所有阶段（发现→...→初始化）可以按插件各自自由地自动串联，无需协调。
    /// 这意味着实际上并不需要拓扑加载顺序，只需要进行验证（依赖是否存在？是否有循环？）。
    /// 因为插件在 initialize()过程中，没有任何操作会依赖于其他插件，而“运行”状态会在
    /// 所有插件都达到“已初始化”后才统一开启，因此依赖插件之间的加载/初始化顺序对正确性
    /// 没有影响。依赖解析只需捕获配置错误（如缺失或循环依赖），而无需安排执行顺序。
    /// 这实际上是个好消息——它解答了以前遗留的拓扑排序待办事项，并且自然地得出结论，
    /// 而无需使用 Kahn 算法。
    /**
     * @details PluginPipeline::ResolveHook 的实现：存在性检查 + 循环依赖检测。
     * TODO(可选依赖): PluginDependency::RequireType::Optional 目前和 Required 处理方式相同，
     * 都会导致 resolve 失败；真正区分"缺失可选依赖只降级、不阻断加载"需要先确定降级后的
     * 插件该以什么状态呈现（目前的状态机里 Resolving 只有 通过/失败 两种结果，没有"部分通过"）。 
     */
    std::optional<QString> resolveDependency(const PluginMetadata &meta) const;

    /**
     * @brief 从 startId 开始做 DFS，检测依赖图里是否存在环；假定调用方已持有读锁。
     */
    bool hasDependencyCycle(const QString &startId, QSet<QString> &visiting, QSet<QString> &visited,
                            QStringList &pathOut) const;

private:
    std::atomic<size_t> m_nextId{1}; // 0 保留为"分配失败"哨兵值

    QString m_lastError;
    mutable QReadWriteLock m_lock;

    std::unordered_map<size_t, std::shared_ptr<PluginPipeline>> m_entries;
    // 只有 metadata().id 已知（Validated 之后，见 onPipelineStateChanged）才会写入这里。
    std::unordered_map<QString, std::shared_ptr<PluginPipeline>> m_namedEntries;
};

} // namespace bakuon::gui
