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
     * @note 为什么仍然显式注入、不直接让插件自己再调一次 ExtensionSystem::instance()：
     *       ExtensionSystem::instance() 是 Meyers' Singleton，其"进程内唯一"的保证只在
     *       *同一份代码* 只被加载一次的前提下成立。早期 bakuon::gui 还是 STATIC 库时，
     *       它会被分别静态链接进宿主可执行文件和每一个插件 .so，两边各自持有一份完全独立的
     *       ExtensionSystem::instance() 静态局部对象，内存地址不同，插件在自己 .so 里调用
     *       instance() 拿到的是"自己那一份"，根本不是宿主进程真正在用的那一份，注册的扩展点
     *       对宿主完全不可见——这也是当初这份注释重点强调的坑。
     *
     *       现在 bakuon::gui 已经改为 SHARED 库（见 source/gui/CMakeLists.txt 的
     *       bakuon_add_module(... SHARED) 和 b_extensionsystem.h 里 ExtensionSystem 类
     *       BAKUON_GUI_EXPORT 的说明）：只要插件是动态链接到同一份 gui.dll/.so（而不是
     *       静态链接各自打包一份），ExtensionSystem::instance() 天然就是进程内唯一的，
     *       上面那个坑已经从架构上被消除了。尽管如此，这里仍然保留显式注入而不是让插件直接
     *       调用 instance()，原因是：
     *        1. 解耦：插件代码不应该硬编码依赖一个具体的全局单例访问路径，PluginContext
     *           才是插件与宿主之间唯一被明确定义的契约边界，方便未来替换实现（比如某些
     *           场景需要每个插件拿到经过包装/受限的 IExtensionSystem 视图）。
     *        2. 可测试性：单元测试可以注入一个假的 IExtensionSystem*，不需要触碰真实的
     *           全局单例状态。
     *        3. 防御性：万一将来出现"某个插件被要求以 STATIC 方式链接自己私有的一份
     *           bakuon::gui"这类特殊场景（比如极端隔离需求），显式注入的代码路径依然正确，
     *           不会静默退化回本节开头描述的那个坑。
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
