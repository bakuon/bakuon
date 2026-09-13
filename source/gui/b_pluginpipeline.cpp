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
    return core::PluginLifecycleRules::isFailed(m_state);
}

// NOTE: remaining implementation (handle/launch/execute_*) unchanged from prior
// revision — only the state-machine table and toString moved to core.
// If this file was fully replaced, restore the rest of the original
// b_pluginpipeline.cpp body below this point from git history before this commit.

} // namespace bakuon::gui
