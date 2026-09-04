#include "sandbox/b_tabsandboxmanager.h"

#include <algorithm>

#include <QtCore/QDebug>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QJsonValue>
#include <QtCore/QSaveFile>
#include <QtCore/QStandardPaths>

#include "sandbox/b_sandboxsupervisor.h"
#include "sandbox/b_sandboxsystem.h"

namespace bakuon::sandbox {

TabSandboxManager::TabSandboxManager(QString sandboxRuntimeExecutable, QObject *parent)
    : QObject(parent)
    , m_defaultSandboxRuntimeExecutable(std::move(sandboxRuntimeExecutable))
    // 不把 this 传给 SandboxSystem 的构造参数（QObject parent）——本类用
    // std::unique_ptr 独占管理它的生命周期，两套所有权机制叠在同一个对象上
    // 会导致析构时被删两次，这里刻意保持 SandboxSystem 是"无父对象"的。
    , m_sandboxSystem(std::make_unique<SandboxSystem>())
{
    connect(m_sandboxSystem.get(),
            &SandboxSystem::sandboxPhaseChanged,
            this,
            &TabSandboxManager::onPhaseChanged);
    connect(m_sandboxSystem.get(),
            &SandboxSystem::sandboxFaulted,
            this,
            &TabSandboxManager::onFaulted);
    connect(m_sandboxSystem.get(),
            &SandboxSystem::sandboxLogMessage,
            this,
            &TabSandboxManager::onLogMessage);
    connect(m_sandboxSystem.get(),
            &SandboxSystem::sandboxProcessFinished,
            this,
            &TabSandboxManager::onProcessFinished);
    connect(m_sandboxSystem.get(),
            &SandboxSystem::orphanDiscovered,
            this,
            &TabSandboxManager::onOrphanDiscovered);
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
    m_maxConcurrent = max < 0 ? 0 : max;
    tryQueued();
}

int TabSandboxManager::maxConcurrentSandboxes() const noexcept
{
    return m_maxConcurrent;
}

QString TabSandboxManager::defaultSessionFilePath()
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    return dir.isEmpty() ? QString() : dir + QStringLiteral("/tab-sessions.json");
}

void TabSandboxManager::setSessionFilePath(QString filePath)
{
    m_sessionFilePath = std::move(filePath);
}

const QString &TabSandboxManager::sessionFilePath() const noexcept
{
    return m_sessionFilePath;
}

void TabSandboxManager::persistSession() const
{
    if (m_sessionFilePath.isEmpty()) {
        return;
    }

    QJsonArray arr;
    for (const auto &[id, session] : m_tabs) {
        Q_UNUSED(id)
        // 只写重建一个 Tab 需要的最小信息（见类文档"持久化范围"一节）；state 不写——
        // 恢复的时候统一用 Restoring，是不是真的还活着要靠重新发现孤儿来确认，
        // 崩溃前的 state 快照参考意义不大。
        QJsonObject obj;
        // tabId 用字符串存：JSON 数字在大多数实现里按 double 处理，64 位整数超出
        // double 精确表示范围（2^53）时会静默失真；字符串没有这个问题。
        obj[QStringLiteral("tabId")]                    = QString::number(session.tabId);
        obj[QStringLiteral("sandboxId")]                = session.sandboxId;
        obj[QStringLiteral("pluginFilePath")]           = session.pluginFilePath;
        obj[QStringLiteral("sandboxRuntimeExecutable")] = session.sandboxRuntimeExecutable;
        obj[QStringLiteral("pluginArguments")]          = QJsonObject::fromVariantMap(
            session.pluginArguments);
        arr.append(obj);
    }

    // QSaveFile：先写临时文件，commit() 时原子 rename 过去——这个功能存在的意义就是
    // 应对"进程随时可能崩溃"，写会话文件这一步本身也不能有"写到一半崩溃留下半份
    // 损坏文件"的风险，否则下次启动 restoreSession() 直接解析失败，等于白做。
    QSaveFile file(m_sessionFilePath);
    if (!file.open(QIODevice::WriteOnly)) {
        qWarning() << "TabSandboxManager::persistSession: 无法打开会话文件" << m_sessionFilePath
                   << file.errorString();
        return;
    }
    file.write(QJsonDocument(arr).toJson(QJsonDocument::Compact));
    if (!file.commit()) {
        qWarning() << "TabSandboxManager::persistSession: 提交会话文件失败" << m_sessionFilePath
                   << file.errorString();
    }
}

int TabSandboxManager::restoreSession()
{
    if (m_sessionFilePath.isEmpty()) {
        qWarning() << "TabSandboxManager::restoreSession: sessionFilePath 未设置，跳过";
        return 0;
    }

    QFile file(m_sessionFilePath);
    if (!file.exists()) {
        return 0; // 第一次启动/上次正常退出后被清空，不是错误
    }
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "TabSandboxManager::restoreSession: 无法打开会话文件" << m_sessionFilePath
                   << file.errorString();
        return 0;
    }
    const QByteArray raw = file.readAll();
    file.close();

    QJsonParseError parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(raw, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isArray()) {
        qWarning() << "TabSandboxManager::restoreSession: 会话文件解析失败，忽略"
                   << m_sessionFilePath << parseError.errorString();
        return 0;
    }

    int restored           = 0;
    uint64_t maxRestoredId = 0;
    for (const QJsonValue v : doc.array()) {
        if (!v.isObject()) {
            continue;
        }
        const QJsonObject obj = v.toObject();
        bool ok               = false;
        const uint64_t tabId  = obj[QStringLiteral("tabId")].toString().toULongLong(&ok);
        if (!ok || tabId == 0 || m_tabs.contains(tabId)) {
            continue; // 格式不对/tabId 冲突（理论上不该出现，防御性跳过而不是覆盖）
        }

        TabSession session;
        session.tabId          = tabId;
        session.sandboxId      = obj[QStringLiteral("sandboxId")].toString();
        session.pluginFilePath = obj[QStringLiteral("pluginFilePath")].toString();
        session.sandboxRuntimeExecutable = obj[QStringLiteral("sandboxRuntimeExecutable")].toString();
        session.pluginArguments = obj[QStringLiteral("pluginArguments")].toObject().toVariantMap();
        session.state           = TabState::Restoring;

        m_tabs.emplace(tabId, std::move(session));
        maxRestoredId = std::max(maxRestoredId, tabId);
        ++restored;
        Q_EMIT tabRestoring(tabId);
    }

    // 避免后续 openTab() 生成的新 tabId 和刚放回来的旧 tabId 撞车——本进程的
    // m_nextTabSeq 是从 1 重新数的，如果不追上历史最大值，很快就会撞上。
    if (maxRestoredId >= m_nextTabSeq) {
        m_nextTabSeq = maxRestoredId + 1;
    }
    return restored;
}

bool TabSandboxManager::respawnRestoredTab(uint64_t tabId)
{
    return restartTab(tabId);
}

size_t TabSandboxManager::pendingRestoreCount() const noexcept
{
    size_t n = 0;
    for (const auto &[id, session] : m_tabs) {
        Q_UNUSED(id)
        if (session.state == TabState::Restoring) {
            ++n;
        }
    }
    return n;
}

uint64_t TabSandboxManager::nextTabId()
{
    return m_nextTabSeq++;
}

size_t TabSandboxManager::occupyingCount() const noexcept
{
    size_t n = 0;
    for (const auto &[id, session] : m_tabs) {
        if (session.state != TabState::Queued) {
            ++n;
        }
    }
    return n;
}

uint64_t TabSandboxManager::openTab(const QString &pluginFilePath, QVariantMap pluginArguments,
                                    QString sandboxRuntimeExecutable)
{
    if (sandboxRuntimeExecutable.isEmpty()) {
        sandboxRuntimeExecutable = m_defaultSandboxRuntimeExecutable;
    }
    if (sandboxRuntimeExecutable.isEmpty()) {
        qWarning() << "TabSandboxManager::openTab: 未指定 sandboxRuntimeExecutable，"
                      "且 defaultSandboxRuntimeExecutable() 也是空的，拒绝打开新 Tab。";
        return 0;
    }

    const auto tabId = nextTabId();
    TabSession session;
    session.tabId                    = tabId;
    session.pluginFilePath           = pluginFilePath;
    session.sandboxRuntimeExecutable = sandboxRuntimeExecutable;
    session.pluginArguments          = std::move(pluginArguments);
    session.state                    = TabState::Queued;

    auto [it, inserted] = m_tabs.emplace(tabId, std::move(session));
    Q_ASSERT(inserted);
    Q_UNUSED(inserted)

    if (m_maxConcurrent <= 0 || occupyingCount() < static_cast<size_t>(m_maxConcurrent)) {
        spawnSession(it->second);
    } else {
        m_pendingQueue.push_back(tabId);
        Q_EMIT tabQueued(tabId);
    }
    persistSession();
    return tabId;
}

void TabSandboxManager::spawnSession(TabSession &session)
{
    // 恢复出来的会话（restoreSession()）里 sandboxRuntimeExecutable 理论上不该是空的
    // （openTab() 当初已经解析过一次），但这里仍然做一次兜底：sandboxRuntimeExecutable
    // 本质是"这次 Host 安装在哪"这类环境细节，不是 Tab 身份的一部分（pluginFilePath
    // 才是），换一次安装路径/版本升级之后旧记录里的路径完全可能失效——与其死板地用
    // 一个可能已经不存在的旧路径去 spawn() 必然失败，不如退回当前进程实际配置的
    // defaultSandboxRuntimeExecutable()，并把解析结果回填进 session，后续持久化/
    // 查询看到的就是真实生效的路径。
    if (session.sandboxRuntimeExecutable.isEmpty()) {
        session.sandboxRuntimeExecutable = m_defaultSandboxRuntimeExecutable;
    }
    const QString sandboxId = m_sandboxSystem->spawn(session.pluginFilePath,
                                                     session.sandboxRuntimeExecutable,
                                                     session.pluginArguments);
    session.sandboxId       = sandboxId;
    session.state           = TabState::Launching;
    m_sandboxIdToTab.emplace(sandboxId, session.tabId);
    Q_EMIT tabLaunching(session.tabId);
}

void TabSandboxManager::tryQueued()
{
    while (!m_pendingQueue.empty()
           && (m_maxConcurrent <= 0 || occupyingCount() < static_cast<size_t>(m_maxConcurrent))) {
        const auto tabId = m_pendingQueue.front();
        m_pendingQueue.pop_front();

        auto it = m_tabs.find(tabId);
        if (it == m_tabs.end() || it->second.state != TabState::Queued) {
            // Tab 在排队期间被 closeTab() 掉了，条目已经不在了（见 closeTab 对 Queued 分支
            // 的处理），跳过即可。
            continue;
        }
        spawnSession(it->second);
    }
    persistSession();
}

bool TabSandboxManager::closeTab(uint64_t tabId)
{
    auto it = m_tabs.find(tabId);
    if (it == m_tabs.end()) {
        return false;
    }
    auto &session = it->second;

    switch (session.state) {
    case TabState::Queued:
    case TabState::Restoring: {
        // 两种情况都还没有真正在跑的子进程——Queued 是排队等 spawn()，Restoring 是
        // 从会话文件读回来、还没等到匹配孤儿——直接从排队队列（如果在）和条目表里
        // 摘掉即可，没有子进程需要等待退出。
        m_pendingQueue.erase(std::remove(m_pendingQueue.begin(), m_pendingQueue.end(), tabId),
                             m_pendingQueue.end());
        m_tabs.erase(it);
        Q_EMIT tabClosed(tabId);
        tryQueued(); // 内部会 persistSession()
        return true;
    }
    case TabState::Launching:
    case TabState::Running  : {
        session.state = TabState::Closing;
        m_sandboxSystem->shutdown(session.sandboxId);
        persistSession();
        return true;
    }
    case TabState::Faulted: {
        if (session.sandboxId.isEmpty()) {
            // 子进程已经退出（handleProcessFinished 已经跑过、清空了 sandboxId），
            // 直接终结这个条目即可，不需要再等待任何异步事件。
            finalizeSession(session, /*emitClosed=*/true);
            tryQueued(); // 内部会 persistSession()
        } else {
            // 子进程可能还没真正退出（例如报了 Faulted 但进程还在收尾），
            // 走和 Running 一样的优雅关闭路径，交给 handleProcessFinished 收尾。
            session.state = TabState::Closing;
            m_sandboxSystem->shutdown(session.sandboxId);
            persistSession();
        }
        return true;
    }
    case TabState::Closing: return true; // 已经在关闭/已关闭，重复调用是 no-op
    default               : break;
    }
    return true;
}

void TabSandboxManager::closeAll()
{
    // 复制一份 key 列表再逐个 closeTab()：closeTab() 可能同步修改 m_tabs
    // （Queued 分支），直接在遍历 m_tabs 的同时改它是未定义行为。
    QVector<uint64_t> ids;
    ids.reserve(static_cast<int>(m_tabs.size()));
    for (const auto &[id, entry] : m_tabs) {
        Q_UNUSED(entry)
        ids.push_back(id);
    }
    for (const auto &id : ids) {
        closeTab(id);
    }
}

bool TabSandboxManager::restartTab(uint64_t tabId)
{
    auto it = m_tabs.find(tabId);
    if (it == m_tabs.end()) {
        return false;
    }
    auto &session = it->second;
    if (session.state == TabState::Queued || session.state == TabState::Closing) {
        return false; // 正在关闭中，语义上不清楚"重启"该指向新旧哪个实例，拒绝
    }

    if (!session.sandboxId.isEmpty()) {
        // 旧实例还活着（Launching/Running/Faulted-but-not-yet-exited）：
        // 先摘掉旧的 sandboxId -> tabId 映射再发起 shutdown()，这样旧实例真正退出、
        // 触发 handleProcessFinished 时会因为查不到映射而直接忽略（见该函数实现），
        // 不会覆盖我们即将建立的新映射。
        m_sandboxIdToTab.erase(session.sandboxId);
        m_sandboxSystem->shutdown(session.sandboxId);
        session.sandboxId.clear();
    }

    // 立即为同一个 Tab 重新 spawn()。短时间内新旧两个子进程可能同时存在
    // （旧的正在异步收尾、新的已经起来），因此并发计数在这个窗口内可能超出
    // maxConcurrentSandboxes() 一个名额——这是 restart 语义相对严格名额控制的
    // 刻意取舍，见类注释。
    spawnSession(session);
    persistSession();
    return true;
}

void TabSandboxManager::finalizeSession(TabSession &session, bool emitClosed)
{
    // 先把 tabId 拷贝出来：m_tabs.erase(session.tabId) 之后 session 这个引用就悬空了
    // （指向的 map 节点已经被销毁），后面不能再碰 session 的任何成员，包括读 tabId 本身——
    // 这里如果直接在 erase() 之后继续用 session.tabId 是访问已析构对象的未定义行为。
    const uint64_t tabId = session.tabId;

    session.state = TabState::Faulted;
    if (!session.sandboxId.isEmpty()) {
        m_sandboxIdToTab.erase(session.sandboxId);
        m_sandboxSystem->remove(session.sandboxId);
        session.sandboxId.clear();
    }
    m_tabs.erase(tabId);
    persistSession();
    if (emitClosed) {
        Q_EMIT tabClosed(tabId);
    }
}

TabState TabSandboxManager::tabState(uint64_t tabId) const
{
    auto it = m_tabs.find(tabId);
    return it != m_tabs.end() ? it->second.state : TabState::Faulted;
}

QString TabSandboxManager::sandboxIdForTab(uint64_t tabId) const
{
    auto it = m_tabs.find(tabId);
    return it != m_tabs.end() ? it->second.sandboxId : QString{};
}

std::optional<uint64_t> TabSandboxManager::tabForSandboxId(const QString &sandboxId) const
{
    auto it = m_sandboxIdToTab.find(sandboxId);
    if (it == m_sandboxIdToTab.end()) {
        return std::nullopt;
    }
    return it->second;
}

std::optional<TabSession> TabSandboxManager::sessionForTab(uint64_t tabId) const
{
    auto it = m_tabs.find(tabId);
    if (it == m_tabs.end()) {
        return std::nullopt;
    }
    return it->second;
}

QVector<uint64_t> TabSandboxManager::tabIds() const
{
    QVector<uint64_t> ids;
    ids.reserve(static_cast<int>(m_tabs.size()));
    for (const auto &[id, session] : m_tabs) {
        Q_UNUSED(session)
        ids.push_back(id);
    }
    return ids;
}

size_t TabSandboxManager::count() const noexcept
{
    return m_tabs.size();
}

QVector<uint64_t> TabSandboxManager::tryAdoptOrphanedSandboxes()
{
    QVector<uint64_t> adopted;
    // 先把待处理列表整体挪出来再遍历：adopt() 内部会同步触发一连串信号
    // （tabAdopted/tabLaunching 等），万一某个槽函数又重入调用了本方法，直接在
    // m_pendingOrphans 上遍历+修改会是未定义行为，先搬空更安全。
    std::vector<QString> pending;
    pending.swap(m_pendingOrphans);

    for (const QString &sandboxId : pending) {
        if (m_sandboxIdToTab.contains(sandboxId)) {
            continue; // 上一轮已经收编过了（理论上不应该出现在这里，防御性检查）
        }
        if (!m_sandboxSystem->adopt(sandboxId)) {
            continue; // SandboxSystem 侧判定它已经不是孤儿了（比如已被别的路径收编）
        }

        // 优先匹配 restoreSession() 放回来的 Restoring 记录——按 sandboxId 精确匹配，
        // 命中就是"原地恢复成同一个 Tab"，这是本方法结合会话持久化之后真正的目的。
        // 找不到匹配（真正陌生的孤儿）才走"分配全新 tabId"的老路径。
        uint64_t restoredTabId = 0;
        for (auto &[id, session] : m_tabs) {
            if (session.state == TabState::Restoring && session.sandboxId == sandboxId) {
                restoredTabId = id;
                break;
            }
        }

        if (restoredTabId != 0) {
            TabSession &session = m_tabs.at(restoredTabId);
            session.state       = TabState::Launching;
            m_sandboxIdToTab.emplace(sandboxId, restoredTabId);

            adopted.push_back(restoredTabId);
            Q_EMIT tabRestored(restoredTabId, sandboxId);
            Q_EMIT tabLaunching(restoredTabId);
        } else {
            const auto tabId = nextTabId();
            TabSession session;
            session.tabId                    = tabId;
            session.sandboxId                = sandboxId;
            session.sandboxRuntimeExecutable = m_defaultSandboxRuntimeExecutable;
            // pluginFilePath 留空：这是"陌生收编"和"原地恢复"/"正常 openTab()"在数据
            // 完整性上的本质区别，见类注释——本进程从未见过它，无从得知。
            session.state                    = TabState::Launching;

            m_tabs.emplace(tabId, std::move(session));
            m_sandboxIdToTab.emplace(sandboxId, tabId);

            adopted.push_back(tabId);
            Q_EMIT tabAdopted(tabId, sandboxId);
            Q_EMIT tabLaunching(tabId);
        }
    }
    if (!adopted.isEmpty()) {
        persistSession();
    }
    return adopted;
}

void TabSandboxManager::onPhaseChanged(const QString &sandboxId, SandboxPhase phase)
{
    auto it = m_sandboxIdToTab.find(sandboxId);
    if (it == m_sandboxIdToTab.end()) {
        return; // 不认识的 sandboxId（可能是 restartTab() 里刚摘掉映射的旧实例），忽略
    }
    auto tabIt = m_tabs.find(it->second);
    if (tabIt == m_tabs.end()) {
        return;
    }
    auto &session = tabIt->second;

    switch (phase) {
    case SandboxPhase::Ready:
        // "打开一个 Tab"这个动作在 Host 侧的语义就是"让它跑起来"，不需要调用方
        // 再手动调一次 run()——这一步是驱动 SandboxPhase 从 Initialized 进入 Running
        // 唯一需要 Host 主动做的事，其余阶段迁移都是 SandboxSupervisor 自己响应的。
        m_sandboxSystem->run(sandboxId);
        break;
    case SandboxPhase::Running:
        session.state = TabState::Running;
        Q_EMIT tabRunning(session.tabId);
        persistSession();
        break;
    default:
        break; // Connecting/Loading/Initializing/Stopping/Stopped/Faulted 不需要额外动作
               // （Faulted 由 onFaulted 单独处理，携带 reason）
    }
}

void TabSandboxManager::onFaulted(const QString &sandboxId, const QString &reason)
{
    const auto it = m_sandboxIdToTab.find(sandboxId);
    if (it == m_sandboxIdToTab.end()) {
        return;
    }
    auto tabIt = m_tabs.find(it->second);
    if (tabIt == m_tabs.end()) {
        return;
    }

    auto &session = tabIt->second;
    session.state = TabState::Faulted;
    Q_EMIT tabFaulted(session.tabId, reason);
    persistSession();
}

void TabSandboxManager::onLogMessage(const QString &sandboxId, int level, const QString &message)
{
    auto tabIt = m_sandboxIdToTab.find(sandboxId);
    if (tabIt == m_sandboxIdToTab.end()) {
        return;
    }
    Q_EMIT tabLogMessage(tabIt->second, level, message);
}

void TabSandboxManager::onProcessFinished(const QString &sandboxId, int exitCode)
{
    Q_UNUSED(exitCode)

    auto tabIt = m_sandboxIdToTab.find(sandboxId);
    if (tabIt == m_sandboxIdToTab.end()) {
        // restartTab() 已经主动摘掉了旧映射，这里只需要把 SandboxSystem 侧的注册表
        // 也清干净，不涉及任何 Tab 状态变化。
        m_sandboxSystem->remove(sandboxId);
        return;
    }
    const auto tabId = tabIt->second;
    auto sessionIt   = m_tabs.find(tabId);
    if (sessionIt == m_tabs.end()) {
        m_sandboxSystem->remove(sandboxId);
        return;
    }
    auto &session = sessionIt->second;
    if (session.state == TabState::Closing) {
        // 用户主动要求关闭，子进程也确实退出了：这个 Tab 彻底终结。
        finalizeSession(session, /*emitClosed=*/true);
        tryQueued();
        return;
    }

    // 不是我们主动关闭的（典型情况：Faulted 之后子进程自己退出了）——保留这个
    // Entry（连同 session），状态维持/回落到 Faulted，只清空已经失效的 sandboxId，
    // 这样调用方后续还能用 restartTab() 或者 closeTab() 结束它；同时这个名额已经
    // 空出来了，尝试给排队中的 Tab 补上。
    m_sandboxIdToTab.erase(sandboxId);
    session.sandboxId.clear();
    session.state = TabState::Faulted;
    tryQueued();
}

void TabSandboxManager::onOrphanDiscovered(const QString &sandboxId)
{
    if (m_sandboxIdToTab.contains(sandboxId)) {
        return; // 理论上不会发生（SandboxSystem 那边已经用 m_tabs 过滤过一次），防御性检查
    }
    if (std::find(m_pendingOrphans.begin(), m_pendingOrphans.end(), sandboxId)
        != m_pendingOrphans.end()) {
        return; // 已经报告过、还没被 tryAdoptOrphanedSandboxes() 处理掉，不重复入队
    }
    m_pendingOrphans.push_back(sandboxId);
    Q_EMIT orphanSandboxAvailable(sandboxId);
}

} // namespace bakuon::sandbox
