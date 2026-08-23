#include "gui/b_pluginsystem.h"

#include "gui/b_plugin.h"
#include "gui/b_pluginblock.h"
#include "gui/b_plugindiscoverer.h"

namespace bakuon::gui {

// 仅测试创建：改用 PluginBlock::create()（正确的组合分配 + 生命周期管理入口），
// 不再直接调用 constructBlock/constructManaged —— 那两个是 PluginBlock 的私有实现细节，
// 只保证“分配+构造”，不负责对应的析构+释放，正确的用法只能通过 create() 走完整路径。
static std::shared_ptr<PluginBlock> createPlugin(std::size_t id, const QString& filePath)
{
    return PluginBlock::create(id, filePath);
}

PluginSystem::PluginSystem(QObject* parent)
    : QObject(parent)
{
}
} // namespace bakuon::gui
