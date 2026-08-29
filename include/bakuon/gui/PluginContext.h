#pragma once

#include <QtCore/QStringList>

// ============================================================================
// bakuon::gui 插件开发 SDK —— 门面头文件（facade）
// 参见 include/bakuon/gui/Plugin.h 顶部关于门面 / 内部实现分层的说明。
//
// PluginContext 在 Plugin::initialize(PluginContext&) 中传入，
// 插件开发者需要它才能拿到启动参数等能力，因此和 Plugin 一起被列入公开门面。
// ============================================================================

namespace bakuon::gui {

class IExtensionSystem;

/**
 * @brief 插件初始化上下文
 *
 * 在 IPlugin::initialize() 阶段提供给插件的只读上下文。
 * 封装了插件初始化所需的所有能力，同时隔离插件对 Application 内部实现的直接依赖。
 *
 * 插件可通过此上下文：
 *  - 访问应用运行期服务（IApplication::getService<T>()）
 *  - 注册自身的服务提供者（IApplication::registerProvider()）
 *  - 获取启动命令行参数
 *
 * 设计原则：
 *  - 接口稳定：未来扩展新能力只需添加成员，不改 initialize() 签名
 *  - 轻量传递：不持有所有权，仅持有引用
 *
 * @warning 早期版本这里用 `const QStringList&` 引用绑定默认参数 `{}`，
 *          临时对象在构造函数调用结束后就销毁了，导致 m_arguments 变成悬垂引用——
 *          Plugin::initialize() 里正是用 `PluginContext ctx;`（走默认参数）调用的，
 *          相当于每次插件初始化都会触发未定义行为。现在改成按值持有，从根上避免这个坑；
 *          不允许拷贝 PluginContext 本身仍然保留（见下方），是为了不鼓励插件把 ctx
 *          存到 initialize() 调用栈之外的地方长期持有。
 */
class PluginContext
{
public:
    /**
     * @brief 插件上下文
     * @param arguments       插件启动命令行参数
     * @param extensionSystem 当前进程内的 IExtensionSystem 实例（可为 nullptr，见下方说明）。
     *
     * @note 为什么是指针而不是原来 TODO 里设想的引用：extensionSystem() 允许返回 nullptr——
     *       并不是所有调用 initialize() 的场景都一定有扩展系统可用（比如未来如果出现极简的
     *       "纯命令行工具、不需要任何扩展点"的宿主场景）。指针能自然表达"可能没有"，
     *       引用做不到，插件侧用之前必须判空，这是刻意的、比引用更诚实的接口。
     *
     * @note 为什么必须显式注入、不能让插件自己再调一次 ExtensionSystem::instance()：
     *       ExtensionSystem::instance() 是 Meyers' Singleton，其"进程内唯一"的保证只在
     *       *同一个* 链接产物内成立——一旦插件是被 dlopen() 进来的独立 .so，且 bakuon::gui
     *       是以 STATIC 库形式分别静态链接进宿主可执行文件和插件 .so 两份（当前就是这样），
     *       两边各自持有一份完全独立的 ExtensionSystem::instance() 静态局部对象，
     *       内存地址不同，插件在自己 .so 里调用 instance() 拿到的是"自己那一份"，
     *       根本不是宿主进程真正在用的那一份，注册的扩展点对宿主完全不可见。
     *       把宿主进程手里那份 IExtensionSystem* 显式通过 PluginContext 传进来，
     *       插件侧只做虚函数指针调用（ABI 兼容、不依赖任何跨 .so 的 ODR 合并），
     *       是唯一在"bakuon::gui 保持 STATIC 库"这个现状下仍然正确的做法——
     *       这同时也是这个类之前留的 "@todo 注入 IExtensionSystem 引用" 的正式实现。
     */
    explicit PluginContext(QStringList arguments = {}, IExtensionSystem* extensionSystem = nullptr)
        : m_arguments(std::move(arguments))
        , m_extensionSystem(extensionSystem)
    {
    }

    // 禁止拷贝：上下文仅在 initialize() 调用栈内有效
    PluginContext(const PluginContext&)            = delete;
    PluginContext& operator=(const PluginContext&) = delete;

    /**
     * @brief 获取插件启动命令行参数
     */
    const QStringList& arguments() const { return m_arguments; }

    /**
     * @brief 获取当前进程内的 IExtensionSystem 实例，用于注册/查询扩展点。
     * @return 调用方（宿主 PluginPipeline）注入的实例；理论上不会是 nullptr，
     *         但插件侧仍应判空后再使用，避免极端场景下的空指针解引用。
     */
    IExtensionSystem* extensionSystem() const { return m_extensionSystem; }

private:
    QStringList m_arguments;
    IExtensionSystem* m_extensionSystem = nullptr;
};

} // namespace bakuon::gui
