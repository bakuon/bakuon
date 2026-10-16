#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <QtCore/QList>
#include <QtCore/QString>
#include <QtGui/QIcon>
#include <QtGui/QKeySequence>

#include <bakuon/core/Handle.h>
#include <bakuon/core/Result.h>
#include <bakuon/gui/IContextSystem.h>

class QAction;

// ============================================================================
// 命令系统的"插件侧"稳定门面：纯接口，Qt 值类型可见，entt 不可见。
//
// 命令 = registry 里的一个实体。它的组件状态：
//   CommandPresentation  默认展示（无权威源时代理 QAction 显示什么）
//   CommandShortcuts     快捷键（当前 / 默认）
//   CommandPolicy        代理如何镜像权威 QAction（逐项开关）
//   CommandFlags         命令级开关（如整体禁用）
//   CommandAuthority     当前权威绑定（系统仲裁出来的，只读）
// 绑定（Binding）也是实体：(命令, 上下文) → 真实 QAction。
//
// 代理 QAction（menus/toolbars 里放的那个）由系统持有，action() 取到的指针在命令注销前一直有效。
// 没有全局单例：ICommandSystem 由 Workspace/Application 的某个领域提供。
// ============================================================================

namespace bakuon::gui {

struct CommandHandleTag;
struct BindingHandleTag;
using CommandHandle = core::BasicHandle<CommandHandleTag>;
using BindingHandle = core::BasicHandle<BindingHandleTag>;

/// 代理 QAction 如何随权威 QAction 变化。未镜像的属性一律显示命令自己的默认值。
struct CommandPolicy
{
    bool mirrorText{true};
    bool mirrorIcon{true};
    bool mirrorToolTip{true};
    bool mirrorChecked{true};
    bool mirrorEnabled{true};
    bool hideWhenIdle{false}; ///< 没有权威源时隐藏代理，而不是仅禁用

    friend bool operator==(const CommandPolicy &, const CommandPolicy &) = default;
};

/// 允许插件修改的命令组件字段（patch 语义：只有设置了的字段才会被修改；
/// 一次 patch 内的多个字段只触发一次代理同步）。
struct CommandPatch
{
    std::optional<QString> text; ///< 默认文案（无权威源时显示）
    std::optional<QIcon> icon;
    std::optional<QString> toolTip;
    std::optional<bool> checkable;
    std::optional<QList<QKeySequence>> shortcuts;        ///< 当前快捷键
    std::optional<QList<QKeySequence>> defaultShortcuts; ///< 默认快捷键
    std::optional<CommandPolicy> policy;
    std::optional<bool> disabled; ///< true：无论权威源怎样，代理都被强制禁用
};

/// 一个命令当前状态的值拷贝快照。
struct CommandState
{
    std::string id;
    QString text;
    QIcon icon;
    QString toolTip;
    bool checkable{false};
    QList<QKeySequence> shortcuts;
    QList<QKeySequence> defaultShortcuts;
    CommandPolicy policy;
    bool disabled{false};

    bool hasAuthority{false}; ///< 当前是否有权威 QAction
    bool enabled{false};      ///< 代理 QAction 此刻的实际状态
    bool visible{true};
    bool checked{false};
};

class BAKUON_GUI_EXPORT ICommandSystem
{
public:
    virtual ~ICommandSystem() = default;

    // ---- 命令注册 ----

    /// id 为空 → InvalidArgument；已存在 → AlreadyExists。
    [[nodiscard]] virtual core::Result<CommandHandle> registerCommand(std::string_view id,
                                                                      const QString &text) = 0;
    /// 取或建：多个插件共用同一条标准命令（如 edit.delete）时使用。
    [[nodiscard]] virtual core::Result<CommandHandle> ensureCommand(std::string_view id,
                                                                    const QString &text)   = 0;
    /// 注销命令：先清理它的全部绑定，再销毁实体和代理 QAction。
    virtual bool unregisterCommand(CommandHandle command)                                  = 0;

    [[nodiscard]] virtual CommandHandle find(std::string_view id) const                  = 0;
    [[nodiscard]] virtual std::vector<CommandHandle> commands() const                    = 0;
    /// 代理 QAction：放进 QMenu/QToolBar/QWidget。命令注销后指针失效。
    [[nodiscard]] virtual QAction *action(CommandHandle command) const                   = 0;
    [[nodiscard]] virtual std::optional<CommandState> state(CommandHandle command) const = 0;

    // ---- 可修改的组件状态 ----

    virtual bool patch(CommandHandle command, const CommandPatch &patch) = 0;

    bool setText(CommandHandle command, QString text)
    {
        CommandPatch p;
        p.text = std::move(text);
        return patch(command, p);
    }
    bool setDisabled(CommandHandle command, bool disabled)
    {
        CommandPatch p;
        p.disabled = disabled;
        return patch(command, p);
    }
    bool setShortcuts(CommandHandle command, QList<QKeySequence> shortcuts)
    {
        CommandPatch p;
        p.shortcuts = std::move(shortcuts);
        return patch(command, p);
    }

    // ---- 绑定：(命令, 上下文) → 真实 QAction ----

    /// 把 real 注册为"命令在该上下文激活时的权威动作"。同一 (命令, 上下文) 重复绑定会替换动作。
    /// real 为空 / 是某个命令的代理 QAction（会造成自反馈死循环）→ InvalidArgument。
    /// real 被销毁时绑定自动移除，命令自动重新仲裁。
    [[nodiscard]] virtual core::Result<BindingHandle> bind(CommandHandle command,
                                                           ContextHandle context,
                                                           QAction *real) = 0;
    virtual bool unbind(BindingHandle binding)                            = 0;

    /// 当前权威 QAction（没有则为空）；仅供调试/展示，不参与仲裁。
    [[nodiscard]] virtual QAction *authority(CommandHandle command) const                    = 0;
    [[nodiscard]] virtual std::vector<ContextHandle> contextsOf(CommandHandle command) const = 0;
};

/// 绑定的 RAII 守卫：析构时自动 unbind()，插件 shutdown() 时不会遗留悬空绑定。
class ScopedBinding
{
public:
    ScopedBinding() = default;
    ScopedBinding(ICommandSystem &system, BindingHandle binding) noexcept
        : m_system(&system)
        , m_binding(binding)
    {
    }
    ~ScopedBinding() { reset(); }

    ScopedBinding(const ScopedBinding &)            = delete;
    ScopedBinding &operator=(const ScopedBinding &) = delete;
    ScopedBinding(ScopedBinding &&other) noexcept
        : m_system(std::exchange(other.m_system, nullptr))
        , m_binding(std::exchange(other.m_binding, {}))
    {
    }
    ScopedBinding &operator=(ScopedBinding &&other) noexcept
    {
        if (this != &other) {
            reset();
            m_system  = std::exchange(other.m_system, nullptr);
            m_binding = std::exchange(other.m_binding, {});
        }
        return *this;
    }

    void reset() noexcept
    {
        if (m_system && m_binding) {
            m_system->unbind(m_binding);
        }
        m_system  = nullptr;
        m_binding = {};
    }
    [[nodiscard]] BindingHandle handle() const noexcept { return m_binding; }

private:
    ICommandSystem *m_system{nullptr};
    BindingHandle m_binding{};
};

} // namespace bakuon::gui
