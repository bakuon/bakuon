#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "core/b_entity.h"
#include "core/b_flags.h"

namespace bakuon::core::command {

// ============================================================================
// 纯数据组件 —— 不认识 Qt，不认识 QAction。
//
// "命令" 在这里只是一个挂了几个组件的 Entity：谁的镜像策略是什么、当前权威
// 上下文是谁，全部是可查询的数据。GUI 层（bakuon::gui）订阅
// Container::onConstruct<Command>()/onUpdate<ActiveContext>() 之类的钩子，
// 在回调里给同一个 Entity 挂一个只有 gui 层认识的 Action 组件
// （struct Action { QAction *proxyAction = nullptr; }），从而把"数据"和
// "Qt 展示"缝合起来——core 全程不知道 Action 组件的存在，也不需要知道。
// ============================================================================

/// 命令身份：稳定字符串 id（对应旧 gui::CommandId 的字符串值）。
struct Command
{
    std::string id;
};

/// 默认展示文案；无权威上下文时代理应回落到这个值（对应旧 Command::m_defaultText）。
struct CommandName
{
    std::string name;
};

/**
 * @brief 声明式描述"代理应如何随权威 realAction 变化"，与旧
 * gui::Command::Attribute 逐位对应，语义未变——只是从 QFlags 换成了
 * BAKUON_DECLARE_FLAGS 生成的 core::Flags<CommandFlag>。
 */
enum class CommandFlag : std::uint8_t {
    None          = 0,
    UpdateText    = 1u << 0, ///< 镜像 realAction 的 text()
    UpdateIcon    = 1u << 1, ///< 镜像 realAction 的 icon()
    UpdateToolTip = 1u << 2, ///< 镜像 realAction 的 toolTip()
    UpdateChecked = 1u << 3, ///< 镜像 realAction 的 checkable/checked 联动状态
    UpdateEnabled = 1u << 4, ///< 镜像 realAction 的 enabled；无权威源时也用它控制是否禁用代理
    HideWhenIdle  = 1u << 5, ///< 无权威源时隐藏代理，而非仅禁用
};
BAKUON_DECLARE_FLAGS(CommandFlags, CommandFlag)

struct CommandAttributes
{
    CommandFlags flags{CommandFlag::UpdateText | CommandFlag::UpdateIcon
                       | CommandFlag::UpdateToolTip | CommandFlag::UpdateChecked
                       | CommandFlag::UpdateEnabled};
};

/// 快捷键，用 QKeySequence::PortableText 形式的字符串表达，避免 core 直接依赖 QKeySequence。
struct CommandShortcuts
{
    std::vector<std::string> portable;
};

/**
 * @brief 该命令当前的权威上下文（仲裁结果的落点）。
 * @note nullentity 表示当前没有任何激活上下文为它登记了动作——对应旧
 *       gui::Command 里 m_realAction == nullptr 的状态。GUI 层订阅这个组件的
 *       onUpdate 信号，据此决定要不要把代理 QAction 的 realAction 换掉。
 */
struct ActiveContext
{
    Entity context{nullentity};
};

/**
 * @brief Command 的 id <-> Entity 双向索引。
 *
 * 与 Identifier（b_identifier.h）对 StableId 做的事情完全同构：靠
 * on_construct<Command>/on_destroy<Command> 保持索引与 Registry 内容自动同步，
 * 不需要、也不应该在调用方那一侧再手写一份平行的 unordered_map<string, T*>
 * （那正是 gui::CommandManager::m_commands 目前在做、之后可以删掉的事）。
 *
 * 不可拷贝/不可移动：on_construct/on_destroy 的 sink 绑定捕获了 `this`，
 * 理由与 Identifier 完全一致。
 */
class CommandRegistry
{
public:
    explicit CommandRegistry(Registry& registry);
    ~CommandRegistry();

    CommandRegistry(const CommandRegistry&)            = delete;
    CommandRegistry& operator=(const CommandRegistry&) = delete;
    CommandRegistry(CommandRegistry&&)                 = delete;
    CommandRegistry& operator=(CommandRegistry&&)      = delete;

    /**
     * @brief 注册一个新命令；若 id 已存在则直接返回已有 Entity（幂等），
     *        不会用新的 text 覆盖旧配置——与旧 gui::CommandManager::registerCommand()
     *        的既有语义保持一致。
     */
    Entity registerCommand(std::string_view id, std::string name);

    /// 注销；对未知 id 是安全的空操作。
    void unregisterCommand(std::string_view id);

    [[nodiscard]] Entity find(std::string_view id) const noexcept;
    [[nodiscard]] bool contains(std::string_view id) const noexcept;
    [[nodiscard]] std::vector<Entity> all() const;
    [[nodiscard]] std::size_t size() const noexcept { return m_idToEntity.size(); }

private:
    void constructed(Registry& registry, Entity entity);
    void destroyed(Registry& registry, Entity entity);

private:
    Registry& m_registry;
    std::unordered_map<std::string, Entity> m_idToEntity;
    ScopedConnection m_constructConn;
    ScopedConnection m_destroyConn;
};

} // namespace bakuon::core::command
