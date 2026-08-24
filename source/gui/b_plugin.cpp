#include "gui/b_plugin.h"

namespace bakuon::gui {

Plugin::Plugin(size_t id, QString filepath)
    : m_id(id)
    , m_filepath(std::move(filepath))
    // 内置插件占位（filepath 为空）不需要 QPluginLoader；只有真正指向动态库文件时才创建，
    // 避免每个内置插件都白白构造一个用不到的 QPluginLoader。
    , m_loader(m_filepath.isEmpty() ? nullptr : std::make_unique<QPluginLoader>(m_filepath))
{
}

Plugin::Plugin(size_t id, std::shared_ptr<IPlugin> builtin)
    : m_id(id)
    , m_instance(std::move(builtin))
    , m_loader(nullptr)
{
}

Plugin::~Plugin()
{
    // 安全网：正常应该由调用方在 PluginSystem::shutdown() 里显式 quit() 之后再析构。
    // 如果走到这里 m_instance 还非空，说明有插件没有被正确 quit()，尽量兜底调用一次 unload()，
    // 但这里已经处于析构阶段，不再抛异常、不再报告失败，只做尽力而为的清理。
    if (m_instance) {
        unload();
    }
}

bool Plugin::load()
{
    if (m_instance) {
        return true; // 已加载（对内置插件而言，构造完成即已“加载”）
    }
    if (!m_loader) {
        return false; // 既没有内置实例，也没有可加载的动态库文件
    }
    if (!m_loader->load()) {
        return false;
    }

    auto* raw = qobject_cast<IPlugin*>(m_loader->instance());
    if (!raw) {
        // instance() 返回的对象没有实现 IPlugin 接口（IID 不匹配 / 没有 Q_INTERFACES），
        // 视为加载失败，卸载已经加载的动态库，避免残留。
        m_loader->unload();
        return false;
    }

    // instance() 返回对象的生命周期由 QPluginLoader 管理（unload() 时销毁），
    // 这里用空操作删除器把裸指针包进 shared_ptr，绝不能让这个 shared_ptr 自己去 delete 它。
    m_instance = std::shared_ptr<IPlugin>(raw, [](IPlugin*) {});
    return true;
}

bool Plugin::unload()
{
    if (!m_instance) {
        return true; // 幂等：本来就没加载
    }
    m_instance.reset();
    m_initialized = false;

    if (!m_loader) {
        // 内置插件没有动态库可卸载；到这里视为该 Plugin 实例已不可再用。
        return true;
    }
    return !m_loader->isLoaded() || m_loader->unload();
}

bool Plugin::initialize()
{
    if (!m_instance) {
        return false; // 尚未 load() 成功，不应该走到 initialize()
    }
    // TODO(PluginSystem): 目前没有把启动命令行参数透传给插件，PluginContext 一直是空参数构造；
    // 等 PluginSystem::startup() 那边确定参数来源（例如从 QCoreApplication::arguments() 或
    // 插件自己的 --plugin-xxx-arg 语法解析）之后，再把参数传进来。
    PluginContext ctx;
    m_initialized = m_instance->initialize(ctx);
    return m_initialized;
}

void Plugin::reactExtensions()
{
    if (m_instance) {
        m_instance->extensionsInitialized();
    }
}

void Plugin::quit()
{
    if (m_instance) {
        m_instance->shutdown();
    }
    unload();
}

} // namespace bakuon::gui
