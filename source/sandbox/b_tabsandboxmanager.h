#pragma once

#include <cstdint>
#include <deque>
#include <memory>
#include <optional>
#include <unordered_map>

#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtCore/QVariantMap>
#include <QtCore/QVector>

#include "gui/b_id.h"

namespace bakuon::sandbox {

class SandboxSystem;
enum class SandboxPhase;

/// Tab 的强类型 ID，复用 gui::Id<Tag> 的编译期区分机制（同一套模式已经用在
/// CommandId/ContextId 上），避免和 sandboxId（SandboxSystem 里的裸 QString）
/// 混用出现只有运行时才暴露的错误。
using TabId = bakuon::gui::Id<struct TabIdTag>;

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
    QString pluginFilePath;
    QString sandboxRuntimeExecutable;
    QVariantMap pluginArguments;
};

/// Tab 的生命周期状态机，比 SandboxPhase 粗一个粒度——TabSandboxManager 只关心
/// "这个 Tab 现在处于哪个对 Host/UI 有意义的阶段"，SandboxPhase 里 Connecting/
/// Loading/Initializing 这些子阶段细节被折叠进 Launching。
enum class TabState {
    Queued,    // 已分配 TabId，因并发上限暂未真正 spawn() 底层沙箱进程
    Launching, // 已 spawn()，尚未到达 Running（对应 SandboxPhase 的 Connecting..Ready）
    Running,
    Faulted,   // 沙箱异常（子进程可能还没退出，也可能已经退出，见 sandboxId() 是否非空）
    Closing,   // 已调用 shutdown()，等待子进程真正退出
    Closed,    // 终态，仅在 tabIds() 返回前的极短窗口内可观察到，随后 entry 即被移除
};

[[nodiscard]] QString toString(TabState state);

/**
 * @brief Host 侧"一个标签一个进程"的编排层：把 TabId（UI/Tab 概念）与
 * sandbox::SandboxSystem 的 sandboxId（进程/IPC 概念）绑定在一起。
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
 * @note 关于"Host 崩溃重启后接管孤儿沙箱进程"：这需要 SandboxSystem 内部换成
 *       Registry 拓扑（QRemoteObjectRegistryHost）才能做到——见
 *       source/sandbox/README.md 的选型讨论和 tryAdoptOrphanedSandboxes() 的文档。
 *       在那份改动落地之前，本类按"Host 存活期间管理一批 Tab"的语义实现，
 *       Host 进程本身退出后所有映射关系随之失效（沙箱子进程目前仍然会被
 *       closeAll()/析构级联关闭，不是"孤儿"）。
 *
 * @note 线程模型：和 SandboxSystem/gui::PluginSystem 一致，只在主线程使用，
 *       内部没有加锁；跨线程访问需要调用方自己做同步。
 */
class TabSandboxManager : public QObject
{
    Q_OBJECT
public:
    /**
     * @param defaultSandboxRuntimeExecutable openTab() 不显式指定时使用的
     *        sandbox_runtime 可执行文件路径；也可以留空，每次 openTab() 单独指定。
     */
    explicit TabSandboxManager(QString defaultSandboxRuntimeExecutable = {}, QObject *parent = nullptr);
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
     * @return 新分配的 TabId；若既没有 defaultSandboxRuntimeExecutable() 也没有显式传入，
     *         返回一个 isValid()==false 的 TabId（不会创建任何条目），调用方应检查。
     */
    TabId openTab(const QString &pluginFilePath, QVariantMap pluginArguments = {},
                  QString sandboxRuntimeExecutable = {});

    /// 请求关闭一个 Tab（对已 spawn 的沙箱是异步优雅关闭，见 SandboxSupervisor::shutdown()）。
    /// 对处于 Queued 的 Tab 是同步的：直接从排队队列移除。
    bool closeTab(TabId tabId);

    /// 对崩溃（Faulted）或仍在运行的 Tab，用同一份 TabSession 重新 spawn() 一次。
    /// @note 旧沙箱实例的优雅关闭是异步的，短时间内并发数可能超出 maxConcurrentSandboxes()
    ///       一个名额，这是刻意的权衡（restart 语义优先于严格的名额计数），见类注释。
    bool restartTab(TabId tabId);

    /// 对所有仍在注册表里的 Tab 发起 closeTab()；不等待子进程真正退出。
    void closeAll();

    [[nodiscard]] TabState tabState(TabId tabId) const;
    [[nodiscard]] QString sandboxIdForTab(TabId tabId) const;
    [[nodiscard]] std::optional<TabId> tabForSandboxId(const QString &sandboxId) const;
    [[nodiscard]] std::optional<TabSession> sessionForTab(TabId tabId) const;
    [[nodiscard]] QVector<TabId> tabIds() const;
    [[nodiscard]] size_t count() const noexcept;

    /**
     * @brief [占位钩子，等 Registry 拓扑落地后实现] 尝试把当前实例不认识、但已经在
     *        Registry 上线的沙箱进程重新收编成某个 Tab。
     *
     * 目前 SandboxSystem 是点对点直连：sandboxId 对应的本地地址是本进程生成并通过命令行
     * 传给子进程的，一旦本进程（Host）退出，新启动的 Host 实例并不知道旧地址是什么，
     * 也就无从"找回"仍然存活的旧沙箱子进程。等 SandboxSystem 换成 Registry 拓扑
     * （内部持有 QRemoteObjectRegistryHost；SandboxSupervisor 通过共享的
     * QRemoteObjectNode& 而不是各自独立监听/连接）之后，新 Host 实例可以向 Registry
     * 查询"当前在线但未被认领"的 PluginSandboxControl 节点，逐个 acquire() 拿到
     * Replica；届时还需要沙箱侧在上报里带上足够的会话标识（至少是 pluginFilePath，
     * 理想情况下还有 Host 侧生成的稳定 TabSession 标识)，本类才能反查回对应的
     * TabSession、重建 TabId <-> sandboxId 映射。这个方法就是那个时机该实现的钩子，
     * 目前是文档化的空实现，不阻塞本次 TabHost 骨架交付。
     * @return 本次成功收编的 TabId 列表（目前恒为空）
     */
    [[nodiscard]] QVector<TabId> tryAdoptOrphanedSandboxes();

Q_SIGNALS:
    void tabQueued(TabId tabId);
    void tabLaunching(TabId tabId);
    void tabRunning(TabId tabId);
    void tabFaulted(TabId tabId, const QString &reason);
    void tabClosed(TabId tabId);
    void tabLogMessage(TabId tabId, int level, const QString &message);

private:
    struct Entry
    {
        TabSession session;
        TabState state = TabState::Queued;
        QString sandboxId; // Queued/Closed 状态下为空
    };

    TabId nextTabId();
    [[nodiscard]] size_t activeCount() const noexcept;
    void spawnEntry(const TabId &tabId, Entry &entry);
    void trySpawnNextQueued();
    void finalizeEntry(const TabId &tabId, Entry &entry, bool emitClosed);

    void handlePhaseChanged(const QString &sandboxId, SandboxPhase phase);
    void handleFaulted(const QString &sandboxId, const QString &reason);
    void handleLogMessage(const QString &sandboxId, int level, const QString &message);
    void handleProcessFinished(const QString &sandboxId, int exitCode);

private:
    // 声明顺序即析构顺序（反向）：m_sandboxSystem 必须放在最后声明，确保它是所有成员里
    // 第一个被析构的——它的析构链路（SandboxSystem -> shared_ptr<SandboxSupervisor> ->
    // ~SandboxSupervisor() 内部 waitForFinished() 阻塞等待时会顺带泵一次本地事件循环）
    // 完全可能在 TabSandboxManager 自身还没析构完的当口，同步重入
    // handlePhaseChanged()/handleFaulted() 等槽函数（此时 TabSandboxManager 的 QObject
    // 基类部分尚未析构，信号连接依然有效）。这些槽函数会访问 m_entries/m_sandboxIdToTab，
    // 如果这两个成员先于 m_sandboxSystem 被析构，就是访问已析构对象的悬空内存——
    // 这里的声明顺序就是专门为了避免这个坑，不要因为"看起来更整齐"而调整。
    std::unordered_map<TabId, Entry> m_entries;
    std::unordered_map<QString, TabId> m_sandboxIdToTab; // 仅覆盖已 spawn 的条目
    std::deque<TabId> m_pendingQueue;
    QString m_defaultSandboxRuntimeExecutable;
    int m_maxConcurrent  = 8;
    uint64_t m_nextSeq   = 1;
    std::unique_ptr<SandboxSystem> m_sandboxSystem;
};

} // namespace bakuon::sandbox

Q_DECLARE_METATYPE(bakuon::sandbox::TabId)
Q_DECLARE_METATYPE(bakuon::sandbox::TabState)
