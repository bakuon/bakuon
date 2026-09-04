#pragma once

#include <cstdint>
#include <deque>
#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>

#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtCore/QVariantMap>
#include <QtCore/QVector>

namespace bakuon::sandbox {

class SandboxSystem;
enum class SandboxPhase;

/**
 * @brief Host 侧一个"标签页/会话"的稳定身份，与底层 sandboxId 解耦。
 *
 * restartTab() 会换新的 sandboxId，但 TabId 保持不变——这是本类存在的理由：
 * UI 层只认 Tab，不认沙箱进程。
 */
// struct TabId
// {
//     quint64 value = 0;

//     [[nodiscard]] bool isValid() const noexcept { return value != 0; }
//     [[nodiscard]] friend bool operator==(TabId a, TabId b) noexcept { return a.value == b.value; }
//     [[nodiscard]] friend bool operator!=(TabId a, TabId b) noexcept { return a.value != b.value; }
// };

/// Tab 的生命周期状态机，比 SandboxPhase 粗一个粒度——TabSandboxManager 只关心
/// "这个 Tab 现在处于哪个对 Host/UI 有意义的阶段"，SandboxPhase 里 Connecting/
/// Loading/Initializing 这些子阶段细节被折叠进 Launching。
enum class TabState {
    Queued,    // 已分配 TabId，因并发上限暂未真正 spawn() 底层沙箱进程
    Launching, // 已 spawn()，尚未到达 Running（对应 SandboxPhase 的 Connecting..Ready）
    Running,
    Closing,   // 已调用 shutdown()，等待子进程真正退出
    Faulted,   // 沙箱异常（子进程可能还没退出，也可能已经退出，见 sandboxId() 是否非空）
    Restoring, // 从会话文件里读出来的历史记录，还没等到匹配的孤儿沙箱重新出现
               // （见 restoreSession()/tryAdoptOrphanedSandboxes()），也还没被
               // respawnRestoredTab() 主动放弃等待、重新 spawn()。
};

/**
 * @brief 单个 Tab 对应的沙箱会话描述：足够重新 spawn() 一次同样的沙箱实例。
 *
 * 字段全是可平凡拷贝/序列化的值类型（没有指针、没有句柄），这是刻意的——
 * 崩溃恢复/会话持久化（重启后把这些信息写盘、下次启动再读回来）是自然的下一步，
 * 但本次不包含落盘逻辑，只是先把数据结构设计成"随时可以被序列化"的形状，
 * 不在这里引入具体的文件格式/路径决策。
 */
struct TabSession
{
    uint64_t tabId;
    QString sandboxId;
    QString pluginFilePath;
    QString sandboxRuntimeExecutable;
    TabState state = TabState::Queued;
    QVariantMap pluginArguments;
};

/**
 * @brief Host 侧"一个标签一个进程"的编排层：把 TabId（UI/Tab 概念）与
 * sandbox::SandboxSystem 的 sandboxId（进程/IPC 概念）绑定在一起。
 * @details 面向"多标签页"宿主的编排层：在 SandboxSystem 之上做并发节流、排队、
 *          重启、孤儿收编。不重复实现单个沙箱的生命周期（那是 SandboxSupervisor）。
 *
 * 与 gui::PluginSystem 之于 gui::PluginPipeline、SandboxSystem 之于
 * SandboxSupervisor 的关系类似：本类不重新实现任何单个沙箱实例的启动/通信逻辑，
 * 内部持有一个 SandboxSystem 并把它的信号转译成带 TabId 的、对 UI 更友好的事件；
 * 新增的是三件 SandboxSystem 本身不管、但"一个标签一个进程"场景必然要处理的事：
 *
 *  1. 生命周期绑定：openTab()/closeTab() 直接对应 UI 上"打开/关闭一个标签"的动作，
 *     调用方不需要自己记 TabId -> sandboxId 的映射表。
 *  2. 故障隔离：某个 Tab 对应的沙箱 Faulted，只影响这一个 Tab（tabFaulted 信号
 *     携带的是 TabId 而不是让调用方自己去反查"这个 sandboxId 是哪个标签页"）；
 *     支持 restartTab() 让用户对崩溃的标签页发起"重新加载"，无需关闭重开整个 Tab。
 *  3. 并发节流：用户可能一次性打开几十个标签，若不设上限会一次性拉起几十个子进程，
 *     见 setMaxConcurrentSandboxes()——超出上限的 openTab() 请求进入排队（Queued），
 *     等有 Tab 关闭/崩溃退出腾出名额后自动补上。
 *
 * @note "Host 崩溃重启后接管孤儿沙箱进程"：sandbox 模块已经换成 Registry 拓扑
 *       （QRemoteObjectRegistryHost，见 source/sandbox/README.md 的选型讨论），
 *       本类因此能做到"发现并重新接管"仍然存活的孤儿沙箱进程，见
 *       tryAdoptOrphanedSandboxes()。结合会话持久化（setSessionFilePath()/
 *       restoreSession()），本类现在能做到"原地恢复成同一个 Tab"：崩溃前把
 *       tabId/sandboxId/pluginFilePath 等信息写盘，重启后先按 tabId 把这些记录
 *       原样放回 m_tabs（状态为 Restoring），后续收编到匹配的孤儿 sandboxId 时，
 *       走的是"就地把 Restoring 状态的旧条目接上"而不是"分配一个全新 tabId"——
 *       调用方看到的 tabId 前后一致，UI 层不需要做任何特殊的"这其实是同一个东西"
 *       的映射。没有等到匹配孤儿的 Restoring 条目，调用方可以用
 *       respawnRestoredTab()（本质是 restartTab() 的别名）主动放弃等待、
 *       用持久化下来的 pluginFilePath 重新 spawn() 一个全新实例。
 *
 * @note 持久化范围：只持久化"重建一个 Tab 需要的最小信息"（tabId、最后已知的
 *       sandboxId、pluginFilePath、sandboxRuntimeExecutable、pluginArguments），
 *       不包含插件自己的运行时状态（比如打开的文档内容、光标位置）——那些是插件/
 *       文档层的职责，本类完全不知道也不应该知道。默认不启用持久化（构造函数不需要
 *       任何文件路径），必须显式调用 setSessionFilePath() 才会开始写盘，这是刻意的：
 *       不是所有 Host 场景都需要/想要这个行为（比如短生命周期的命令行工具场景）。
 *
 * @note 线程模型：和 SandboxSystem/gui::PluginSystem 一致，只在主线程使用，
 *       内部没有加锁；跨线程访问需要调用方自己做同步。
 */
class TabSandboxManager : public QObject
{
    Q_OBJECT
public:
    /**
     * @param sandboxRuntimeExecutable openTab() 不显式指定时使用的
     *        sandbox_runtime 可执行文件路径；也可以留空，每次 openTab() 单独指定。
     */
    explicit TabSandboxManager(QString sandboxRuntimeExecutable = {}, QObject *parent = nullptr);
    ~TabSandboxManager() override;

    TabSandboxManager(const TabSandboxManager &)            = delete;
    TabSandboxManager &operator=(const TabSandboxManager &) = delete;

    void setDefaultSandboxRuntimeExecutable(QString executable);
    [[nodiscard]] const QString &defaultSandboxRuntimeExecutable() const noexcept;

    /// 允许同时处于 Launching/Running/Closing 的沙箱实例数上限；<= 0 表示不限制。
    /// 默认值 8 只是一个保守的起始值，早期孵化阶段可以按实测情况随时调整。
    void setMaxConcurrentSandboxes(int max);
    [[nodiscard]] int maxConcurrentSandboxes() const noexcept;

    /**
     * @brief 打开一个新 Tab：登记会话信息，若未超并发上限立即 spawn()，否则排队。
     * @param pluginFilePath 要在沙箱里加载的插件动态库路径
     * @param pluginArguments 透传给插件 initialize() 的启动参数
     * @param sandboxRuntimeExecutable 留空则使用 defaultSandboxRuntimeExecutable()
     * @return 新分配的 Tab Id；若既没有 defaultSandboxRuntimeExecutable() 也没有显式传入，
     *         返回一个 isValid()==false 的 TabId（不会创建任何条目），调用方应检查。
     */
    uint64_t openTab(const QString &pluginFilePath, QVariantMap pluginArguments = {},
                     QString sandboxRuntimeExecutable = {});

    /// 请求关闭一个 Tab（对已 spawn 的沙箱是异步优雅关闭，见 SandboxSupervisor::shutdown()）。
    /// 对处于 Queued 的 Tab 是同步的：直接从排队队列移除。
    bool closeTab(uint64_t tabId);
    /// 对所有仍在注册表里的 Tab 发起 closeTab()；不等待子进程真正退出。
    void closeAll();

    /// 对崩溃（Faulted）或仍在运行的 Tab，用同一份 TabSession 重新 spawn() 一次。
    /// @note 旧沙箱实例的优雅关闭是异步的，短时间内并发数可能超出 maxConcurrentSandboxes()
    ///       一个名额，这是刻意的权衡（restart 语义优先于严格的名额计数），见类注释。
    bool restartTab(uint64_t tabId);

    [[nodiscard]] TabState tabState(uint64_t tabId) const;
    [[nodiscard]] QString sandboxIdForTab(uint64_t tabId) const;
    [[nodiscard]] std::optional<uint64_t> tabForSandboxId(const QString &sandboxId) const;
    [[nodiscard]] std::optional<TabSession> sessionForTab(uint64_t tabId) const;
    [[nodiscard]] QVector<uint64_t> tabIds() const;
    [[nodiscard]] size_t count() const noexcept;

    /**
     * @brief 把当前已经发现、但还没收编的孤儿沙箱（见 orphanSandboxAvailable 信号）
     *        全部收编。
     *
     * 每个孤儿先按 sandboxId 在 m_tabs 里找有没有 Restoring 状态、记录着同一个
     * sandboxId 的旧条目——命中就是"原地恢复"：沿用原来的 tabId，发 tabRestored；
     * 没命中就是"陌生孤儿"：分配一个全新 tabId，pluginFilePath 留空（本进程从未
     * 见过它），发 tabAdopted。两种情况 state 都直接进 Launching（底层进程其实
     * 早就跑起来了，phase 会通过正常的 onPhaseChanged() 很快同步过来）。
     *
     * 收编不受 maxConcurrentSandboxes() 节流：孤儿进程的资源已经在被占用，
     * 收编与否不影响它是否消耗系统资源，只影响本类是否知道并追踪它，
     * 因此没有理由为了"节流"而拒绝接管一个已经存在的进程。
     *
     * @return 本次成功收编（原地恢复 + 陌生收编）的 tabId 列表；没有待收编的孤儿
     *         时返回空列表。
     */
    [[nodiscard]] QVector<uint64_t> tryAdoptOrphanedSandboxes();

    // ------------------------------------------------------------------
    // 会话持久化：restoreSession() 之后，Restoring 状态的条目会等着被
    // tryAdoptOrphanedSandboxes() 就地接上（原地恢复）或者被 respawnRestoredTab()
    // 主动重新 spawn()。见类注释里对整条链路的说明。
    // ------------------------------------------------------------------

    /// 默认的会话文件路径建议值（QStandardPaths::AppDataLocation 下的固定文件名），
    /// 纯粹是给调用方省事的便利函数，不会有任何副作用（不读也不写文件），
    /// 也不会自动被使用——仍然要调用方自己传给 setSessionFilePath()。
    [[nodiscard]] static QString defaultSessionFilePath();

    /// 设置会话持久化文件路径。设置后，后续所有会改变 Tab 集合/状态的操作都会
    /// 自动把当前完整状态重写到这个文件（QSaveFile 原子写入，见 persist() 实现，
    /// 不会因为写到一半崩溃而留下损坏文件）。留空（默认）表示不持久化。
    /// @note 只影响"以后"的写入；不会触发读取，读取是 restoreSession() 的职责，
    ///       两者分开是为了让调用方可以先设置路径、连接完信号，再决定什么时候读。
    void setSessionFilePath(QString filePath);
    [[nodiscard]] const QString &sessionFilePath() const noexcept;

    /**
     * @brief 从 sessionFilePath() 指向的文件读取上一次留下的 Tab 记录，
     *        以 TabState::Restoring 状态把它们放回 m_tabs（保留原来的 tabId）。
     *
     * 只是把记录"摆回来"，不会主动 spawn()/connect 任何东西——真正"接上线"要么
     * 靠后续的 tryAdoptOrphanedSandboxes() 匹配到同一个 sandboxId（原地恢复），
     * 要么靠调用方主动调用 respawnRestoredTab()（放弃等待、重新 spawn()）。
     *
     * 通常应该在构造完成、连接完 tabRestoring 等信号之后，作为 Host 启动流程里
     * 很靠前的一步显式调用一次；文件不存在（比如全新安装/上次是正常退出、
     * 已经被清空）按 0 条记录处理，不是错误。
     *
     * @return 成功恢复放回 m_tabs 的记录条数。
     */
    int restoreSession();

    /// respawnRestoredTab() 是 restartTab() 的别名——对 Restoring 状态的 Tab 调用，
    /// 语义是"放弃等待匹配的孤儿，直接用持久化下来的 pluginFilePath 重新 spawn() 一个
    /// 全新实例"；单独起个名字只是让调用方的意图更清楚，实现完全复用 restartTab()。
    bool respawnRestoredTab(uint64_t tabId);

    /// 还有多少 Tab 停留在 Restoring 状态（既没等到匹配孤儿、也没被 respawn）。
    /// 纯只读便利函数，方便调用方实现自己的"等多久就放弃"策略——本类不内置任何
    /// 超时/自动放弃逻辑，那是 Host 层的产品决策，不是本类的职责。
    [[nodiscard]] size_t pendingRestoreCount() const noexcept;

Q_SIGNALS:
    void tabQueued(uint64_t tabId);
    void tabLaunching(uint64_t tabId);
    void tabRunning(uint64_t tabId);
    void tabClosed(uint64_t tabId);
    void tabFaulted(uint64_t tabId, const QString &reason);
    void tabLogMessage(uint64_t tabId, int level, const QString &message);
    /// 注册中心里出现了一个尚未被收编的孤儿沙箱（转发自 SandboxSystem::orphanDiscovered），
    /// 调用方可以选择立即/稍后调用 tryAdoptOrphanedSandboxes()，也可以完全不管它
    /// （比如产品决策是"崩溃恢复关掉、宁可留一个不再被任何 Tab 追踪的后台进程"）。
    void orphanSandboxAvailable(const QString &sandboxId);
    /// tryAdoptOrphanedSandboxes() 收编了一个*完全陌生*的孤儿（没有匹配上任何
    /// Restoring 记录）才会发，对应一个全新分配的 tabId。UI 层可以用它展示
    /// "检测到一个恢复的后台任务，文档信息不可用"这类提示。
    void tabAdopted(uint64_t tabId, const QString &sandboxId);
    /// restoreSession() 每从文件里放回一条记录就发一次，调用方可以立刻用这个
    /// tabId 在 UI 上摆一个"正在恢复…"的占位标签页，不需要等到真正接上孤儿。
    void tabRestoring(uint64_t tabId);
    /// tryAdoptOrphanedSandboxes() 把一个 Restoring 状态的旧记录*原地*接上了匹配的
    /// 孤儿——这才是"原地恢复成同一个 Tab"的真正完成时刻，tabId 和崩溃前完全一致。
    /// 和 tabAdopted 分开发，是为了让 UI 层能区分"恢复成功，内容都在"和
    /// "捡到一个来路不明的孤儿，内容对不上"这两种截然不同的用户提示。
    void tabRestored(uint64_t tabId, const QString &sandboxId);

private:
    uint64_t nextTabId();
    [[nodiscard]] size_t occupyingCount() const noexcept;
    void spawnSession(TabSession &session);
    void tryQueued();
    void finalizeSession(TabSession &session, bool emitClosed);

    void onPhaseChanged(const QString &sandboxId, SandboxPhase phase);
    void onFaulted(const QString &sandboxId, const QString &reason);
    void onLogMessage(const QString &sandboxId, int level, const QString &message);
    void onProcessFinished(const QString &sandboxId, int exitCode);
    void onOrphanDiscovered(const QString &sandboxId);
    /// 把当前 m_tabs 完整重写到 sessionFilePath()；sessionFilePath() 为空时是 no-op。
    /// 用 QSaveFile 原子写入，见 .cpp 实现里的说明。
    void persistSession() const;

private:
    QString m_defaultSandboxRuntimeExecutable;
    QString m_sessionFilePath; // 空表示不持久化
    int m_maxConcurrent   = 8;
    uint64_t m_nextTabSeq = 1;

    std::unordered_map<uint64_t, TabSession> m_tabs;
    std::unordered_map<QString, uint64_t> m_sandboxIdToTab; // 仅覆盖已 spawn 的条目
    std::deque<uint64_t> m_pendingQueue;
    std::vector<QString> m_pendingOrphans; // 已发现、尚未 tryAdoptOrphanedSandboxes() 的孤儿

    // 声明顺序即析构顺序（反向）：m_sandboxSystem 必须放在最后声明，确保它是所有成员里
    // 第一个被析构的——它的析构链路（SandboxSystem -> shared_ptr<SandboxSupervisor> ->
    // ~SandboxSupervisor() 内部 waitForFinished() 阻塞等待时会顺带泵一次本地事件循环）
    // 完全可能在 TabSandboxManager 自身还没析构完的当口，同步重入
    // handlePhaseChanged()/handleFaulted() 等槽函数（此时 TabSandboxManager 的 QObject
    // 基类部分尚未析构，信号连接依然有效）。这些槽函数会访问 m_tabs/m_sandboxIdToTab，
    // 如果这两个成员先于 m_sandboxSystem 被析构，就是访问已析构对象的悬空内存——
    // 这里的声明顺序就是专门为了避免这个坑，不要因为"看起来更整齐"而调整。
    std::unique_ptr<SandboxSystem> m_sandboxSystem;
};

} // namespace bakuon::sandbox

Q_DECLARE_METATYPE(bakuon::sandbox::TabState)
