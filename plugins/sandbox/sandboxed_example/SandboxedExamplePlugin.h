#pragma once

#include <QtCore/QObject>
#include <QtWidgets/QWidget>

// 插件实现应当只依赖 include/bakuon/ 下的公开门面，
// 不要直接 #include "gui/b_xxx.h" / "sandbox/b_xxx.h"（那些是内部实现细节）。
#include "bakuon/gui/IPlugin.h"
#include "bakuon/sandbox/IGuiSurfaceHandler.h"
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
 * @brief 跨进程 GUI 合成的最小验证用例：一个自绘的、带点击计数器的 QWidget。
 *
 * 故意画得很简单（纯色背景 + 居中文字），不追求美观——这个类存在的唯一目的是
 * 用最少的代码同时验证两个方向都真正打通：
 *  1. 渲染方向（Sandbox → Host）：数字变化后，Host 侧显示的画面应该跟着更新；
 *  2. 输入方向（Host → Sandbox）：鼠标点击从 Host 转发过来、真的送达了这个
 *     widget 的 mousePressEvent()，而不是石沉大海。
 * 只要点击一次、数字真的从 0 变成 1，链路就是通的。
 */
class ClickCounterWidget final : public QWidget
{
    Q_OBJECT
public:
    explicit ClickCounterWidget(QWidget *parent = nullptr);

    [[nodiscard]] int clickCount() const noexcept { return m_clickCount; }

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;

private:
    int m_clickCount = 0;
};

/// IGuiSurfaceHandler 的最小实现：把 ClickCounterWidget 包出去。
class ClickCounterSurfaceHandler final : public bakuon::sandbox::IGuiSurfaceHandler
{
public:
    explicit ClickCounterSurfaceHandler(QObject *parentForWidget);
    ~ClickCounterSurfaceHandler() override;

    QWidget *surfaceWidget() override { return m_widget; }

private:
    // 没有 parent（构造时传的是 nullptr，见 .cpp 里的说明），生命周期由本类自己
    // 通过析构函数管理，不依赖 Qt 的父子对象自动清理机制。
    ClickCounterWidget *m_widget;
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
    std::shared_ptr<ClickCounterSurfaceHandler> m_surfaceHandler;
};

} // namespace bakuon::plugins::sandboxed_example
