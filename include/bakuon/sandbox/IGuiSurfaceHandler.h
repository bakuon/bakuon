#pragma once

#include <bakuon/gui/IExtensionPoint.h>

class QWidget;

// ============================================================================
// bakuon::sandbox 门面头文件（facade）
// 参见 include/bakuon/gui/IPlugin.h 顶部关于门面 / 内部实现分层的说明，
// 本文件遵循同样的约定：只做稳定转发，不包含新的实现代码。
//
// 插件开发者应该写：
//   #include "bakuon/sandbox/IGuiSurfaceHandler.h"
// 而不是直接依赖 source/sandbox/ 下的 b_ 前缀内部头文件。
// ============================================================================

namespace bakuon::sandbox {

/**
 * @brief 沙箱化 GUI 插件的"表面"扩展点 —— 跨进程 GUI 合成的可插拔落点。
 *
 * 和 ISandboxCommandHandler 是同一套设计模式：SandboxRuntime 负责"骨架"
 * （offscreen QApplication、周期性抓帧、写入共享内存、通过 .rep 契约的
 * frameReady 信号通知 Host；以及把 Host 转发过来的输入事件回放到 widget 上），
 * 具体某个插件想展示什么样的自绘界面，通过实现本接口、注册到
 * IExtensionPoint<IGuiSurfaceHandler> 来提供——SandboxRuntime 本身对界面内容
 * 零感知。
 *
 * ## 使用方式（插件侧，在 IPlugin::initialize() 中）
 * @code
 *   bool MyPlugin::initialize(bakuon::gui::PluginContext &ctx)
 *   {
 *       auto point = bakuon::gui::ExtensionSystem::instance()
 *                        .extensionPoint<bakuon::sandbox::IGuiSurfaceHandler>();
 *       if (point) {
 *           point->registerExtension(std::make_shared<MySurfaceHandler>(), 0);
 *       }
 *       return true;
 *   }
 * @endcode
 * @note 该扩展点由 SandboxRuntime 在沙箱进程启动时统一注册，插件只需要
 *       registerExtension()。同一沙箱进程当前只使用第一个注册的处理器
 *       （一个沙箱实例对应一个 Tab、一块画面，多个处理器同时争抢没有意义）。
 *
 * @warning surfaceWidget() 返回的 QWidget 永远不会被 show()——它活在一个
 *          `-platform offscreen` 的 QApplication 里，SandboxRuntime 只会调用
 *          `grab()`/`render()` 把它的像素抓出来，不会创建任何真实窗口。
 *          插件不应该在这个 widget 上假设自己"看得见"（比如依赖窗口激活/
 *          焦点相关的事件），键盘/鼠标事件全部由 SandboxRuntime 合成后
 *          `QCoreApplication::sendEvent()` 直接送达，不经过真实的输入法/
 *          窗口系统。
 */
class IGuiSurfaceHandler
{
public:
    virtual ~IGuiSurfaceHandler() = default;

    /**
     * @brief 返回要被捕获/合成的顶层 widget，SandboxRuntime 只在第一次调用时取用，
     *        之后持有同一个指针直到插件 shutdown()。
     * @note 建议在构造时就把 widget 建好、调用一次 resize() 定好固定尺寸——
     *       当前实现（v1）按固定尺寸抓帧，运行期 resize() 会被忽略，见
     *       b_sandboxruntime.cpp 里对这一点的说明。
     */
    virtual QWidget *surfaceWidget() = 0;
};

} // namespace bakuon::sandbox

BAKUON_DECLARE_EXTENSION_IID(bakuon::sandbox::IGuiSurfaceHandler,
                             "com.bakuon.sandbox.IGuiSurfaceHandler")
