#include "sandbox/b_tabsandboxmanager.h"

#include <QtCore/QDebug>

#include "sandbox/b_sandboxsupervisor.h"
#include "sandbox/b_sandboxsystem.h"

namespace bakuon::sandbox {

QString toString(TabState state)
{
    switch (state) {
    case TabState::Queued    : return QStringLiteral("Queued");
    case TabState::Launching : return QStringLiteral("Launching");
    case TabState::Running   : return QStringLiteral("Running");
    case TabState::Faulted   : return QStringLiteral("Faulted");
    case TabState::Closing   : return QStringLiteral("Closing");
    case TabState::Closed    : return QStringLiteral("Closed");
    default                  : break;
    }
    return QStringLiteral("<unknown TabState>");
}

TabSandboxManager::TabSandboxManager(QString defaultSandboxRuntimeExecutable, QObject *parent)
    : QObject(parent)
    , m_defaultSandboxRuntimeExecutable(std::move(defaultSandboxRuntimeExecutable))
    // 不把 this 传给 SandboxSystem 的构造参数（QObject parent）——本类用
    // std::unique_ptr 独占管理它的生命周期，两套所有权机制叠在同一个对象上
    // 会导致析构时被删两次，这里刻意保持 SandboxSystem 是"无父对象"的。
    , m_sandboxSystem(std::make_unique<SandboxSystem>())
{
    connect(m_sandboxSystem.get(), &SandboxSystem::sandboxPhaseChanged, this,
            &TabSandboxManager::handlePhaseChanged);
    connect(m_sandboxSystem.get(), &SandboxSystem::sandboxFaulted, this, &TabSandboxManager::handleFaulted);
    connect(m_sandboxSystem.get(), &SandboxSystem::sandboxLogMessage, this,
            &TabSandboxManager::handleLogMessage);
    connect(m_sandboxSystem.get(), &SandboxSystem::sandboxProcessFinished, this,
            &TabSandboxManager::handleProcessFinished);
}

TabSandboxManager::~TabSandboxManager()
{
    // 不在析构里做优雅关闭（那是异步的，析构函数等不起）：直接让 m_sandboxSystem
    // 的析构（继而 SandboxSupervisor 的析构）负责强制终止仍在跑的子进程，
    // 语义与 SandboxSystem 自己的析构行为保持一致，调用方如果需要优雅关闭，
    // 应该在析构之前主动调用 closeAll() 并等待 tabClosed 信号。
}

void TabSandboxManager::setDefaultSandboxRuntimeExecutable(QString executable)
{
    m_defaultSandboxRuntimeExecutable = std::move(executable);
}

const QString &TabSandboxManager::defaultSandboxRuntimeExecutable() const noexcept
{
    return m_defaultSandboxRuntimeExecutable;
}

void TabSandboxManager::setMaxConcurrentSandboxes(int max)
{
    m_maxConcurrent = max;
    trySpawnNextQueued();
}

int TabSandboxManager::maxConcurrentSandboxes() const noexcept
{
    return m_maxConcurrent;
}

TabId TabSandboxManager::nextTabId()
{
    return TabId(QStringLiteral("tab-%1").arg(m_nextSeq++));
}

size_t TabSandboxManager::activeCount() const noexcept
{
    size_t n = 0;
    for (const auto &[id, entry] : m_entries) {
        if (entry.state == TabState::Launching || entry.state == TabState::Running ||
            entry.state == TabState::Closing) {
            ++n;
        }
    }
    return n;
}

TabId TabSandboxManager::openTab(const QString &pluginFilePath, QVariantMap pluginArguments,
                                  QString sandboxRuntimeExecutable)
{
    if (sandboxRuntimeExecutable.isEmpty()) {
        sandboxRuntimeExecutable = m_defaultSandboxRuntimeExecutable;
    }
    if (sandboxRuntimeExecutable.isEmpty()) {
        qWarning() << "TabSandboxManager::openTab: 未指定 sandboxRuntimeExecutable，"
                      "且 defaultSandboxRuntimeExecutable() 也是空的，拒绝打开新 Tab。";
        return TabId{}; // isValid() == false
    }

    const TabId tabId = nextTabId();
    Entry entry;
    entry.session.pluginFilePath           = pluginFilePath;
    entry.session.sandboxRuntimeExecutable = sandboxRuntimeExecutable;
    entry.session.pluginArguments          = std::move(pluginArguments);
    entry.state                            = TabState::Queued;

    auto [it, inserted] = m_entries.emplace(tabId, std::move(entry));
    Q_ASSERT(inserted);
    Q_UNUSED(inserted);

    if (m_maxConcurrent <= 0 || activeCount() < static_cast<size_t>(m_maxConcurrent)) {
        spawnEntry(tabId, it->second);
    } else {
        m_pendingQueue.push_back(tabId);
        Q_EMIT tabQueued(tabId);
    }
    return tabId;
}

void TabSandboxManager::spawnEntry(const TabId &tabId, Entry &entry)
{
    const QString sandboxId =
        m_sandboxSystem->spawn(entry.session.pluginFilePath, entry.session.sandboxRuntimeExecutable,
                               entry.session.pluginArguments);
    entry.sandboxId = sandboxId;
    entry.state     = TabState::Launching;
    m_sandboxIdToTab.emplace(sandboxId, tabId);
    Q_EMIT tabLaunching(tabId);
}

void TabSandboxManager::trySpawnNextQueued()
{
    while (!m_pendingQueue.empty() &&
           (m_maxConcurrent <= 0 || activeCount() < static_cast<size_t>(m_maxConcurrent))) {
        const TabId tabId = m_pendingQueue.front();
        m_pendingQueue.pop_front();

        auto it = m_entries.find(tabId);
        if (it == m_entries.end() || it->second.state != TabState::Queued) {
            // Tab 在排队期间被 closeTab() 掉了，条目已经不在了（见 closeTab 对 Queued 分支
            // 的处理），跳过即可。
            continue;
        }
        spawnEntry(tabId, it->second);
    }
}

bool TabSandboxManager::closeTab(TabId tabId)
{
    auto it = m_entries.find(tabId);
    if (it == m_entries.end()) {
        return false;
    }
    Entry &entry = it->second;

    switch (entry.state) {
    case TabState::Queued: {
        // 还没真正 spawn()，直接从排队队列和条目表里摘掉，没有子进程需要等待退出。
        auto qit = std::find(m_pendingQueue.begin(), m_pendingQueue.end(), tabId);
        if (qit != m_pendingQueue.end()) {
            m_pendingQueue.erase(qit);
        }
        m_entries.erase(it);
        Q_EMIT tabClosed(tabId);
        trySpawnNextQueued();
        return true;
    }
    case TabState::Launching:
    case TabState::Running: {
        entry.state = TabState::Closing;
        m_sandboxSystem->shutdown(entry.sandboxId);
        return true;
    }
    case TabState::Faulted: {
        if (entry.sandboxId.isEmpty()) {
            // 子进程已经退出（handleProcessFinished 已经跑过、清空了 sandboxId），
            // 直接终结这个条目即可，不需要再等待任何异步事件。
            finalizeEntry(tabId, entry, /*emitClosed=*/true);
            trySpawnNextQueued();
        } else {
            // 子进程可能还没真正退出（例如报了 Faulted 但进程还在收尾），
            // 走和 Running 一样的优雅关闭路径，交给 handleProcessFinished 收尾。
            entry.state = TabState::Closing;
            m_sandboxSystem->shutdown(entry.sandboxId);
        }
        return true;
    }
    case TabState::Closing:
    case TabState::Closed:
        return false; // 已经在关闭/已关闭，重复调用是 no-op
    }
    return false;
}

bool TabSandboxManager::restartTab(TabId tabId)
{
    auto it = m_entries.find(tabId);
    if (it == m_entries.end()) {
        return false;
    }
    Entry &entry = it->second;
    if (entry.state == TabState::Closing) {
        return false; // 正在关闭中，语义上不清楚"重启"该指向新旧哪个实例，拒绝
    }

    if (!entry.sandboxId.isEmpty()) {
        // 旧实例还活着（Launching/Running/Faulted-but-not-yet-exited）：
        // 先摘掉旧的 sandboxId -> tabId 映射再发起 shutdown()，这样旧实例真正退出、
        // 触发 handleProcessFinished 时会因为查不到映射而直接忽略（见该函数实现），
        // 不会覆盖我们即将建立的新映射。
        m_sandboxIdToTab.erase(entry.sandboxId);
        m_sandboxSystem->shutdown(entry.sandboxId);
        entry.sandboxId.clear();
    }

    // 立即为同一个 Tab 重新 spawn()。短时间内新旧两个子进程可能同时存在
    // （旧的正在异步收尾、新的已经起来），因此并发计数在这个窗口内可能超出
    // maxConcurrentSandboxes() 一个名额——这是 restart 语义相对严格名额控制的
    // 刻意取舍，见类注释。
    spawnEntry(tabId, entry);
    return true;
}

void TabSandboxManager::closeAll()
{
    // 复制一份 key 列表再逐个 closeTab()：closeTab() 可能同步修改 m_entries
    // （Queued 分支），直接在遍历 m_entries 的同时改它是未定义行为。
    QVector<TabId> ids;
    ids.reserve(static_cast<int>(m_entries.size()));
    for (const auto &[id, entry] : m_entries) {
        Q_UNUSED(entry);
        ids.push_back(id);
    }
    for (const TabId &id : ids) {
        closeTab(id);
    }
}

void TabSandboxManager::finalizeEntry(const TabId &tabId, Entry &entry, bool emitClosed)
{
    entry.state = TabState::Closed;
    if (!entry.sandboxId.isEmpty()) {
        m_sandboxIdToTab.erase(entry.sandboxId);
        m_sandboxSystem->remove(entry.sandboxId);
        entry.sandboxId.clear();
    }
    m_entries.erase(tabId);
    if (emitClosed) {
        Q_EMIT tabClosed(tabId);
    }
}

TabState TabSandboxManager::tabState(TabId tabId) const
{
    auto it = m_entries.find(tabId);
    return it != m_entries.end() ? it->second.state : TabState::Closed;
}

QString TabSandboxManager::sandboxIdForTab(TabId tabId) const
{
    auto it = m_entries.find(tabId);
    return it != m_entries.end() ? it->second.sandboxId : QString{};
}

std::optional<TabId> TabSandboxManager::tabForSandboxId(const QString &sandboxId) const
{
    auto it = m_sandboxIdToTab.find(sandboxId);
    if (it == m_sandboxIdToTab.end()) {
        return std::nullopt;
    }
    return it->second;
}

std::optional<TabSession> TabSandboxManager::sessionForTab(TabId tabId) const
{
    auto it = m_entries.find(tabId);
    if (it == m_entries.end()) {
        return std::nullopt;
    }
    return it->second.session;
}

QVector<TabId> TabSandboxManager::tabIds() const
{
    QVector<TabId> ids;
    ids.reserve(static_cast<int>(m_entries.size()));
    for (const auto &[id, entry] : m_entries) {
        Q_UNUSED(entry);
        ids.push_back(id);
    }
    return ids;
}

size_t TabSandboxManager::count() const noexcept
{
    return m_entries.size();
}

QVector<TabId> TabSandboxManager::tryAdoptOrphanedSandboxes()
{
    // 见头文件里的详细说明：需要 SandboxSystem 换成 Registry 拓扑之后才能实现，
    // 目前故意保持空实现。
    return {};
}

void TabSandboxManager::handlePhaseChanged(const QString &sandboxId, SandboxPhase phase)
{
    auto tabIt = m_sandboxIdToTab.find(sandboxId);
    if (tabIt == m_sandboxIdToTab.end()) {
        return; // 不认识的 sandboxId（可能是 restartTab() 里刚摘掉映射的旧实例），忽略
    }
    auto entryIt = m_entries.find(tabIt->second);
    if (entryIt == m_entries.end()) {
        return;
    }
    Entry &entry = entryIt->second;

    switch (phase) {
    case SandboxPhase::Ready:
        // "打开一个 Tab"这个动作在 Host 侧的语义就是"让它跑起来"，不需要调用方
        // 再手动调一次 run()——这一步是驱动 SandboxPhase 从 Initialized 进入 Running
        // 唯一需要 Host 主动做的事，其余阶段迁移都是 SandboxSupervisor 自己响应的。
        m_sandboxSystem->run(sandboxId);
        break;
    case SandboxPhase::Running:
        entry.state = TabState::Running;
        Q_EMIT tabRunning(tabIt->second);
        break;
    default:
        break; // Connecting/Loading/Initializing/Stopping/Stopped/Faulted 不需要额外动作
               // （Faulted 由 handleFaulted 单独处理，携带 reason）
    }
}

void TabSandboxManager::handleFaulted(const QString &sandboxId, const QString &reason)
{
    auto tabIt = m_sandboxIdToTab.find(sandboxId);
    if (tabIt == m_sandboxIdToTab.end()) {
        return;
    }
    auto entryIt = m_entries.find(tabIt->second);
    if (entryIt == m_entries.end()) {
        return;
    }
    entryIt->second.state = TabState::Faulted;
    Q_EMIT tabFaulted(tabIt->second, reason);
}

void TabSandboxManager::handleLogMessage(const QString &sandboxId, int level, const QString &message)
{
    auto tabIt = m_sandboxIdToTab.find(sandboxId);
    if (tabIt == m_sandboxIdToTab.end()) {
        return;
    }
    Q_EMIT tabLogMessage(tabIt->second, level, message);
}

void TabSandboxManager::handleProcessFinished(const QString &sandboxId, int exitCode)
{
    Q_UNUSED(exitCode);

    auto tabIt = m_sandboxIdToTab.find(sandboxId);
    if (tabIt == m_sandboxIdToTab.end()) {
        // restartTab() 已经主动摘掉了旧映射，这里只需要把 SandboxSystem 侧的注册表
        // 也清干净，不涉及任何 Tab 状态变化。
        m_sandboxSystem->remove(sandboxId);
        return;
    }
    const TabId tabId = tabIt->second;
    auto entryIt       = m_entries.find(tabId);
    if (entryIt == m_entries.end()) {
        m_sandboxSystem->remove(sandboxId);
        return;
    }
    Entry &entry = entryIt->second;

    if (entry.state == TabState::Closing) {
        // 用户主动要求关闭，子进程也确实退出了：这个 Tab 彻底终结。
        finalizeEntry(tabId, entry, /*emitClosed=*/true);
        trySpawnNextQueued();
        return;
    }

    // 不是我们主动关闭的（典型情况：Faulted 之后子进程自己退出了）——保留这个
    // Entry（连同 session），状态维持/回落到 Faulted，只清空已经失效的 sandboxId，
    // 这样调用方后续还能用 restartTab() 或者 closeTab() 结束它；同时这个名额已经
    // 空出来了，尝试给排队中的 Tab 补上。
    m_sandboxIdToTab.erase(sandboxId);
    entry.sandboxId.clear();
    entry.state = TabState::Faulted;
    trySpawnNextQueued();
}

} // namespace bakuon::sandbox
