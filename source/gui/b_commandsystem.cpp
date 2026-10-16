#include "gui/b_commandsystem.h"

#include <algorithm>

#include <QtCore/QSignalBlocker>

namespace bakuon::gui {

using namespace command;
using context::ConstContextRef;
using context::ContextRef;
using core::Entity;
using core::handleOf;
using core::NullEntity;

namespace {

/// 代理 QAction 上的动态属性：bind() 据此拒绝"把代理当真实动作"（自反馈死循环）。
constexpr char kProxyProperty[] = "bakuon_command_proxy";

struct DepthGuard
{
    explicit DepthGuard(int &depth) noexcept
        : m_depth(depth)
    {
        ++m_depth;
    }
    ~DepthGuard() { --m_depth; }
    DepthGuard(const DepthGuard &)            = delete;
    DepthGuard &operator=(const DepthGuard &) = delete;
    int &m_depth;
};

QString toQString(std::string_view text)
{
    return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
}

} // namespace

template<typename Component>
void CommandSystem::watch()
{
    m_connections.emplace_back(
        m_registry.on_construct<Component>().template connect<&CommandSystem::componentChanged>(
            *this));
    m_connections.emplace_back(
        m_registry.on_update<Component>().template connect<&CommandSystem::componentChanged>(*this));
}

CommandSystem::CommandSystem(core::Registry &registry, ContextSystem &contexts, QObject *parent)
    : QObject(parent)
    , core::RefResolver(registry)
    , m_registry(registry)
    , m_contexts(contexts)
    , m_keys(registry)
{
    // 命令实体上"会影响代理 QAction 外观"的组件：任何来源的改动都走同一条同步路径。
    watch<CommandPresentation>();
    watch<CommandShortcuts>();
    watch<CommandPolicy>();
    watch<CommandFlags>();
    watch<CommandAuthority>();
    m_connections.emplace_back(
        m_registry.on_destroy<ActionBinding>().connect<&CommandSystem::bindingDestroyed>(*this));

    m_contextObserver = m_contexts.onChanged(
        [this](ContextHandle context, ContextChange change) { onContextChanged(context, change); });
}

CommandSystem::~CommandSystem()
{
    // 代理 QAction 的生命周期属于 Registry（晚于本对象销毁）。先把"代理 ↔ 真实动作"的连接全部断开，
    // 避免 real->changed 的 lambda 在本对象销毁后还捕获着悬空的 this。
    m_registry.view<CommandAuthority>().each([](CommandAuthority &authority) {
        QObject::disconnect(authority.changed);
        QObject::disconnect(authority.triggered);
    });
}

// ---------------------------------------------------------------------------
// 命令注册
// ---------------------------------------------------------------------------

core::Result<CommandHandle> CommandSystem::registerCommand(std::string_view id, const QString &text)
{
    if (id.empty()) {
        return core::Fail<CommandHandle>(core::StatusCode::InvalidArgument,
                                         "command id must not be empty");
    }
    if (m_keys.contains(id)) {
        return core::Fail<CommandHandle>(core::StatusCode::AlreadyExists,
                                         "command already registered: " + std::string(id));
    }

    const Entity entity = m_registry.create();

    auto proxy = std::make_unique<QAction>(text);
    proxy->setProperty(kProxyProperty, true);
    proxy->setObjectName(toQString(id));
    m_registry.emplace<CommandProxy>(entity, CommandProxy{std::move(proxy)});

    // 顺序刻意：CommandAuthority 最后挂载——它的 on_construct 触发第一次完整同步，
    // 此时代理和其它组件都已就位（syncProxy 对缺组件的情况直接跳过）。
    m_registry.emplace<CommandPresentation>(entity, CommandPresentation{text, {}, {}, false});
    m_registry.emplace<CommandShortcuts>(entity);
    m_registry.emplace<CommandPolicy>(entity);
    m_registry.emplace<CommandFlags>(entity);
    m_registry.emplace<CommandBindings>(entity);
    m_registry.emplace<CommandAuthority>(entity);

    if (auto assigned = m_keys.assign(entity, id); assigned.error()) {
        m_registry.destroy(entity);
        return core::Fail<CommandHandle>(assigned.status());
    }
    return core::Ok(handleOf<CommandHandle>(entity));
}

core::Result<CommandHandle> CommandSystem::ensureCommand(std::string_view id, const QString &text)
{
    if (const Entity existing = m_keys.find(id); m_registry.valid(existing)) {
        return core::Ok(handleOf<CommandHandle>(existing));
    }
    return registerCommand(id, text);
}

bool CommandSystem::unregisterCommand(CommandHandle command)
{
    const auto cmd = resolve<CommandRef>(command);
    if (!cmd) {
        return false;
    }
    // 先销毁绑定（bindingDestroyed 会维护两侧列表），再销毁命令本体。
    const std::vector<Entity> bindings = cmd.bindings();
    for (const Entity binding : bindings) {
        if (const auto ref = resolve<BindingRef>(binding)) {
            ref.destroy();
        }
    }
    cmd.destroy(); // Tagger 索引、代理 QAction 随之清理
    return true;
}

CommandHandle CommandSystem::find(std::string_view id) const
{
    return handleOf<CommandHandle>(m_keys.find(id));
}

std::vector<CommandHandle> CommandSystem::commands() const
{
    std::vector<CommandHandle> result;
    for (const Entity entity : m_keys.entities()) {
        result.push_back(handleOf<CommandHandle>(entity));
    }
    return result;
}

QAction *CommandSystem::action(CommandHandle command) const
{
    const auto cmd = resolve<ConstCommandRef>(command);
    return cmd ? cmd.proxy() : nullptr;
}

std::optional<CommandState> CommandSystem::state(CommandHandle command) const
{
    const auto cmd = resolve<ConstCommandRef>(command);
    if (!cmd) {
        return std::nullopt;
    }
    const auto &presentation = cmd.presentation();
    const auto &shortcuts    = cmd.shortcuts();
    const QAction *proxy     = cmd.proxy();

    CommandState result;
    result.id               = std::string(m_keys.tag(cmd.entity()));
    result.text             = presentation.text;
    result.icon             = presentation.icon;
    result.toolTip          = presentation.toolTip;
    result.checkable        = presentation.checkable;
    result.shortcuts        = shortcuts.current;
    result.defaultShortcuts = shortcuts.defaults;
    result.policy           = cmd.policy();
    result.disabled         = cmd.flags().disabled;
    result.hasAuthority     = cmd.real() != nullptr;
    result.enabled          = proxy->isEnabled();
    result.visible          = proxy->isVisible();
    result.checked          = proxy->isChecked();
    return result;
}

bool CommandSystem::patch(CommandHandle command, const CommandPatch &patch)
{
    const auto cmd = resolve<CommandRef>(command);
    if (!cmd) {
        return false;
    }
    {
        // 批处理：一次 patch 内改多个组件，监听器不逐个同步，批末统一同步一次。
        DepthGuard batch(m_syncDepth);
        if (patch.text || patch.icon || patch.toolTip || patch.checkable) {
            cmd.patch<CommandPresentation>([&](CommandPresentation &p) {
                if (patch.text) {
                    p.text = *patch.text;
                }
                if (patch.icon) {
                    p.icon = *patch.icon;
                }
                if (patch.toolTip) {
                    p.toolTip = *patch.toolTip;
                }
                if (patch.checkable) {
                    p.checkable = *patch.checkable;
                }
            });
        }
        if (patch.shortcuts || patch.defaultShortcuts) {
            cmd.patch<CommandShortcuts>([&](CommandShortcuts &s) {
                if (patch.shortcuts) {
                    s.current = *patch.shortcuts;
                }
                if (patch.defaultShortcuts) {
                    s.defaults = *patch.defaultShortcuts;
                }
            });
        }
        if (patch.policy) {
            cmd.patch<CommandPolicy>([&](CommandPolicy &p) { p = *patch.policy; });
        }
        if (patch.disabled) {
            cmd.patch<CommandFlags>([&](CommandFlags &f) { f.disabled = *patch.disabled; });
        }
    }
    syncProxy(cmd.entity());
    return true;
}

// ---------------------------------------------------------------------------
// 绑定
// ---------------------------------------------------------------------------

core::Result<BindingHandle> CommandSystem::bind(CommandHandle command, ContextHandle context,
                                                QAction *real)
{
    const auto cmd = resolve<CommandRef>(command);
    if (!cmd) {
        return core::Fail<BindingHandle>(core::StatusCode::NotFound, "invalid command");
    }
    const auto ctx = resolve<ContextRef>(context);
    if (!ctx) {
        return core::Fail<BindingHandle>(core::StatusCode::NotFound, "invalid context");
    }
    if (real == nullptr) {
        return core::Fail<BindingHandle>(core::StatusCode::InvalidArgument,
                                         "real action must not be null");
    }
    if (real->property(kProxyProperty).toBool()) {
        return core::Fail<BindingHandle>(core::StatusCode::InvalidArgument,
                                         "a command proxy action cannot be bound as a real action "
                                         "(self-feedback loop)");
    }

    // 同一 (命令, 上下文) 只允许一个绑定：已存在则替换动作。
    Entity binding = NullEntity;
    for (const Entity candidate : cmd.bindings()) {
        const auto existing = resolve<ConstBindingRef>(candidate);
        if (existing && existing.context() == ctx.entity()) {
            binding = candidate;
            break;
        }
    }

    if (const auto existing = resolve<BindingRef>(binding)) {
        if (existing.action() != real) {
            const auto connection = watchAction(binding, real);
            existing.patch<BoundAction>([&](BoundAction &bound) {
                QObject::disconnect(bound.destroyed);
                bound.action    = real;
                bound.destroyed = connection;
            });
        }
    } else {
        binding = m_registry.create();
        m_registry.emplace<ActionBinding>(binding, ActionBinding{cmd.entity(), ctx.entity()});
        m_registry.emplace<BoundAction>(binding, BoundAction{real, watchAction(binding, real)});
        cmd.bindings().push_back(binding);
        ctx.getOrEmplace<ContextBindings>().ordered.push_back(binding);
    }

    arbitrate(cmd);
    return core::Ok(handleOf<BindingHandle>(binding));
}

bool CommandSystem::unbind(BindingHandle binding)
{
    const auto ref = resolve<BindingRef>(binding);
    if (!ref) {
        return false;
    }
    removeBinding(ref);
    return true;
}

QAction *CommandSystem::authority(CommandHandle command) const
{
    const auto cmd = resolve<ConstCommandRef>(command);
    return cmd ? cmd.real() : nullptr;
}

std::vector<ContextHandle> CommandSystem::contextsOf(CommandHandle command) const
{
    std::vector<ContextHandle> result;
    if (const auto cmd = resolve<ConstCommandRef>(command)) {
        for (const Entity binding : cmd.bindings()) {
            if (const auto link = resolve<ConstBindingRef>(binding)) {
                result.push_back(handleOf<ContextHandle>(link.context()));
            }
        }
    }
    return result;
}

// ---------------------------------------------------------------------------
// 仲裁 + 同步
// ---------------------------------------------------------------------------

void CommandSystem::arbitrate(CommandRef command)
{
    Entity bestBinding  = NullEntity;
    QAction *bestAction = nullptr;
    core::ContextArbitrationKey bestKey{};
    bool hasBest = false;

    for (const Entity binding : command.bindings()) {
        const auto link = resolve<ConstBindingRef>(binding);
        if (!link || link.action() == nullptr) {
            continue; // 动作已销毁（destroyed 处理器稍后会清理绑定）
        }
        const auto ctx = resolve<ConstContextRef>(link.context());
        if (!ctx || !ctx.active()) {
            continue; // 上下文已销毁 / 未激活
        }
        const auto &arbitration = ctx.arbitration();
        const core::ContextArbitrationKey key{arbitration.tier,
                                              arbitration.priority,
                                              arbitration.order};
        if (core::beats(key, bestKey, hasBest)) {
            bestBinding = binding;
            bestAction  = link.action();
            bestKey     = key;
            hasBest     = true;
        }
    }
    setAuthority(command, bestBinding, bestAction);
}

void CommandSystem::setAuthority(CommandRef command, Entity binding, QAction *action)
{
    const auto &current = command.authority();
    if (current.binding == binding && current.real.data() == action) {
        return; // 权威没变：不做任何事（既不重连也不重同步）
    }

    QAction *proxy      = command.proxy();
    const Entity entity = command.entity();
    QMetaObject::Connection changed;
    QMetaObject::Connection triggered;
    if (action != nullptr) {
        // 以代理为 context：代理销毁时两条连接自动断开。
        changed = QObject::connect(action, &QAction::changed, proxy, [this, entity] {
            syncProxy(entity);
        });
        // 只转发 triggered：可勾选动作点击时先 toggle 再 emit triggered，
        // 若再连 toggled -> real->toggle()，real 会被切换两次。
        const QPointer<QAction> real(action);
        triggered = QObject::connect(proxy, &QAction::triggered, action, [real] {
            if (real) {
                real->trigger();
            }
        });
    }

    // patch 触发 on_update<CommandAuthority> → componentChanged → syncProxy。
    command.patch<CommandAuthority>([&](CommandAuthority &authority) {
        QObject::disconnect(authority.changed);
        QObject::disconnect(authority.triggered);
        authority.binding   = binding;
        authority.real      = action;
        authority.changed   = changed;
        authority.triggered = triggered;
    });
}

void CommandSystem::syncProxy(Entity command)
{
    if (m_syncDepth > 0) {
        return;
    }
    const auto cmd = resolve<ConstCommandRef>(command);
    if (!cmd
        || !cmd.has<CommandPresentation,
                    CommandShortcuts,
                    CommandPolicy,
                    CommandFlags,
                    CommandAuthority>()) {
        return; // 命令还在构造中（组件尚未挂全）
    }

    QAction *proxy = cmd.proxy();
    QAction *real  = cmd.real();
    if (proxy == nullptr) {
        return;
    }
    const auto &presentation = cmd.presentation();
    const auto &policy       = cmd.policy();
    const bool hasReal       = real != nullptr;
    const auto useReal       = [&](bool mirror) { return hasReal && mirror; };

    // 规则：被镜像的属性取权威动作的值；其余一律显示命令自己的默认值。
    const QString text = useReal(policy.mirrorText) ? real->text() : presentation.text;
    if (proxy->text() != text) {
        proxy->setText(text);
    }
    const QIcon icon = useReal(policy.mirrorIcon) && !real->icon().isNull() ? real->icon()
                                                                            : presentation.icon;
    if (proxy->icon().cacheKey() != icon.cacheKey()) {
        proxy->setIcon(icon);
    }
    const QString toolTip = useReal(policy.mirrorToolTip) ? real->toolTip() : presentation.toolTip;
    if (proxy->toolTip() != toolTip) {
        proxy->setToolTip(toolTip);
    }

    const bool checkable = useReal(policy.mirrorChecked) ? real->isCheckable()
                                                         : presentation.checkable;
    proxy->setCheckable(checkable);
    if (checkable) {
        const bool checked = useReal(policy.mirrorChecked) ? real->isChecked()
                                                           : (hasReal ? proxy->isChecked() : false);
        const QSignalBlocker blocker(proxy); // 避免递归 toggled
        proxy->setChecked(checked);
    }

    const auto &shortcuts = cmd.shortcuts();
    if (shortcuts.current != proxy->shortcuts()) {
        proxy->setShortcuts(shortcuts.current); // 快捷键只挂在代理上，从不镜像
    }

    bool enabled = hasReal ? (policy.mirrorEnabled ? real->isEnabled() : true) : false;
    if (cmd.flags().disabled) {
        enabled = false; // 命令级开关压过一切
    }
    proxy->setEnabled(enabled);
    proxy->setVisible(hasReal ? real->isVisible() : !policy.hideWhenIdle);
}

void CommandSystem::removeBinding(BindingRef binding)
{
    const Entity command = binding.command();
    binding.destroy(); // bindingDestroyed 维护两侧列表并断开 destroyed 连接
    if (const auto cmd = resolve<CommandRef>(command)) {
        arbitrate(cmd);
    }
}

QMetaObject::Connection CommandSystem::watchAction(Entity binding, QAction *action)
{
    // 真实动作被销毁（插件卸载/窗口关闭）时自动移除绑定并重新仲裁。
    return QObject::connect(action, &QObject::destroyed, this, [this, binding] {
        if (const auto ref = resolve<BindingRef>(binding)) {
            removeBinding(ref);
        }
    });
}

// ---------------------------------------------------------------------------
// 监听器 / 观察者
// ---------------------------------------------------------------------------

void CommandSystem::componentChanged(core::Registry & /*registry*/, Entity command)
{
    syncProxy(command);
}

void CommandSystem::bindingDestroyed(core::Registry &registry, Entity binding)
{
    // 监听器里用 wrap（不做种类校验）：on_destroy 触发时组件仍在，只读已知存在的两个。
    const auto link = core::wrap<ConstBindingRef>(registry, binding);
    if (const auto cmd = core::resolve<CommandRef>(registry, link.command())) {
        std::erase(cmd.bindings(), binding);
    }
    if (const auto ctx = core::resolve<ContextRef>(registry, link.context())) {
        if (auto *list = ctx.tryGet<ContextBindings>()) {
            std::erase(list->ordered, binding);
        }
    }
    if (const auto *bound = link.tryGet<BoundAction>()) {
        QObject::disconnect(bound->destroyed);
    }
}

void CommandSystem::onContextChanged(ContextHandle context, ContextChange change)
{
    if (change != ContextChange::Activation && change != ContextChange::Priority
        && change != ContextChange::Undeclaring) {
        return;
    }
    const auto ctx = resolve<ConstContextRef>(context);
    if (!ctx) {
        return;
    }
    const auto *list = ctx.tryGet<ContextBindings>();
    if (list == nullptr) {
        return;
    }

    // 先快照再处理：销毁/重新仲裁过程会修改这份列表。
    const std::vector<Entity> bindings = list->ordered;
    std::vector<Entity> affected;
    affected.reserve(bindings.size());
    for (const Entity binding : bindings) {
        if (const auto link = resolve<ConstBindingRef>(binding)) {
            affected.push_back(link.command());
        }
    }

    if (change == ContextChange::Undeclaring) {
        for (const Entity binding : bindings) {
            if (const auto ref = resolve<BindingRef>(binding)) {
                ref.destroy();
            }
        }
    }
    for (const Entity command : affected) {
        if (const auto cmd = resolve<CommandRef>(command)) {
            arbitrate(cmd);
        }
    }
}

} // namespace bakuon::gui
