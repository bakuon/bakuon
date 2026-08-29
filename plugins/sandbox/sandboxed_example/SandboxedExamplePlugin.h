#pragma once

#include <QtCore/QObject>

// 插件实现应当只依赖 include/bakuon/ 下的公开门面，
// 不要直接 #include "gui/b_xxx.h" / "sandbox/b_xxx.h"（那些是内部实现细节）。
#include "bakuon/gui/IPlugin.h"
#include "bakuon/sandbox/ISandboxCommandHandler.h"

namespace bakuon::plugins::sandboxed_example {

/**
 * @brief "血肉"部分的示例：把一段共享内存里的 float 数组原地求和，结果写回同一块内存。
 *
 * 用最简单的计算演示 ISandboxCommandHandler 的完整数据流转，真实场景里这里应该是
 * 音视频转码 / 点云处理 / 大文本分析这类真正需要隔离 + 高吞吐的计算——本类刻意保持
 * "傻瓜式"是为了让骨架部分的正确性一目了然，不被具体业务逻辑的复杂度掩盖。
 *
 * 输入/输出都复用同一块共享内存 Payload 区域：
 *   输入：N 个 float（小端，紧密排列，N = readInput().size() / sizeof(float)）
 *   输出：1 个 float（所有输入的和），写回后 writeResult() 会把 payloadLength 更新为 4
 */
class SumFloatsCommandHandler final : public bakuon::sandbox::ISandboxCommandHandler
{
public:
    [[nodiscard]] QString commandId() const override
    {
        return QStringLiteral("com.bakuon.example.sumFloats");
    }

    bool execute(bakuon::sandbox::ISandboxCommandContext &context) override;

    [[nodiscard]] QString errorMessage() const override { return m_error; }

private:
    QString m_error;
};

/**
 * @brief 沙箱示例插件：在 initialize() 里注册 SumFloatsCommandHandler。
 * @note 本插件设计上只应该被 sandbox_runtime 加载（走 SandboxSupervisor::start()），
 *       不建议被主程序直接加载——ISandboxCommandHandler 扩展点由 SandboxRuntime 统一
 *       创建，主程序进程内没有这个扩展点，registerExtension() 会静默失败（返回 nullptr
 *       被忽略），插件本身仍然能正常 initialize()/run()，只是命令处理器不会被真正用到。
 */
class SandboxedExamplePlugin final : public QObject, public bakuon::gui::IPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "com.bakuon.plugin" FILE "sandboxed_example_plugin.json")
    Q_INTERFACES(bakuon::gui::IPlugin)

public:
    [[nodiscard]] QString id() const override
    {
        return QStringLiteral("com.bakuon.sandboxed_example");
    }
    [[nodiscard]] QString name() const override
    {
        return QStringLiteral("Sandboxed Example Plugin");
    }
    [[nodiscard]] QString version() const override { return QStringLiteral("1.0.0"); }
    [[nodiscard]] QString description() const override;

    bool initialize(bakuon::gui::PluginContext &ctx) override;
    void extensionsInitialized() override;
    void shutdown() override;

private:
    std::shared_ptr<SumFloatsCommandHandler> m_handler;
};

} // namespace bakuon::plugins::sandboxed_example
