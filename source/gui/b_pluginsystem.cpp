#include "gui/b_pluginsystem.h"

#include <QtCore/QDir>
#include <QtCore/QDirIterator>
#include <QtCore/QLibrary>
#include <QtCore/QReadLocker>
#include <QtCore/QWriteLocker>

namespace bakuon::gui {

PluginSystem::PluginSystem(QObject* parent)
    : QObject(parent)
{
}

PluginSystem::~PluginSystem() = default;

// ============================================================================
// 注册
// ============================================================================

size_t PluginSystem::registerBuiltIn(std::shared_ptr<IPlugin> instance)
{
    if (!instance) {
        m_lastError = QStringLiteral("registerBuiltIn: 传入了空的 IPlugin 实例");
        return 0;
    }
    const size_t id = nextId();
    auto p          = std::make_shared<PluginPipeline>(id, std::move(instance));
    registerPipeline(id, p);
    return id;
}

size_t PluginSystem::registerFile(const QString& filePath)
{
    const size_t id = nextId();
    auto p          = std::make_shared<PluginPipeline>(id, filePath);
    registerPipeline(id, p);
    return id;
}

QVector<size_t> PluginSystem::registerDirectory(const QString& directory, bool recursive)
{
    QVector<size_t> ids;

    QDir dir(directory);
    if (!dir.exists()) {
        m_lastError = QStringLiteral("registerDirectory: 目录不存在: %1").arg(directory);
        return ids;
    }

    const auto flags = recursive ? QDirIterator::Subdirectories : QDirIterator::NoIteratorFlags;
    QDirIterator it(directory, QDir::Files, flags);
    while (it.hasNext()) {
        const QString filePath = it.next();
        if (QLibrary::isLibrary(filePath)) {
            ids.push_back(registerFile(filePath));
        }
    }
    return ids;
}

bool PluginSystem::unregisterPlugin(size_t id)
{
    QWriteLocker locker(&m_lock);

    auto it = m_entries.find(id);
    if (it == m_entries.end()) {
        m_lastError = QStringLiteral("unregisterPlugin: 未知的插件 id=%1").arg(id);
        return false;
    }
    if (it->second->state() != PluginState::Unloaded) {
        m_lastError = QStringLiteral(
                          "unregisterPlugin: 插件 id=%1 当前处于 %2，只有 Unloaded 态才允许移除")
                          .arg(id)
                          .arg(toString(it->second->state()));
        return false;
    }

    const QString pluginId = it->second->metadata().id;
    m_entries.erase(it);

    if (!pluginId.isEmpty()) {
        auto namedIt = m_namedEntries.find(pluginId);
        // 理论上这里一定能找到、且指向同一个对象；加个防御性检查，避免误删了同名冲突场景下
        // 属于另一个插件的条目（见 onPipelineStateChanged() 里对 id 冲突的处理）。
        if (namedIt != m_namedEntries.end() && namedIt->second->id() == id) {
            m_namedEntries.erase(namedIt);
        }
    }
    return true;
}

size_t PluginSystem::unregisterUnloaded()
{
    // 不能直接对 m_entries 做 range-for 同时调用 unregisterPlugin()（它会 erase() 当前正在遍历的
    // 元素，是未定义行为），见 idSnapshot 函数实现里的注释。
    const std::vector<size_t> ids = idSnapshot();

    size_t removed = 0;
    for (size_t id : ids) {
        auto p = pipeline(id);
        if (p && p->state() == PluginState::Unloaded && unregisterPlugin(id)) {
            ++removed;
        }
    }
    return removed;
}

// ============================================================================
// 批量编排
// ============================================================================

bool PluginSystem::launchAll()
{
    const std::vector<size_t> ids = idSnapshot();

    bool allOk = true;
    for (size_t id : ids) {
        if (auto p = pipeline(id)) {
            allOk = p->launch() && allOk;
        }
    }

    // 依赖顺序不保证：如果插件 A 依赖插件 B、而 B 的注册顺序排在 A 后面，A 第一轮 resolve 时
    // B 还没被发现过，会失败在 ResolveFailed。跑完第一轮后，所有插件（不论自己成功与否）只要
    // 走到过 Validated，id 就已经登记进命名表了，这时候重试一次 resolve 就有完整信息了——
    // 一轮重试对任意依赖深度都足够，因为命名表的完整性只取决于"是否跑过一轮"，不取决于顺序。
    for (size_t id : ids) {
        auto p = pipeline(id);
        if (p && p->state() == PluginState::ResolveFailed) {
            allOk = p->launch() && allOk;
        }
    }
    return allOk;
}

bool PluginSystem::runAll()
{
    const std::vector<size_t> ids = idSnapshot();

    bool allOk = true;
    for (size_t id : ids) {
        auto p = pipeline(id);
        if (!p || p->state() != PluginState::Initialized) {
            continue; // 没准备好的直接跳过，不计入失败——它有自己的失败状态可查（见类头部说明）
        }
        allOk = p->run() && allOk;
    }
    return allOk;
}

bool PluginSystem::startup()
{
    bool ok = launchAll();
    ok      = runAll() && ok;
    return ok;
}

bool PluginSystem::stopAll()
{
    const std::vector<size_t> ids = idSnapshot();

    bool allOk = true;
    for (auto rit = ids.rbegin(); rit != ids.rend(); ++rit) {
        auto p = pipeline(*rit);
        if (p && (p->state() == PluginState::Running || p->state() == PluginState::RunFailed)) {
            allOk = p->stop() && allOk;
        }
    }
    return allOk;
}

bool PluginSystem::unloadAll()
{
    const std::vector<size_t> ids = idSnapshot();

    bool allOk = true;
    for (auto rit = ids.rbegin(); rit != ids.rend(); ++rit) {
        auto p = pipeline(*rit);
        if (p && p->state() == PluginState::Stopped) {
            allOk = p->unload() && allOk;
            // unload()/unloadAll()不自动移除：卸载后可能还想查 pipeline(id)->lastError()/最终状态做诊断，
            // "卸载"和"彻底释放"是两个动作，交给调用方自己决定要不要接着调 unregisterPlugin()
        }
    }
    return allOk;
}

void PluginSystem::shutdown()
{
    stopAll();
    unloadAll();
}

// ============================================================================
// 单个插件的编排（薄封装，直接转发给对应的 pipeline）
// ============================================================================

bool PluginSystem::launch(size_t id)
{
    auto p = pipeline(id);
    if (!p) {
        m_lastError = QStringLiteral("launch: 未知的插件 id=%1").arg(id);
        return false;
    }
    return p->launch();
}

bool PluginSystem::run(size_t id)
{
    auto p = pipeline(id);
    if (!p) {
        m_lastError = QStringLiteral("run: 未知的插件 id=%1").arg(id);
        return false;
    }
    return p->run();
}

bool PluginSystem::stop(size_t id)
{
    auto p = pipeline(id);
    if (!p) {
        return false;
    }
    return p->stop();
}

bool PluginSystem::unloadNow(size_t id)
{
    auto p = pipeline(id);
    if (!p) {
        return false;
    }
    // NOTE: 是否忘记了释放 Pipeline 对象？
    return p->unload();
}

// ============================================================================
// 查询
// ============================================================================

bool PluginSystem::hasPlugin(size_t id) const
{
    QReadLocker locker(&m_lock);
    return m_entries.find(id) != m_entries.end();
}

bool PluginSystem::hasPlugin(const QString& id) const
{
    QReadLocker locker(&m_lock);
    return m_namedEntries.find(id) != m_namedEntries.end();
}

size_t PluginSystem::pluginCount() const
{
    QReadLocker locker(&m_lock);
    return m_entries.size();
}

std::shared_ptr<PluginPipeline> PluginSystem::pipeline(size_t id) const
{
    QReadLocker locker(&m_lock);
    auto it = m_entries.find(id);
    return it != m_entries.end() ? it->second : nullptr;
}

std::shared_ptr<PluginPipeline> PluginSystem::pipeline(const QString& id) const
{
    QReadLocker locker(&m_lock);
    auto it = m_namedEntries.find(id);
    return it != m_namedEntries.end() ? it->second : nullptr;
}

std::vector<std::shared_ptr<PluginPipeline>> PluginSystem::pipelines() const
{
    QReadLocker locker(&m_lock);
    std::vector<std::shared_ptr<PluginPipeline>> result;
    result.reserve(m_entries.size());
    for (const auto& entry : m_entries) {
        result.push_back(entry.second);
    }
    return result;
}

// ============================================================================
// 私有辅助
// ============================================================================

size_t PluginSystem::nextId()
{
    return m_nextId.fetch_add(1, std::memory_order_relaxed);
}

/**
 * 为什么这里要先把 id 收集成一份快照（std::vector），而不是在需要遍历的地方直接写
 * for (const auto &[id, pipeline] : m_entries) { ... }：
 * 
 *  1. 遍历期间会调用 pipeline->launch()/run()/stop()/unloadNow()，这些调用会同步触发
 *     PluginPipeline::stateChanged 信号，直接连接到 onPipelineStateChanged()——它会尝试
 *     获取 m_lock 来写 m_namedEntries。如果外层遍历时正持有 m_lock（哪怕只是读锁），
 *     这个回调再去请求写锁就会自死锁：QReadWriteLock 不允许同一线程从"持有读锁"升级到
 *     "持有写锁"。所以外层遍历必须先释放锁，再去调用 pipeline 的方法，这就要求先把
 *     "要遍历哪些 id" 这件事在锁的保护下一次性问清楚、存下来，而不是让锁的生命周期
 *     横跨整个遍历过程。
 *  2. 直接对 m_entries 做 range-for，同时循环体里又可能触发 unregisterPlugin() 之类会
 *     erase() 该 map 的操作（见 unregisterAllUnloaded()），对 unordered_map 边遍历边
 *     erase 当前元素之外的操作是未定义行为，快照成普通 vector 之后就没有这个问题。
 *  3. 收集到的 id 集合天然是"那一刻的快照"：遍历过程中哪怕通过其它路径新增/移除了插件，
 *     这一轮批量操作的范围也不会被打乱，语义更容易讲清楚。
 * 
 * 代价是要按 id 重新 pipeline(id) 查一次（多一次哈希查找 + 加锁/解锁），量级上（几十个
 * 插件）可以忽略；真的想省这次查找，可以在快照里直接存 shared_ptr<PluginPipeline> 而不是
 * id——目前存 id 是因为 unregisterAllUnloaded() 这类场景需要用 id 重新判断"现在还在不在/
 * 现在是什么状态"，而不是用快照时刻的旧状态。
 */
std::vector<size_t> PluginSystem::idSnapshot() const
{
    QReadLocker locker(&m_lock);
    std::vector<size_t> ids;
    ids.reserve(m_entries.size());
    for (const auto& entry : m_entries) {
        ids.push_back(entry.first);
    }
    return ids;
}

void PluginSystem::registerPipeline(size_t id, const std::shared_ptr<PluginPipeline>& pipeline)
{
    pipeline->setResolveHook([this](const PluginMetadata& meta) { return resolveDependency(meta); });
    connect(pipeline.get(),
            &PluginPipeline::stateChanged,
            this,
            &PluginSystem::onPipelineStateChanged);
    connect(pipeline.get(), &PluginPipeline::running, this, &PluginSystem::pluginRunning);
    connect(pipeline.get(), &PluginPipeline::failed, this, &PluginSystem::pluginFailed);

    QWriteLocker locker(&m_lock);
    m_entries.emplace(id, pipeline);
    // 内置插件构造完 metadata 就已经知道了（状态直接是 Validated），趁写锁还在手上顺便登记进命名表；
    // 动态库插件此时 metadata 还是空的，要等 onPipelineStateChanged() 在 Validated 时机再登记。
    if (!pipeline->metadata().id.isEmpty()) {
        m_namedEntries.emplace(pipeline->metadata().id, pipeline);
    }
}

void PluginSystem::onPipelineStateChanged(size_t id, PluginState state)
{
    Q_EMIT pluginStateChanged(id, state);

    if (state != PluginState::Validated) {
        return;
    }

    auto p = pipeline(id);
    if (!p) {
        return;
    }
    const QString pluginId = p->metadata().id;
    if (pluginId.isEmpty()) {
        return;
    }

    // 注意这里的抢锁
    QWriteLocker locker(&m_lock);
    auto it = m_namedEntries.find(pluginId);
    if (it != m_namedEntries.end() && it->second != p) {
        m_lastError = QStringLiteral("插件 id 冲突：\"%1\" 同时被多个动态库文件声明").arg(pluginId);
        return;
    }
    m_namedEntries.emplace(pluginId, p);
}

std::optional<QString> PluginSystem::resolveDependency(const PluginMetadata& meta) const
{
    for (const auto& dep : meta.dependencies) {
        if (dep.id == meta.id) {
            return QStringLiteral("插件不能依赖自身: %1").arg(meta.id);
        }
    }

    QReadLocker locker(&m_lock);

    for (const auto& dep : meta.dependencies) {
        if (dep.type == PluginDependency::RequireType::Required
            && !m_namedEntries.contains(dep.id)) {
            return QStringLiteral(
                       "缺少必需依赖 \"%1\"（如果它也在同一批目录扫描里，稍后会自动重试）")
                .arg(dep.id);
        }
    }

    if (!meta.id.isEmpty()) {
        QSet<QString> visiting;
        QSet<QString> visited;
        QStringList path;
        if (hasDependencyCycle(meta.id, visiting, visited, path)) {
            return QStringLiteral("检测到循环依赖: %1").arg(path.join(QStringLiteral(" -> ")));
        }
    }
    return std::nullopt;
}

bool PluginSystem::hasDependencyCycle(const QString& startId, QSet<QString>& visiting,
                                      QSet<QString>& visited, QStringList& pathOut) const
{
    if (visited.contains(startId)) {
        return false;
    }
    if (visiting.contains(startId)) {
        pathOut.push_back(startId); // 闭合点：把环显示完整
        return true;
    }

    visiting.insert(startId);
    pathOut.push_back(startId);

    auto it = m_namedEntries.find(startId);
    if (it != m_namedEntries.end()) {
        for (const auto& dep : it->second->metadata().dependencies) {
            if (hasDependencyCycle(dep.id, visiting, visited, pathOut)) {
                return true;
            }
        }
    }

    pathOut.removeLast();
    visiting.remove(startId);
    visited.insert(startId);
    return false;
}

} // namespace bakuon::gui
