#include "SandboxedExamplePlugin.h"

#include <QtCore/QByteArray>
#include <QtCore/QDebug>
#include <QtGui/QMouseEvent>
#include <QtGui/QPainter>

#include "bakuon/gui/IExtensionPoint.h"
#include "bakuon/gui/IExtensionSystem.h"

namespace bakuon::plugins::sandboxed_example {

ClickCounterWidget::ClickCounterWidget(QWidget *parent)
    : QWidget(parent)
{
}

void ClickCounterWidget::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event)
    QPainter painter(this);
    painter.fillRect(rect(), QColor(40, 90, 160));

    painter.setPen(Qt::white);
    QFont font = painter.font();
    font.setPointSize(24);
    painter.setFont(font);
    painter.drawText(rect(), Qt::AlignCenter,
                     QStringLiteral("点击次数：%1").arg(m_clickCount));
}

void ClickCounterWidget::mousePressEvent(QMouseEvent *event)
{
    Q_UNUSED(event)
    ++m_clickCount;
    update(); // 触发一次 paintEvent；SandboxRuntime 的定时抓帧会在下一个 tick 里带上这次变化
}

ClickCounterSurfaceHandler::ClickCounterSurfaceHandler(QObject *parentForWidget)
{
    // 不给 widget 设 Qt parent：它从来不会被真正 show()（活在 offscreen 平台下，
    // 只被 SandboxRuntime grab() 抓像素），生命周期完全由本类自己通过析构函数管理，
    // 比借用 Qt 父子对象机制更直接、不容易和插件自身的 QObject 树产生歧义。
    Q_UNUSED(parentForWidget)
    m_widget = new ClickCounterWidget();
}

ClickCounterSurfaceHandler::~ClickCounterSurfaceHandler()
{
    delete m_widget;
}

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
        "演示 ISandboxCommandHandler 和 IGuiSurfaceHandler 两个扩展点的示例插件："
        "既注册了一个把共享内存里的 float 数组原地求和的命令处理器，"
        "也注册了一个自绘的点击计数器 widget，用于验证跨进程 GUI 合成链路。");
}

bool SandboxedExamplePlugin::initialize(bakuon::gui::PluginContext &ctx)
{
    bakuon::gui::IExtensionSystem *extensionSystem = ctx.extensionSystem();
    if (!extensionSystem) {
        qWarning() << Q_FUNC_INFO << "PluginContext 未注入 IExtensionSystem，跳过扩展点注册";
        return true;
    }

    if (auto point = extensionSystem->extensionPoint<bakuon::sandbox::ISandboxCommandHandler>()) {
        m_handler = std::make_shared<SumFloatsCommandHandler>();
        point->registerExtension(m_handler, 0);
    } else {
        // 不是致命错误：本插件如果被当作普通进程内插件加载（不经过 sandbox_runtime），
        // 这个扩展点确实不存在，此时只是"求和命令"功能不可用，插件其余部分不受影响。
        qWarning() << Q_FUNC_INFO
                   << "找不到 ISandboxCommandHandler 扩展点，"
                      "本插件可能没有运行在 sandbox_runtime 子进程内";
    }

    if (auto point = extensionSystem->extensionPoint<bakuon::sandbox::IGuiSurfaceHandler>()) {
        m_surfaceHandler = std::make_shared<ClickCounterSurfaceHandler>(this);
        point->registerExtension(m_surfaceHandler, 0);
    } else {
        qWarning() << Q_FUNC_INFO
                   << "找不到 IGuiSurfaceHandler 扩展点，"
                      "本插件可能没有运行在 sandbox_runtime 子进程内，跳过 GUI 表面注册";
    }

    return true;
}

void SandboxedExamplePlugin::extensionsInitialized()
{
    qInfo() << Q_FUNC_INFO;
}

void SandboxedExamplePlugin::shutdown()
{
    m_handler.reset();
    m_surfaceHandler.reset(); // ClickCounterWidget 目前没有父对象，这里让它一并释放
}

} // namespace bakuon::plugins::sandboxed_example
