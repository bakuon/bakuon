#include "SandboxedExamplePlugin.h"

#include <QtCore/QByteArray>
#include <QtCore/QDebug>

#include "bakuon/gui/IExtensionPoint.h"
#include "bakuon/gui/IExtensionSystem.h"

namespace bakuon::plugins::sandboxed_example {

bool SumFloatsCommandHandler::execute(bakuon::sandbox::ISandboxCommandContext &context)
{
    const QByteArray input = context.readInput();
    if (input.size() % static_cast<qsizetype>(sizeof(float)) != 0) {
        m_error = QStringLiteral("输入长度 %1 不是 sizeof(float) 的整数倍").arg(input.size());
        return false;
    }

    const auto *values    = reinterpret_cast<const float *>(input.constData());
    const qsizetype count = input.size() / static_cast<qsizetype>(sizeof(float));

    double sum = 0.0; // 用 double 累加，避免大量 float 相加时的精度损失
    for (qsizetype i = 0; i < count; ++i) {
        sum += static_cast<double>(values[i]);
    }
    const auto result = static_cast<float>(sum);

    QByteArray output(reinterpret_cast<const char *>(&result), sizeof(result));
    if (!context.writeResult(output)) {
        m_error = QStringLiteral("共享内存容量不足以写回结果（需要至少 %1 字节，当前容量 %2）")
                      .arg(sizeof(result))
                      .arg(context.sharedMemoryCapacity());
        return false;
    }
    return true;
}

QString SandboxedExamplePlugin::description() const
{
    return QStringLiteral(
        "演示 ISandboxCommandHandler 扩展点的示例插件：注册一个把共享内存里的 float "
        "数组原地求和的命令处理器，供 sandbox_runtime 子进程加载后通过 "
        "PluginSandboxControl::executeCommand() 调用。");
}

bool SandboxedExamplePlugin::initialize(bakuon::gui::PluginContext &ctx)
{
    bakuon::gui::IExtensionSystem *extensionSystem = ctx.extensionSystem();
    if (!extensionSystem) {
        qWarning() << Q_FUNC_INFO << "PluginContext 未注入 IExtensionSystem，跳过命令处理器注册";
        return true;
    }

    auto point = extensionSystem->extensionPoint<bakuon::sandbox::ISandboxCommandHandler>();
    if (!point) {
        // 不是致命错误：本插件如果被当作普通进程内插件加载（不经过 sandbox_runtime），
        // 这个扩展点确实不存在，此时只是"求和命令"功能不可用，插件其余部分不受影响。
        qWarning() << Q_FUNC_INFO
                   << "找不到 ISandboxCommandHandler 扩展点，"
                      "本插件可能没有运行在 sandbox_runtime 子进程内";
        return true;
    }

    m_handler = std::make_shared<SumFloatsCommandHandler>();
    point->registerExtension(m_handler, 0);
    return true;
}

void SandboxedExamplePlugin::extensionsInitialized()
{
    qInfo() << Q_FUNC_INFO;
}

void SandboxedExamplePlugin::shutdown()
{
    m_handler.reset();
}

} // namespace bakuon::plugins::sandboxed_example
