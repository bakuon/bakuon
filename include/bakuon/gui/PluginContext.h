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
    explicit PluginContext(QStringList arguments = {})
        : m_arguments(std::move(arguments))
    {
    }

    // 禁止拷贝：上下文仅在 initialize() 调用栈内有效
    PluginContext(const PluginContext&)            = delete;
    PluginContext& operator=(const PluginContext&) = delete;

    /**
     * @brief 获取插件启动命令行参数
     */
    const QStringList& arguments() const { return m_arguments; }

private:
    QStringList m_arguments;
};

} // namespace bakuon::gui
