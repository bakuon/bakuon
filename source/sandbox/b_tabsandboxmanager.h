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
    Closing, // 已调用 shutdown()，等待子进程真正退出
    Faulted, // 沙箱异常（子进程可能还没退出，也可能已经退出，见 sandboxId() 是否非空）
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
 *       tryAdoptOrphanedSandboxes()。但这只解决了 QtRO 连接层面的重新发现——
 *       "这个孤儿原本对应哪个 Tab、是哪个文档"这层业务身份，只存在于上一个 Host
 *       进程的内存里，没有跨进程持久化，本类目前也没有；因此收编回来的孤儿会作为
 *       *新* Tab（携带 tabAdopted 信号）出现，而不是"原地恢复"成它崩溃前对应的
 *       那个 TabId。真正做到原地恢复需要一份跨 Host 进程生命周期的会话持久化
 *       （TabSession 已经设计成随时可序列化的形状，就是为了给这一步铺路），
 *       这次不包含落盘逻辑，是有意为之的范围收窄，不是遗漏。
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
     *        全部收编成新 Tab。
     *
     * 每收编一个孤儿：分配一个新 TabId（不是它崩溃前的那个——本类/本进程都不知道
     * 那个身份是什么，见类注释里对这一点的说明），Entry.session 里 pluginFilePath
     * 留空、sandboxRuntimeExecutable 用 defaultSandboxRuntimeExecutable()（仅用于
     * 万一之后 restartTab()——但 restartTab() 用空 pluginFilePath 重新 spawn()
     * 基本没有意义，调用方如果关心这一点，应该在收到 tabAdopted 后自行用别的渠道
     * 把 pluginFilePath 补全，见 sessionForTab() 可以读到当前会话），state 直接进
     * Launching（底层进程其实早就跑起来了，phase 会通过正常的 handlePhaseChanged()
     * 很快同步过来，不需要特殊处理）。
     *
     * 收编不受 maxConcurrentSandboxes() 节流：孤儿进程的资源已经在被占用，
     * 收编与否不影响它是否消耗系统资源，只影响本类是否知道并追踪它，
     * 因此没有理由为了"节流"而拒绝接管一个已经存在的进程。
     *
     * @return 本次成功收编、新分配的 id 列表；没有待收编的孤儿时返回空列表。
     */
    [[nodiscard]] QVector<uint64_t> tryAdoptOrphanedSandboxes();

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
    /// tryAdoptOrphanedSandboxes() 每成功收编一个孤儿就发一次，UI 层可以用它
    /// 区分"这是用户主动 openTab() 打开的"还是"这是恢复回来的孤儿"，展示不同的
    /// 提示（比如"检测到一个恢复的后台任务，文档信息不可用"）。
    void tabAdopted(uint64_t tabId, const QString &sandboxId);

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

private:
    QString m_defaultSandboxRuntimeExecutable;
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
