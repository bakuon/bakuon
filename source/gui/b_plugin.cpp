#pragma once

#include "gui/b_plugin.h"

namespace bakuon::gui {

namespace {

// 元数据JSON键名常量
namespace MetaKey {
constexpr QLatin1String METADATA{"MetaData"};
constexpr QLatin1String ID{"Id"}; // 插件唯一标识
constexpr QLatin1String NAME{"Name"};
constexpr QLatin1String VERSION{"Version"};
constexpr QLatin1String COMPAT_VERSION{"CompatVersion"};
constexpr QLatin1String CATEGORY{"Category"};
constexpr QLatin1String DESCRIPTION{"Description"};
constexpr QLatin1String VENDOR{"Vendor"};
constexpr QLatin1String COPYRIGHT{"Copyright"};
constexpr QLatin1String LICENSE{"License"};
constexpr QLatin1String URL{"Url"};
constexpr QLatin1String PLATFORM{"Platform"};
constexpr QLatin1String EXPERIMENTAL{"Experimental"};
constexpr QLatin1String REQUIRED{"Required"};
constexpr QLatin1String DISABLED_BY_DEFAULT{"DisabledByDefault"};
constexpr QLatin1String DEPENDENCIES{"Dependencies"};
constexpr QLatin1String ARGUMENTS{"Arguments"};
// 依赖字段
constexpr QLatin1String DEP_ID{"Id"};
constexpr QLatin1String DEP_NAME{"Name"};
constexpr QLatin1String DEP_VERSION{"Version"};
constexpr QLatin1String DEP_TYPE{"Type"};
// 参数字段
constexpr QLatin1String ARG_NAME{"Name"};
constexpr QLatin1String ARG_OPTION{"Option"};
constexpr QLatin1String ARG_DESCRIPTION{"Description"};

constexpr QLatin1String EXPECTED_IID{"com.bakuon.plugin"};
} // namespace MetaKey

} // anonymous namespace

Plugin::Plugin(size_t id, QString filepath)
    : m_id(id)
    , m_filepath(std::move(filepath))
    , m_loader(std::make_unique<QPluginLoader>(m_filepath))
{
}

Plugin::~Plugin()
{
}

bool Plugin::load()
{
    return false;
}

bool Plugin::unload()
{
    return false;
}

bool Plugin::initialize()
{
    m_keepAlive = shared_from_this();

    PluginContext ctx;
    m_instance->initialize(ctx);

    return true;
}

void Plugin::reactExtensions()
{
    m_instance->extensionsInitialized();
}

void Plugin::quit()
{
    m_instance->shutdown();
    m_loader->unload();
    m_keepAlive.reset();
}

} // namespace bakuon::gui
