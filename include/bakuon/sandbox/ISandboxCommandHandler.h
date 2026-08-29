#pragma once

#include <bakuon/gui/IExtensionPoint.h>

#include <QtCore/QByteArray>
#include <QtCore/QString>
#include <QtCore/QVariantMap>

// ============================================================================
// bakuon::sandbox 门面头文件（facade）
// 参见 include/bakuon/gui/IPlugin.h 顶部关于门面 / 内部实现分层的说明，
// 本文件遵循同样的约定：只做稳定转发，不包含新的实现代码。
//
// 插件开发者应该写：
//   #include "bakuon/sandbox/ISandboxCommandHandler.h"
// 而不是直接依赖 source/sandbox/ 下的 b_ 前缀内部头文件。
// ============================================================================

namespace bakuon::sandbox {

/**
 * @brief 单条"重型命令"在沙箱进程内的执行上下文。
 *
 * 由 SandboxRuntime 在收到 Host 的 executeCommand() 调用后构造，生命周期
 * 仅限于本次命令处理期间，Handler 不应该把它保存到 execute() 调用栈之外。
 */
class ISandboxCommandContext
{
public:
    virtual ~ISandboxCommandContext() = default;

    /// 本次命令 Host 一侧下发的额外参数（executeCommand 契约里的 params）。
    [[nodiscard]] virtual QVariantMap params() const = 0;

    /// 读取共享内存里当前的 Payload（通常是 Host 侧写入的输入数据）。
    [[nodiscard]] virtual QByteArray readInput() const = 0;

    /**
     * @brief 将结果原地写回共享内存（覆盖同一块 Payload 区域）。
     * @return true 写入成功；false 容量不足（见 sharedMemoryCapacity()）
     */
    virtual bool writeResult(const QByteArray &result) = 0;

    /// 共享内存段的可用容量（字节），Handler 可据此判断是否需要分块处理。
    [[nodiscard]] virtual qsizetype sharedMemoryCapacity() const = 0;
};

/**
 * @brief 沙箱命令处理器扩展接口 —— "血肉"部分的可插拔扩展点。
 *
 * "骨架"（SandboxRuntime + QtRO 契约 + QSharedMemory 通道，见
 * source/sandbox/ 下的实现）已经解决了"怎么把一次调用、一块内存从 Host
 * 安全地送到 Sandbox 进程"这件事；具体某个插件想在沙箱里做什么样的高吞吐
 * 原地计算（大文本处理/音视频转码/点云计算/图像处理……），通过实现本接口、
 * 注册到 IExtensionPoint<ISandboxCommandHandler> 来提供——SandboxRuntime
 * 本身对具体业务零感知，符合开闭原则，也是"先搭骨架、再填血肉"里"血肉"
 * 部分真正的可扩展落点。
 *
 * ## 使用方式（插件侧，在 IPlugin::initialize() 中）
 * 沙箱是独立的操作系统进程，bakuon::gui::ExtensionSystem::instance() 是
 * 进程内单例，因此沙箱进程天然拥有一份与主程序、与其他沙箱完全隔离的扩展点
 * 注册表——插件在 initialize() 里用和"进程内插件"完全一样的写法即可注册命令
 * 处理器，不需要任何额外的跨进程适配代码：
 * @code
 *   bool MyPlugin::initialize(bakuon::gui::PluginContext &ctx)
 *   {
 *       auto point = bakuon::gui::ExtensionSystem::instance()
 *                        .extensionPoint<bakuon::sandbox::ISandboxCommandHandler>();
 *       if (point) {
 *           point->registerExtension(std::make_shared<MyPointCloudHandler>(), 0);
 *       }
 *       return true;
 *   }
 * @endcode
 * @note 该扩展点由 SandboxRuntime 在沙箱进程启动时统一注册（见
 *       b_sandboxruntime.cpp），插件只需要 registerExtension()，不需要
 *       自己 registerExtensionPoint()。
 */
class ISandboxCommandHandler
{
public:
    virtual ~ISandboxCommandHandler() = default;

    /// 本处理器能处理的命令 id（对应 Host 侧 executeCommand() 传入的 commandId）。
    [[nodiscard]] virtual QString commandId() const = 0;

    /**
     * @brief 执行命令，直接在共享内存上原地读写（高吞吐场景应避免额外拷贝）。
     * @param context 本次调用的上下文（输入/输出/参数访问）
     * @return true 处理成功；false 处理失败（错误信息见 errorMessage()）
     */
    virtual bool execute(ISandboxCommandContext &context) = 0;

    /// execute() 返回 false 时的错误描述；成功时应返回空串。
    [[nodiscard]] virtual QString errorMessage() const { return {}; }
};

} // namespace bakuon::sandbox

BAKUON_DECLARE_EXTENSION_IID(bakuon::sandbox::ISandboxCommandHandler,
                             "com.bakuon.sandbox.ISandboxCommandHandler")
