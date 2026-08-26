#include "gui/b_pluginpipeline.h"

#include <QtCore/QDebug>
#include <QtCore/QJsonObject>
#include <QtCore/QLibrary>

namespace bakuon::gui {

QString toString(PluginState state)
{
    // clang(-Wswitch-enum)
    switch (state) {
    case PluginState::Idle            : return QStringLiteral("Idle");
    case PluginState::Discovering     : return QStringLiteral("Discovering");
    case PluginState::Discovered      : return QStringLiteral("Discovered");
    case PluginState::DiscoverFailed  : return QStringLiteral("DiscoverFailed");
    case PluginState::Validating      : return QStringLiteral("Validating");
    case PluginState::Validated       : return QStringLiteral("Validated");
    case PluginState::ValidateFailed  : return QStringLiteral("ValidateFailed");
    case PluginState::Resolving       : return QStringLiteral("Resolving");
    case PluginState::Resolved        : return QStringLiteral("Resolved");
    case PluginState::ResolveFailed   : return QStringLiteral("ResolveFailed");
    case PluginState::Loading         : return QStringLiteral("Loading");
    case PluginState::Loaded          : return QStringLiteral("Loaded");
    case PluginState::LoadFailed      : return QStringLiteral("LoadFailed");
    case PluginState::Initializing    : return QStringLiteral("Initializing");
    case PluginState::Initialized     : return QStringLiteral("Initialized");
    case PluginState::InitializeFailed: return QStringLiteral("InitializeFailed");
    case PluginState::Running         : return QStringLiteral("Running");
    case PluginState::RunFailed       : return QStringLiteral("RunFailed");
    case PluginState::Stopping        : return QStringLiteral("Stopping");
    case PluginState::Stopped         : return QStringLiteral("Stopped");
    case PluginState::StopFailed      : return QStringLiteral("StopFailed");
    case PluginState::Unloading       : return QStringLiteral("Unloading");
    case PluginState::Unloaded        : return QStringLiteral("Unloaded");
    case PluginState::UnloadFailed    : return QStringLiteral("UnloadFailed");
    }
    return QStringLiteral("<unknown PluginState>");
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
    // 内置插件没有文件可发现/校验，metadata 直接从实例读取，状态跳到 Validated——
    // launch() 会识别这个初始状态、从 StartResolve 开始，其余阶段和动态库插件完全一致。
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
    }
    m_state = PluginState::Validated;
}

PluginPipeline::~PluginPipeline() = default;

bool PluginPipeline::isFailed() const noexcept
{
    // clang(-Wswitch-enum)
    switch (m_state) {
    case PluginState::DiscoverFailed:
    case PluginState::ValidateFailed:
    case PluginState::ResolveFailed:
    case PluginState::LoadFailed:
    case PluginState::InitializeFailed:
    case PluginState::RunFailed:
    case PluginState::StopFailed:
    case PluginState::UnloadFailed    : return true;
    default                           : return false;
    }
}

bool PluginPipeline::launch()
{
    if (m_state == PluginState::Idle) {
        return handle(PluginEvent::StartDiscover);
    }
    // 内置插件构造完就是 Validated；重试场景（比如上次卡在 ResolveFailed）也可能落到这里，
    // handle() 会根据 PluginLifecycleRules 校验这是不是一个合法转换，不合法就返回 false，不会误触发。
    return handle(PluginEvent::StartResolve);
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

bool PluginPipeline::handle(PluginEvent event)
{
    m_pendingEvents.push_back(event);
    if (m_processing) {
        // 已经有一个 processQueue() 在栈上（典型场景：execute_xxx() 内部调用 handle(Success)），
        // 直接把事件排进队列、由外层循环消费即可，不递归调用 processQueue()。
        return true;
    }
    return processQueue();
}

bool PluginPipeline::processQueue()
{
    m_processing   = true;
    bool overallOk = true;

    while (!m_pendingEvents.empty()) {
        const PluginEvent event = m_pendingEvents.front();
        m_pendingEvents.pop_front();

        const auto next = PluginLifecycleRules::nextState(m_state, event);
        if (!next) {
            m_lastError = QStringLiteral("非法状态转换：%1 无法响应该事件").arg(toString(m_state));
            qDebug() << "[PluginPipeline]" << m_id << m_lastError;
            overallOk = false;
            continue; // 忽略这个非法事件，继续处理队列里可能还有的其它事件
        }

        m_state = *next;
        recordTimestamp(m_state);
        Q_EMIT stateChanged(m_id, m_state);

        if (!stateReact()) {
            overallOk = false;
        }
        if (isFailed()) {
            // 转换本身合法，但业务结果落在了 *Failed 态——对调用方而言这也是"没成功"，
            // 不能只把 overallOk 的判定局限于"事件是否被状态机接受"。
            overallOk = false;
        }
    }

    m_processing = false;
    return overallOk;
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

bool PluginPipeline::stateReact()
{
    // clang(-Wswitch-enum)
    switch (m_state) {
    // 过程态：执行对应的同步业务动作，动作内部会调用 handle(Success/Fail) 自行上报结果。
    case PluginState::Discovering : executeDiscover(); break;
    case PluginState::Validating  : executeValidate(); break;
    case PluginState::Resolving   : executeResolve(); break;
    case PluginState::Loading     : executeLoad(); break;
    case PluginState::Initializing: executeInitialize(); break;
    case PluginState::Stopping    : executeStop(); break;
    case PluginState::Unloading   : executeUnload(); break;

    // 稳定态：自动投递下一阶段的 Start 事件，形成链式推进。
    case PluginState::Discovered  : handle(PluginEvent::StartValidate); break;
    case PluginState::Validated   : handle(PluginEvent::StartResolve); break;
    case PluginState::Resolved    : handle(PluginEvent::StartLoad); break;
    case PluginState::Loaded      : handle(PluginEvent::StartInitialize); break;

    /**
     * @issue Stopped 状态下执行 StartRun → Running（即不卸载而重新启用）会引发
     * 重入问题（extensionsInitialized() 是否会再次触发？这是否安全？），
     * IPlugin 的契约并未对此作出规定，目前不知如何约定，先把这个功能收回只保留真正必要的部分。
     */

    // Running 的“业务动作”是同步、无失败返回值的 extensionsInitialized()，
    // 不需要单独的 "-ing" 阶段，进入时直接执行。
    case PluginState::Running:
        if (m_instance) {
            m_instance->extensionsInitialized();
        }
        Q_EMIT running(m_id);
        break;

    // 刻意不自动前进的两处停留点，见头文件类注释。
    case PluginState::Initialized:
    case PluginState::Stopped:
    case PluginState::Unloaded        : break;

    // 失败态：不自动重试，等待外部通过 handle(StartXxx) 重新投递重试，或直接放弃。
    case PluginState::DiscoverFailed  :
    case PluginState::ValidateFailed  :
    case PluginState::ResolveFailed   :
    case PluginState::LoadFailed      :
    case PluginState::InitializeFailed:
    case PluginState::RunFailed       :
    case PluginState::StopFailed      :
    case PluginState::UnloadFailed    : Q_EMIT failed(m_id, m_state, m_lastError); break;

    default                           : break;
    }
    // 具体成功/失败由各 execute_*() 通过 handle(Success/Fail) 自行上报并驱动状态继续前进，
    // processQueue() 的循环本身才是 overallOk 的真正来源，这里的返回值不代表最终结果。
    return true;
}

void PluginPipeline::executeDiscover()
{
    if (!QLibrary::isLibrary(m_filePath)) {
        m_lastError = QStringLiteral("不是有效的动态库文件: %1").arg(m_filePath);
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
        if (auto reason = m_resolveHook(m_metadata)) {
            m_lastError = *reason;
            handle(PluginEvent::Fail);
            return;
        }
    }
    handle(PluginEvent::Success);
}

void PluginPipeline::executeLoad()
{
    if (m_instance) {
        // 内置插件：构造时已经绑定好实例，视为"已加载"，直接成功（幂等，重试时也一样）。
        handle(PluginEvent::Success);
        return;
    }

    if (!m_loader) {
        m_loader = std::make_unique<QPluginLoader>(m_filePath);
    }
    if (!m_loader->load()) {
        m_lastError = m_loader->errorString();
        handle(PluginEvent::Fail);
        return;
    }

    auto *raw = qobject_cast<IPlugin *>(m_loader->instance());
    if (!raw) {
        m_lastError = QStringLiteral("动态库已加载，但导出的对象没有实现 IPlugin 接口");
        m_loader->unload();
        handle(PluginEvent::Fail);
        return;
    }

    // instance() 返回对象的生命周期由 QPluginLoader 管理（unload() 时销毁），
    // 这里用空操作删除器包进 shared_ptr，绝不能让这个 shared_ptr 自己去 delete 它。
    m_instance = std::shared_ptr<IPlugin>(raw, [](IPlugin *) {});
    handle(PluginEvent::Success);
}

void PluginPipeline::executeInitialize()
{
    if (!m_instance) {
        m_lastError = QStringLiteral("内部错误：Initializing 阶段 m_instance 为空");
        handle(PluginEvent::Fail);
        return;
    }

    // 参数来源见 PluginSystem::argumentsFor()：按 "--plugin:<id>.<rest>" 的约定从全局命令行里
    // 筛出属于这个插件的部分（剥掉前缀还原成 "--<rest>"），外加所有不带 "--plugin:" 前缀的全局参数。
    // 单独使用 PluginPipeline（不经过 PluginSystem）时，m_argumentValues 默认空，除非调用方自己
    // 用 setArgumentValues() 传值。
    PluginContext ctx(m_argumentValues);
    if (!m_instance->initialize(ctx)) {
        m_lastError = QStringLiteral("IPlugin::initialize() 返回失败");
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

    if (m_loader && m_loader->isLoaded()) {
        if (!m_loader->unload()) {
            m_lastError = QStringLiteral("卸载动态库失败: %1").arg(m_loader->errorString());
            handle(PluginEvent::Fail);
            return;
        }
    }
    handle(PluginEvent::Success);
}

} // namespace bakuon::gui
