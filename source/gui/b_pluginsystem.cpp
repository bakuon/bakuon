#include "gui/b_pluginsystem.h"

#include <QtCore/QDir>
#include <QtCore/QDirIterator>
#include <QtCore/QLibrary>
#include <QtCore/QReadLocker>
#include <QtCore/QWriteLocker>

#include "gui/b_plugin.h"
#include "gui/b_pluginblock.h"
#include "gui/b_plugindiscoverer.h"

namespace bakuon::gui {

PluginSystem::PluginSystem(QObject* parent)
    : QObject(parent)
{
}

PluginSystem::~PluginSystem() = default;

// ============================================================================
// 私有辅助
// ============================================================================

std::shared_ptr<PluginBlock> PluginSystem::blockFor(size_t id) const
{
    QReadLocker locker(&m_lock);
    auto it = m_entries.find(id);
    return it != m_entries.end() ? it->second : nullptr;
}

size_t PluginSystem::numericIdOf(const QString& pluginId) const
{
    QReadLocker locker(&m_lock);
    auto it = m_namedEntries.find(pluginId);
    return it != m_namedEntries.end() ? it->second->id() : 0;
}

PluginDiscoverer& PluginSystem::discoverer()
{
    if (!m_discoverer) {
        // 不传 QObject parent：所有权完全交给 unique_ptr，避免和 QObject 父子删除机制混在一起。
        m_discoverer = std::make_unique<PluginDiscoverer>();
    }
    return *m_discoverer;
}

// ============================================================================
// 注册/发现
// ============================================================================

size_t PluginSystem::nextId()
{
    return m_nextId.fetch_add(1, std::memory_order_relaxed);
}

size_t PluginSystem::discoverBuiltIn(const std::shared_ptr<PluginBlock>& plugin)
{
    if (!plugin || !plugin->plugin()) {
        m_lastError = QStringLiteral("discoverBuiltIn: 传入了空的 PluginBlock");
        return 0;
    }

    const QString stringId = plugin->plugin()->pluginId();
    if (stringId.isEmpty()) {
        m_lastError = QStringLiteral(
            "discoverBuiltIn: 插件未提供有效的 IPlugin::id()（是否忘了 load()？"
            "内置插件应该在构造 Plugin 时就已经绑定实例，一构造完就是已加载状态）");
        return 0;
    }

    const size_t numericId = plugin->id();

    QWriteLocker locker(&m_lock);
    if (m_entries.contains(numericId)) {
        m_lastError = QStringLiteral("discoverBuiltIn: 数字 id=%1 已被占用，请用 nextId() 分配")
                          .arg(numericId);
        return 0;
    }
    if (m_namedEntries.contains(stringId)) {
        m_lastError = QStringLiteral("discoverBuiltIn: 插件 id \"%1\" 与已注册插件冲突")
                          .arg(stringId);
        return 0;
    }

    m_entries.emplace(numericId, plugin);
    m_namedEntries.emplace(stringId, plugin);
    locker.unlock();

    Q_EMIT pluginDiscovered(plugin->plugin()->filePath());
    return numericId;
}

size_t PluginSystem::discoverPlugin(const QString& filePath)
{
    if (!discoverer().discover(filePath)) {
        m_lastError = QStringLiteral("discoverPlugin: 元数据解析失败: %1").arg(filePath);
        Q_EMIT pluginDiscoveryFailed(filePath, m_lastError);
        return 0;
    }

    const auto meta = discoverer().metadata(filePath);
    if (!meta) {
        // 理论上不会走到这里：discover() 刚刚成功过，metadata() 应该一定能取到。
        m_lastError = QStringLiteral(
                          "discoverPlugin: 内部错误，discover() 成功但拿不到 metadata: %1")
                          .arg(filePath);
        return 0;
    }

    {
        QReadLocker locker(&m_lock);
        if (m_namedEntries.contains(meta->id)) {
            m_lastError = QStringLiteral("discoverPlugin: 插件 id \"%1\" 与已注册插件冲突 (%2)")
                              .arg(meta->id, filePath);
            return 0;
        }
    }

    const size_t numericId = nextId();
    // 这里只是分配好了 QPluginLoader 壳子（构造 Plugin，但不调用 load()），真正的
    // dlopen/instance() 延迟到 load()/loadAll() 才发生——discover 和 load 是两个独立阶段。
    auto block             = PluginBlock::create(numericId, filePath);

    QWriteLocker locker(&m_lock);
    m_entries.emplace(numericId, block);
    m_namedEntries.emplace(meta->id, block);
    locker.unlock();

    Q_EMIT pluginDiscovered(filePath);
    return numericId;
}

QVector<size_t> PluginSystem::discoverPlugins(const QString& directory, bool recursive)
{
    QVector<size_t> ids;

    QDir dir(directory);
    if (!dir.exists()) {
        m_lastError = QStringLiteral("discoverPlugins: 目录不存在: %1").arg(directory);
        return ids;
    }

    // 没有直接复用 PluginDiscoverer::discoverDirectory()：那个方法只做“文件级”发现，
    // 不经过 PluginSystem 的 id 分配 / 命名冲突检测。这里自己扫描目录、对每个候选文件调用
    // discoverPlugin()，确保目录批量发现和单文件发现走的是同一条注册路径。
    const auto flags = recursive ? QDirIterator::Subdirectories : QDirIterator::NoIteratorFlags;
    QDirIterator it(directory, QDir::Files, flags);
    while (it.hasNext()) {
        const QString filePath = it.next();
        if (!QLibrary::isLibrary(filePath)) {
            continue;
        }
        const size_t id = discoverPlugin(filePath);
        if (id != 0) {
            ids.push_back(id);
        }
    }
    return ids;
}

// ============================================================================
// 生命周期：load / unload
// ============================================================================

bool PluginSystem::load(size_t id)
{
    auto block = blockFor(id);
    if (!block) {
        m_lastError = QStringLiteral("load: 未知的插件 id=%1").arg(id);
        return false;
    }

    Q_EMIT pluginLoading(id);
    if (!block->plugin()->load()) {
        m_lastError = QStringLiteral("load: 插件加载失败 id=%1 (%2)")
                          .arg(id)
                          .arg(block->plugin()->filePath());
        Q_EMIT pluginLoadFailed(id);
        return false;
    }
    Q_EMIT pluginLoaded(id);
    return true;
}

bool PluginSystem::load(const QString& pluginId)
{
    const size_t id = numericIdOf(pluginId);
    if (id == 0) {
        m_lastError = QStringLiteral("load: 未知的插件 id=\"%1\"").arg(pluginId);
        return false;
    }
    return load(id);
}

bool PluginSystem::loadAll()
{
    std::vector<size_t> ids;
    {
        QReadLocker locker(&m_lock);
        ids.reserve(m_entries.size());
        for (const auto& entry : m_entries) {
            ids.push_back(entry.first);
        }
    }

    bool allOk = true;
    for (size_t id : ids) {
        allOk = load(id) && allOk;
    }
    return allOk;
}

bool PluginSystem::unload(size_t id)
{
    auto block = blockFor(id);
    if (!block) {
        m_lastError = QStringLiteral("unload: 未知的插件 id=%1").arg(id);
        return false;
    }

    if (!block->plugin()->unload()) {
        m_lastError = QStringLiteral("unload: 插件卸载失败 id=%1").arg(id);
        Q_EMIT pluginUnloadFailed(id);
        return false;
    }
    Q_EMIT pluginUnloaded(id);
    return true;
}

bool PluginSystem::unload(const QString& pluginId)
{
    const size_t id = numericIdOf(pluginId);
    if (id == 0) {
        m_lastError = QStringLiteral("unload: 未知的插件 id=\"%1\"").arg(pluginId);
        return false;
    }
    return unload(id);
}

bool PluginSystem::unloadAll()
{
    std::vector<size_t> ids;
    {
        QReadLocker locker(&m_lock);
        ids.reserve(m_entries.size());
        for (const auto& entry : m_entries) {
            ids.push_back(entry.first);
        }
    }

    bool allOk = true;
    for (size_t id : ids) {
        allOk = unload(id) && allOk;
    }
    return allOk;
}

// ============================================================================
// 生命周期：initialize / shutdown
// ============================================================================

bool PluginSystem::doInitialize(size_t id)
{
    auto block = blockFor(id);
    if (!block) {
        m_lastError = QStringLiteral("initialize: 未知的插件 id=%1").arg(id);
        return false;
    }
    if (!block->plugin()->isLoaded()) {
        m_lastError = QStringLiteral("initialize: 插件 id=%1 尚未 load()").arg(id);
        return false;
    }

    if (!block->plugin()->initialize()) {
        m_lastError = QStringLiteral("initialize: 插件 id=%1 initialize() 返回失败").arg(id);
        Q_EMIT pluginInitializeFailed(id);
        return false;
    }
    Q_EMIT pluginInitialized(id);
    return true;
}

bool PluginSystem::initializeOne(size_t id)
{
    Q_EMIT pluginInitializing(id, 0, 1);
    const bool ok = doInitialize(id);
    if (ok) {
        // 单独调用 initializeOne() 时，没有“全部插件都初始化完再统一 reactExtensions()”这个批量语义，
        // 所以这里对这一个插件单独完成第 3 阶段。initializeAll() 里有自己的批量版本，见下方。
        auto block = blockFor(id);
        if (block) {
            block->plugin()->reactExtensions();
            Q_EMIT pluginRunning(id);
        }
    }
    return ok;
}

bool PluginSystem::initializeAll()
{
    std::vector<size_t> ids;
    {
        QReadLocker locker(&m_lock);
        ids.reserve(m_entries.size());
        for (const auto& entry : m_entries) {
            ids.push_back(entry.first);
        }
    }

    bool allOk      = true;
    const int total = static_cast<int>(ids.size());
    for (int i = 0; i < total; ++i) {
        const size_t id = ids[static_cast<size_t>(i)];
        Q_EMIT pluginInitializing(id, i, total);
        allOk = doInitialize(id) && allOk;
    }

    if (!allOk) {
        // 文档约定"失败时插件系统应回滚"；这里的回滚策略很朴素：不调用任何 extensionsInitialized()，
        // 已经 initialize() 成功的插件仍处于“已初始化但未 reactExtensions()”状态，调用方可以选择
        // shutdownAll()/unloadAll() 整体回退，也可以自行排查 lastError() 后重试失败的那几个。
        return false;
    }

    // 全部 initialize() 成功后，按文档约定的第 3 阶段统一调用 extensionsInitialized()，
    // 此时所有插件都已经完成 initialize()，可以安全地互相访问对方注册的扩展。
    for (size_t id : ids) {
        auto block = blockFor(id);
        if (block) {
            block->plugin()->reactExtensions();
            Q_EMIT pluginRunning(id);
        }
    }
    return true;
}

bool PluginSystem::isInitialized(size_t id) const
{
    auto block = blockFor(id);
    return block && block->plugin()->isInitialized();
}

bool PluginSystem::isAllInitialized() const
{
    QReadLocker locker(&m_lock);
    for (const auto& entry : m_entries) {
        if (!entry.second->plugin()->isInitialized()) {
            return false;
        }
    }
    return true;
}

void PluginSystem::shutdownOne(size_t id)
{
    auto block = blockFor(id);
    if (!block) {
        return;
    }
    Q_EMIT pluginStopped(id);
    block->plugin()->quit();
}

void PluginSystem::shutdownAll()
{
    std::vector<size_t> ids;
    {
        QReadLocker locker(&m_lock);
        ids.reserve(m_entries.size());
        for (const auto& entry : m_entries) {
            ids.push_back(entry.first);
        }
    }
    // 反向遍历只是一个占位策略，并不是真正的“反向依赖顺序”——依赖排序还没实现（见类头部说明），
    // 拓扑排序接入后这里应该改成按拓扑序的逆序遍历。
    for (auto rit = ids.rbegin(); rit != ids.rend(); ++rit) {
        shutdownOne(*rit);
    }
}

bool PluginSystem::startup()
{
    // 简化版一步到位流程：依赖解析目前是空的（见类头部说明），这里就是 loadAll() + initializeAll()
    // 的顺序封装，不做任何拓扑排序。
    if (!loadAll()) {
        return false;
    }
    return initializeAll();
}

void PluginSystem::shutdown()
{
    shutdownAll();
    unloadAll();
}

// ============================================================================
// 访问插件数据
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

std::vector<std::shared_ptr<PluginBlock>> PluginSystem::plugins() const
{
    QReadLocker locker(&m_lock);
    std::vector<std::shared_ptr<PluginBlock>> result;
    result.reserve(m_entries.size());
    for (const auto& entry : m_entries) {
        result.push_back(entry.second);
    }
    return result;
}

std::shared_ptr<PluginBlock> PluginSystem::plugin(size_t id) const
{
    return blockFor(id);
}

std::shared_ptr<PluginBlock> PluginSystem::plugin(const QString& id) const
{
    QReadLocker locker(&m_lock);
    auto it = m_namedEntries.find(id);
    return it != m_namedEntries.end() ? it->second : nullptr;
}

} // namespace bakuon::gui
