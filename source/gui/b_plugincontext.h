#pragma once

#include <QtCore/QStringList>

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
 */
class PluginContext
{
public:
    explicit PluginContext(const QStringList& arguments = {})
        : m_arguments(arguments)
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
    const QStringList& m_arguments;
};

} // namespace bakuon::gui
