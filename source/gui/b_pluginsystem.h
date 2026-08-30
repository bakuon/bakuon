#pragma once

#include <atomic>
#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>

#include <QtCore/QObject>
#include <QtCore/QReadWriteLock>
#include <QtCore/QSet>
#include <QtCore/QStringList>
#include <QtCore/QVector>

#include "gui/b_gui_export.h"
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
 * ## 依赖解析为什么不需要拓扑排序（Load/Initialize/Run 方向）
 * IPlugin::initialize() 的约定是不允许访问其他插件；只有 extensionsInitialized()（对应进入
 * Running）才允许跨插件交互，而 Running 本身就要求"所有插件都到达 Initialized" 才会被触发
 * （见 runAll()）。而且扩展的注册工作发生在 initialize() 阶段（同样是 IPlugin 的契约），
 * extensionsInitialized() 只是"安全地使用"这些已经注册好的扩展、不会再注册任何东西——
 * 也就是说 runAll() 调用每个插件的 run() 时，全体插件的扩展早就都注册完毕了，谁先谁后调用
 * extensionsInitialized() 不影响正确性。所以 Load/Initialize/Run 这几个阶段的先后顺序完全
 * 不影响正确性，Resolving 阶段只需要做存在性 + 循环依赖校验，不需要计算一个全局加载顺序。
 *
 * ## 但 stopAll()/unloadAll() 需要按依赖图的精确逆序
 * 停止/卸载方向不满足上面的前提：插件 A 的 shutdown() 里完全可能还要访问它依赖的插件 B
 * 注册的扩展做收尾工作，这时候 B 必须还活着——如果 B 先于 A 被 stop()/unload()，A 的
 * shutdown() 访问到的就是已经释放的扩展，直接崩溃。所以 stopAll()/unloadAll() 用
 * reverseTopologicalOrder()：依赖它的插件先停/先卸载，被依赖的插件最后处理。见那两个函数
 * 和 topologicalOrder() 的实现。
 *
 * ## registerDirectory() 为什么要在 launchAll() 里重试一次 ResolveFailed
 * launchAll() 是逐个调用每个 pipeline 的 launch()、同步跑到底（本系统不是多线程调度），
 * 所以如果插件 A 依赖插件 B、而 B 在注册顺序上排在 A 后面，A 第一次 resolve 时 B 还完全没有
 * 被发现过（不在命名表里），resolve 会失败。等所有插件都跑过一轮 launch() 之后，命名表已经
 * 收全了当前批次里所有插件的 id（哪怕它们自己的 resolve 失败了，Validated 阶段已经把 id
 * 登记进去了），这时候重试一次 resolve 就有充分信息了。详见 launchAll() 实现里的注释。
 */
class BAKUON_GUI_EXPORT PluginSystem : public QObject
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
     * @brief 设置全局命令行参数，供下发给各个插件的 PluginContext 使用。
     *
     * 约定：形如 "--plugin:<pluginId>.<rest>" 的参数只会转发给对应 id 的插件（转发前会剥掉
     * "--plugin:<pluginId>." 前缀、还原成 "--<rest>"）；不带 "--plugin:" 前缀的参数视为全局参数，
     * 原样广播给每一个插件。用一个专门的 "--plugin:" 标记区分"全局"和"限定插件"，而不是用
     * "参数里有没有点号"之类的启发式规则——像 "--log.level=debug" 这种正常的全局参数本身就可能
     * 带点号，用启发式规则筛会有误伤。
     *
     * 什么时候下发给某个插件：一旦该插件的 metadata 已知（即到达 Validated，内置插件在
     * registerBuiltIn() 时就已知；动态库插件要等 launch() 解析完 json 才知道），就会立即调用
     * pipeline->setArgumentValues()。因此 setCommandLineArguments() 最好在 launchAll() 之前调用；
     * 如果在插件已经 Validated 之后才调用，需要对相关插件手动 launch(id) 一次 handle(StartInitialize)
     * 重试，才能用上新参数（正常启动流程不会遇到这个问题）。
     *
     * 只做前缀筛选和转发，不解析 "--theme=<name>" 这类具体语法——那是插件自己的事，
     * metadata().arguments 里的 PluginArgument 目前只是插件声明"我接受哪些参数"的文档信息，
     * 还没有和这里的筛选逻辑绑定做强校验。
     */
    void setCommandLineArguments(QStringList arguments);

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
     * @note 顺序无关：扩展是在 initialize() 阶段注册完成的（IPlugin 的契约），能调用 runAll()
     *       就意味着全体插件的扩展都已经注册好了；extensionsInitialized() 只是安全地使用这些
     *       已经就绪的扩展，不需要按依赖图排序。真正需要顺序的是反方向的 stopAll()/unloadAll()。
     */
    bool runAll();

    /**
     * @brief launchAll() + runAll()。
     */
    bool startup();

    /**
     * @brief 对所有 Running/RunFailed 的插件调用 stop()。
     * @note 按依赖图的精确逆序：如果插件 A 依赖插件 B，A 会先于 B 被 stop()——A 的 shutdown()
     *       里完全可能还要访问 B 注册的扩展做收尾工作，B 必须还活着。见 reverseTopologicalOrder()。
     */
    bool stopAll();

    /**
     * @brief 对所有 Stopped 的插件调用 unload()。
     * @note 顺序同 stopAll()：依赖图的精确逆序，依赖它的插件先卸载，被依赖的插件后卸载。
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
    /// 在此之前的所有阶段（发现→...→初始化）可以按插件各自自由地自动串联，无需协调，
    /// Load/Initialize 阶段的先后顺序不影响正确性——这部分结论仍然成立。
    ///
    /// 更正：runAll() 本身也不需要拓扑排序。扩展是在 initialize() 阶段就注册完成的（同样是
    /// IPlugin 的契约），runAll() 只会在全体插件都到达 Initialized 之后才被调用，这意味着
    /// 调用任何一个 run() 之前，全体插件的扩展早就都注册好了；extensionsInitialized() 只是
    /// "安全地使用"这些已经就绪的扩展，它本身不注册任何东西，谁先谁后调用它不影响正确性。
    /// 真正需要按依赖图排序的只有反方向的 stopAll()/unloadAll()：A 的 shutdown() 里可能还在
    /// 用 B 提供的扩展做收尾，B 不能比 A 先停/先卸载。所以只有 stopAll()/unloadAll() 用
    /// reverseTopologicalOrder()；runAll() 仍然按任意顺序处理（见 idSnapshot()）。
    ///
    /// PluginPipeline::ResolveHook 的实现：存在性检查 + 循环依赖检测。
    /// TODO(可选依赖): PluginDependency::RequireType::Optional 目前和 Required 处理方式相同，
    /// 都会导致 resolve 失败；真正区分"缺失可选依赖只降级、不阻断加载"需要先确定降级后的
    /// 插件该以什么状态呈现（目前的状态机里 Resolving 只有 通过/失败 两种结果，没有"部分通过"）。
    std::optional<QString> resolveDependency(const PluginMetadata &meta) const;

    /**
     * @brief 从 startId 开始做 DFS，检测依赖图里是否存在环；假定调用方已持有读锁。
     */
    bool hasDependencyCycle(const QString &startId, QSet<QString> &visiting, QSet<QString> &visited,
                            QStringList &pathOut) const;

    /**
     * @brief 计算一个拓扑序：若插件 A 依赖插件 B，则 B 排在 A 前面。
     * @note 只考虑依赖 id 当前已在命名表里的边（Resolving 阶段已经保证走到这里的插件，
     *       它声明的 Required 依赖要么已确认存在、要么它自己没能走到 Resolved 之后）。
     *       正常情况下不会有环（Resolving 阶段的 hasDependencyCycle() 已经拦截过），万一出现
     *       未预期的环或孤立节点，剩余插件按数字 id 顺序追加在末尾、用 qWarning 报出来，
     *       不会静默丢插件或直接崩溃。
     */
    [[nodiscard]] std::vector<size_t> topologicalOrder() const;
    /// topologicalOrder() 的逆序，用于 stopAll()/unloadAll()。
    [[nodiscard]] std::vector<size_t> reverseTopologicalOrder() const;

    /// 见 setCommandLineArguments() 的文档；纯函数，不依赖任何插件的当前状态。
    [[nodiscard]] QStringList argumentsFor(const QString &pluginId) const;

private:
    std::atomic<size_t> m_nextId{1}; // 0 保留为"分配失败"哨兵值

    QString m_lastError;
    mutable QReadWriteLock m_lock;

    QStringList m_commandLineArguments; // 见 setCommandLineArguments()

    std::unordered_map<size_t, std::shared_ptr<PluginPipeline>> m_entries;
    // 只有 metadata().id 已知（Validated 之后，见 onPipelineStateChanged）才会写入这里。
    std::unordered_map<QString, std::shared_ptr<PluginPipeline>> m_namedEntries;
};

} // namespace bakuon::gui
