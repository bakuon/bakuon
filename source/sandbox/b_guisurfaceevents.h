#pragma once

namespace bakuon::sandbox {

/**
 * @brief dispatchInputEvent() 契约里 `type` 参数的取值约定。
 *
 * 故意不直接复用 QEvent::Type 的数值（比如 QEvent::MouseButtonPress==2）——
 * 那是 Qt 内部实现细节，不同 Qt 版本之间没有"数值永远不变"的公开承诺；
 * Host 和 Sandbox 两个进程理论上可能被不同版本的 Qt 编译（虽然目前项目里
 * 总是同一份 Qt），把契约里的取值和 Qt 内部枚举脱钩，是更稳妥的做法。
 */
enum class GuiInputEventType : int {
    MouseMove    = 0,
    MousePress   = 1,
    MouseRelease = 2,
    Wheel        = 3,
    KeyPress     = 4,
    KeyRelease   = 5,
};

/// dispatchInputEvent() 里 `button` 参数的取值约定，按位组合（滚轮事件不使用）。
enum class GuiMouseButton : int {
    None   = 0,
    Left   = 1 << 0,
    Right  = 1 << 1,
    Middle = 1 << 2,
};

/// dispatchInputEvent() 里 `modifiers` 参数的取值约定，按位组合。
enum class GuiKeyModifier : int {
    None  = 0,
    Shift = 1 << 0,
    Ctrl  = 1 << 1,
    Alt   = 1 << 2,
};

} // namespace bakuon::sandbox
