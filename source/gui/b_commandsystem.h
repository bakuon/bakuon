#pragma once

#include <memory>
#include <string>
#include <vector>

#include <QtCore/QMetaObject>
#include <QtCore/QObject>
#include <QtCore/QPointer>
#include <QtGui/QAction>
#include <QtGui/QIcon>
#include <QtGui/QKeySequence>

#include <bakuon/core/Connection.h>
#include <bakuon/core/Entity.h>
#include <bakuon/core/Tagger.h>
#include <bakuon/gui/ICommandSystem.h>

#include "core/b_ref.h"
#include "gui/b_contextsystem.h"
#include "gui/b_gui_export.h"

namespace bakuon::gui::command {

// ============================================================================
// 命令 / 绑定实体的组件词汇表。
// 这里允许出现 Qt 值类型（QString/QIcon/QKeySequence）和 QPointer<QAction>：本头文件属于 gui
// 层，不进入 core；而"QAction 裸指针放进组件"仍然被禁止——一律 QPointer / unique_ptr。
//
//  命令实体： CommandKey + CommandProxy + CommandPresentation + CommandShortcuts +
//             CommandPolicy + CommandFlags + CommandBindings + CommandAuthority
//  绑定实体： ActionBinding + BoundAction                （挂在命令和上下文两侧的有序列表里）
//  上下文侧： ContextBindings                            （由本系统在 bind 时附加到上下文实体上）
// ============================================================================

struct CommandKey
{
    std::string value;
};

/// 代理 QAction 的所有者：命令实体销毁时代理一并销毁（菜单/工具栏里的引用自动消失）。
struct CommandProxy
{
    std::unique_ptr<QAction> action;
};

struct CommandPresentation
{
    QString text;
    QIcon icon;
    QString toolTip;
    bool checkable{false};
};

struct CommandShortcuts
{
    QList<QKeySequence> current;
    QList<QKeySequence> defaults;
};

using CommandPolicy = gui::CommandPolicy;

struct CommandFlags
{
    bool disabled{false};
};

/// 命令名下的全部绑定（顺序无意义）。只由 CommandSystem 维护。
struct CommandBindings
{
    std::vector<core::Entity> ordered;
};
struct ContextBindings
{
    std::vector<core::Entity> ordered;
};

/// 仲裁出来的当前权威。binding 为空表示没有权威源。
struct CommandAuthority
{
    core::Entity binding{core::NullEntity};
    QPointer<QAction> real;
    QMetaObject::Connection changed;   ///< real->changed -> 重新同步代理
    QMetaObject::Connection triggered; ///< 代理->triggered -> real->trigger()
};

struct ActionBinding
{
    core::Entity command{core::NullEntity};
    core::Entity context{core::NullEntity};
};

struct BoundAction
{
    QPointer<QAction> action;
    QMetaObject::Connection destroyed; ///< action 销毁 -> 移除本绑定
};

// ---------------------------------------------------------------------------
// 句柄种类描述 + 带领域方法的内部引用（见 core/b_ref.h）。
// 对外仍然是 OpaqueHandle；这些 Ref 只在本系统内部、栈上使用。
// ---------------------------------------------------------------------------
struct CommandTraits
{
    using tag      = CommandHandleTag;
    using required = entt::type_list<CommandProxy>;
};

template<typename Reg>
class BasicCommandRef : public core::RefBase<CommandTraits, Reg>
{
    using Base = core::RefBase<CommandTraits, Reg>;

public:
    using Base::Base;
    BasicCommandRef() = default;

    [[nodiscard]] QAction *proxy() const { return this->template get<CommandProxy>().action.get(); }
    [[nodiscard]] decltype(auto) presentation() const
    {
        return this->template get<CommandPresentation>();
    }
    [[nodiscard]] decltype(auto) shortcuts() const
    {
        return this->template get<CommandShortcuts>();
    }
    [[nodiscard]] decltype(auto) policy() const { return this->template get<CommandPolicy>(); }
    [[nodiscard]] decltype(auto) flags() const { return this->template get<CommandFlags>(); }
    [[nodiscard]] decltype(auto) authority() const
    {
        return this->template get<CommandAuthority>();
    }
    [[nodiscard]] decltype(auto) bindings() const
    {
        return (this->template get<CommandBindings>().ordered);
    }
    /// 当前权威 QAction（没有则为空）。
    [[nodiscard]] QAction *real() const { return authority().real.data(); }
};

struct BindingTraits
{
    using tag      = BindingHandleTag;
    using required = entt::type_list<ActionBinding, BoundAction>;
};

template<typename Reg>
class BasicBindingRef : public core::RefBase<BindingTraits, Reg>
{
    using Base = core::RefBase<BindingTraits, Reg>;

public:
    using Base::Base;
    BasicBindingRef() = default;

    [[nodiscard]] core::Entity command() const
    {
        return this->template get<ActionBinding>().command;
    }
    [[nodiscard]] core::Entity context() const
    {
        return this->template get<ActionBinding>().context;
    }
    [[nodiscard]] QAction *action() const
    {
        return this->template get<BoundAction>().action.data();
    }
};

using CommandRef      = BasicCommandRef<core::Registry>;
using ConstCommandRef = BasicCommandRef<const core::Registry>;
using BindingRef      = BasicBindingRef<core::Registry>;
using ConstBindingRef = BasicBindingRef<const core::Registry>;

} // namespace bakuon::gui::command

namespace bakuon::gui {

/**
 * @brief 命令系统：ECS 里的一个 System——命令/绑定实体的注册、仲裁、代理 QAction 同步。
 *
 * ## 数据流
 *   插件 patch()/bind()/ContextSystem::push()  →  改组件  →  on_update 监听器 / 观察者
 *   →  resolve(命令)（重新选出权威绑定）/ syncProxy(命令)（组件状态 → 代理 QAction）
 * 同一条"组件 → 代理"的同步路径服务于所有改动来源，没有"某些入口忘了刷新"的可能。
 *
 * ## 仲裁规则（与旧版 ContextArbiter 一致）
 * 在命令的全部绑定里，只看其上下文处于激活态、且 QAction 仍然存活的那些，用
 * core::beats() 按 (tier > priority > activationOrder) 选最大者。
 *
 * ## 归属与访问（没有单例）
 * @code
 *   domain.emplaceService<ContextSystem>(domain.registry());
 *   domain.emplaceService<CommandSystem>(domain.registry(), domain.service<ContextSystem>());
 * @endcode
 *
 * ## 已吸取的历史教训
 *  - 把代理 QAction 当成"真实 QAction"绑定会形成 `代理->trigger() -> 代理->trigger()` 的死循环：
 *    bind() 用代理上的动态属性识别并拒绝。
 *  - 可勾选动作只转发 triggered，不转发 toggled，避免 real 被切换两次。
 *  - 同步 checked 时屏蔽信号，避免递归 toggled。
 *
 * ## 约束
 *  - 只在 GUI 线程使用；不可拷贝、不可移动；必须先于 Registry 销毁，且晚于 ContextSystem 创建。
 *  - 不要直接 registry.destroy() 命令/绑定实体：请用 unregisterCommand()/unbind()。
 *  - 快捷键冲突管理（原 ShortcutManager）应作为另一个 System 读写 CommandShortcuts，不在本类内。
 */
class BAKUON_GUI_EXPORT CommandSystem final : public QObject,
                                              public ICommandSystem,
                                              private core::RefResolver
{
public:
    CommandSystem(core::Registry &registry, ContextSystem &contexts, QObject *parent = nullptr);
    ~CommandSystem() override;

    CommandSystem(const CommandSystem &)            = delete;
    CommandSystem &operator=(const CommandSystem &) = delete;
    CommandSystem(CommandSystem &&)                 = delete;
    CommandSystem &operator=(CommandSystem &&)      = delete;

    [[nodiscard]] core::Registry &registry() noexcept { return m_registry; }

    [[nodiscard]] core::Result<CommandHandle> registerCommand(std::string_view id,
                                                              const QString &text) override;
    [[nodiscard]] core::Result<CommandHandle> ensureCommand(std::string_view id,
                                                            const QString &text) override;
    bool unregisterCommand(CommandHandle command) override;
    [[nodiscard]] CommandHandle find(std::string_view id) const override;
    [[nodiscard]] std::vector<CommandHandle> commands() const override;
    [[nodiscard]] QAction *action(CommandHandle command) const override;
    [[nodiscard]] std::optional<CommandState> state(CommandHandle command) const override;
    bool patch(CommandHandle command, const CommandPatch &patch) override;

    [[nodiscard]] core::Result<BindingHandle> bind(CommandHandle command, ContextHandle context,
                                                   QAction *real) override;
    bool unbind(BindingHandle binding) override;
    [[nodiscard]] QAction *authority(CommandHandle command) const override;
    [[nodiscard]] std::vector<ContextHandle> contextsOf(CommandHandle command) const override;

private:
    template<typename Component>
    void watch();

    // 注意：私有成员不能叫 resolve —— 会遮蔽 RefResolver::resolve<Ref>()。
    void arbitrate(command::CommandRef command);
    void setAuthority(command::CommandRef command, core::Entity binding, QAction *action);
    void syncProxy(core::Entity command);
    void removeBinding(command::BindingRef binding);
    [[nodiscard]] QMetaObject::Connection watchAction(core::Entity binding, QAction *action);

    void componentChanged(core::Registry &registry, core::Entity command);
    void bindingDestroyed(core::Registry &registry, core::Entity binding);
    void onContextChanged(ContextHandle context, ContextChange change);

private:
    core::Registry &m_registry;
    ContextSystem &m_contexts;
    core::BasicTagger<command::CommandKey> m_keys;
    int m_syncDepth = 0; ///< patch() 批处理期间 > 0：监听器不立即同步，批末统一同步一次
    std::vector<core::ScopedConnection> m_connections;
    core::Connection m_contextObserver;
};

} // namespace bakuon::gui
