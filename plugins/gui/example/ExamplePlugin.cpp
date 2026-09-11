#include "ExamplePlugin.h"

namespace bakuon::plugins::gui {

QString ExamplePlugin::description() const
{
    return QStringLiteral("bakuon_add_plugin() 的最小可用示例，仅用于验证插件构建链路。");
}

bool ExamplePlugin::initialize(bakuon::gui::PluginContext& /*ctx*/)
{
    // 示例插件不注册任何扩展点，直接返回成功。
    qDebug() << Q_FUNC_INFO;
    return true;
}

void ExamplePlugin::extensionsInitialized()
{
    qDebug() << Q_FUNC_INFO;
}

void ExamplePlugin::shutdown()
{
    // 示例插件没有需要释放的资源。
    qDebug() << Q_FUNC_INFO;
}

} // namespace bakuon::plugins::gui
