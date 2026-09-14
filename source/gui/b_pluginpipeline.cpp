#include "gui/b_pluginpipeline.h"

#include <QtCore/QDebug>
#include <QtCore/QJsonObject>
#include <QtCore/QLibrary>

#include "gui/b_extensionsystem.h"

namespace bakuon::gui {

QString toString(PluginState state)
{
    const auto sv = core::toStringView(state);
    return QString::fromUtf8(sv.data(), static_cast<int>(sv.size()));
}

static bool shouldLoad(PluginEnablePolicy policy)
{
    switch (policy) {
    case PluginEnablePolicy::ForceEnabled:
    case PluginEnablePolicy::EnabledByDefault : return true;
    case PluginEnablePolicy::ForceDisabled    :
    case PluginEnablePolicy::DisabledByDefault: return false;
    default                                   : break;
    }
    return false;
}

PluginPipeline::PluginPipeline(size_t id, QString filePath, QObject *parent)
    : QObject(parent)
    , m_id(id)
    , m_filePath(std::move(filePath))
{
}

PluginPipeline::PluginPipeline(size_t id, std::shared_ptr<IPlugin> instance, QObject *parent)
    : QObject(parent)
    , m_id(id)
    , m_instance(std::move(instance))
{
    if (m_instance) {
        m_metadata.id          = m_instance->id();
        m_metadata.name        = m_instance->name();
        m_metadata.version     = m_instance->version();
        m_metadata.description = m_instance->description();
        for (const QString &depId : m_instance->dependencies()) {
            PluginDependency dep;
            dep.id   = depId;
            dep.type = PluginDependency::RequireType::Required;
            m_metadata.dependencies.push_back(std::move(dep));
        }
        m_state = PluginState::Validated;
    }
}

PluginPipeline::~PluginPipeline() = default;

bool PluginPipeline::isFailed() const noexcept
{
    return PluginLifecycleRules::isFailed(m_state);
}

bool PluginPipeline::handle(PluginEvent event)
{
    m_pendingEvents.push_back(event);
    return processQueue();
}

bool PluginPipeline::launch()
{
    if (m_state == PluginState::Idle) {
        return handle(PluginEvent::StartDiscover);
    }
    if (m_state == PluginState::Validated || m_state == PluginState::ResolveFailed) {
        return handle(PluginEvent::StartResolve);
    }
    if (m_state == PluginState::Initialized) {
        return true;
    }
    return handle(PluginEvent::StartDiscover);
}

bool PluginPipeline::run()
{
    return handle(PluginEvent::StartRun);
}

bool PluginPipeline::stop()
{
    return handle(PluginEvent::StartStop);
}

bool PluginPipeline::unload()
{
    return handle(PluginEvent::StartUnload);
}

bool PluginPipeline::processQueue()
{
    if (m_processing) {
        return true;
    }
    m_processing = true;
    bool ok      = true;
    while (!m_pendingEvents.empty()) {
        const PluginEvent event = m_pendingEvents.front();
        m_pendingEvents.pop_front();
        const auto next = PluginLifecycleRules::nextState(m_state, event);
        if (!next) {
            m_lastError = QStringLiteral("非法状态转换：%1 无法响应该事件").arg(toString(m_state));
            ok          = false;
            continue;
        }
        m_state = *next;
        recordTimestamp(m_state);
        Q_EMIT stateChanged(m_id, m_state);
        if (isFailed()) {
            Q_EMIT failed(m_id, m_state, m_lastError);
        }
        if (m_state == PluginState::Running) {
            Q_EMIT running(m_id);
        }
        if (!reactState()) {
            ok = false;
        }
    }
    m_processing = false;
    return ok;
}

bool PluginPipeline::reactState()
{
    switch (m_state) {
    case PluginState::Discovering : executeDiscover(); break;
    case PluginState::Discovered  : return handle(PluginEvent::StartValidate);
    case PluginState::Validating  : executeValidate(); break;
    case PluginState::Validated   : return handle(PluginEvent::StartResolve);
    case PluginState::Resolving   : executeResolve(); break;
    case PluginState::Resolved    : return handle(PluginEvent::StartLoad);
    case PluginState::Loading     : executeLoad(); break;
    case PluginState::Loaded      : return handle(PluginEvent::StartInitialize);
    case PluginState::Initializing: executeInitialize(); break;
    case PluginState::Initialized : break;
    case PluginState::Running     : break;
    case PluginState::Stopping    : executeStop(); break;
    case PluginState::Stopped     : break;
    case PluginState::Unloading   : executeUnload(); break;
    default                       : break;
    }
    return true;
}

void PluginPipeline::recordTimestamp(PluginState state)
{
    const QDateTime now = QDateTime::currentDateTimeUtc();
    switch (state) {
    case PluginState::Discovered : m_discoveredAt = now; break;
    case PluginState::Loaded     : m_loadedAt = now; break;
    case PluginState::Initialized: m_initializedAt = now; break;
    case PluginState::Running    : m_runningAt = now; break;
    default                      : break;
    }
}

void PluginPipeline::executeDiscover()
{
    if (m_filePath.isEmpty()) {
        m_lastError = QStringLiteral("empty file path");
        handle(PluginEvent::Fail);
        return;
    }
    handle(PluginEvent::Success);
}

void PluginPipeline::executeValidate()
{
    // 只读取元数据，不会触发 dlopen/instance()，因此廉价，可以对大量候选文件批量做
    // （PluginSystem::registerDirectory() 就是这么用的）。
    QPluginLoader probe(m_filePath);
    const QJsonObject root = probe.metaData();

    if (root.isEmpty()) {
        m_lastError = QStringLiteral("无法读取插件元数据（不是 Qt 插件，或已损坏）");
        handle(PluginEvent::Fail);
        return;
    }
    if (root.value(QLatin1String("IID")).toString() != QLatin1String("com.bakuon.plugin")) {
        m_lastError = QStringLiteral("IID 不匹配，不是 bakuon 插件");
        handle(PluginEvent::Fail);
        return;
    }

    QString error;
    auto meta = parsePluginMetadataJson(root.value(QLatin1String("MetaData")).toObject(), &error);
    if (!meta) {
        m_lastError = error;
        handle(PluginEvent::Fail);
        return;
    }
    meta->filePath = m_filePath;
    m_metadata     = std::move(*meta);
    handle(PluginEvent::Success);
}

void PluginPipeline::executeResolve()
{
    if (m_resolveHook) {
        if (auto err = m_resolveHook(m_metadata)) {
            m_lastError = *err;
            handle(PluginEvent::Fail);
            return;
        }
    }
    handle(PluginEvent::Success);
}

void PluginPipeline::executeLoad()
{
    if (m_instance) {
        handle(PluginEvent::Success);
        return;
    }
    m_loader     = std::make_unique<QPluginLoader>(m_filePath);
    QObject *obj = m_loader->instance();
    if (!obj) {
        m_lastError = m_loader->errorString();
        handle(PluginEvent::Fail);
        return;
    }
    m_instance = std::shared_ptr<IPlugin>(qobject_cast<IPlugin *>(obj), [](IPlugin *) {});
    if (!m_instance) {
        m_lastError = QStringLiteral("plugin does not implement IPlugin");
        handle(PluginEvent::Fail);
        return;
    }
    handle(PluginEvent::Success);
}

void PluginPipeline::executeInitialize()
{
    if (!m_instance) {
        m_lastError = QStringLiteral("no instance");
        handle(PluginEvent::Fail);
        return;
    }
    PluginContext ctx(m_argumentValues, &ExtensionSystem::instance());
    if (!m_instance->initialize(ctx)) {
        m_lastError = QStringLiteral("initialize() returned false");
        handle(PluginEvent::Fail);
        return;
    }
    handle(PluginEvent::Success);
}

void PluginPipeline::executeStop()
{
    if (m_instance) {
        m_instance->shutdown();
    }
    handle(PluginEvent::Success);
}

void PluginPipeline::executeUnload()
{
    m_instance.reset();
    if (m_loader) {
        m_loader->unload();
        m_loader.reset();
    }
    handle(PluginEvent::Success);
}

} // namespace bakuon::gui
