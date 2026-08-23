#include "gui/b_pluginsystem.h"

#include "gui/b_plugin.h"
#include "gui/b_pluginblock.h"
#include "gui/b_plugindiscoverer.h"

namespace bakuon::gui {

// 仅测试创建
PluginBlock* createPlugin(std::size_t id, const QString& filePath)
{
    auto* mem   = PluginBlock::allocate<Plugin>();
    auto* block = PluginBlock::construct_block(mem, id);
    PluginBlock::construct_managed<Plugin>(mem, id, filePath);
    return block;
}

PluginSystem::PluginSystem(QObject* parent)
    : QObject(parent)
{
}
} // namespace bakuon::gui
