#pragma once

#include <unordered_set>

#include <QAction>
#include <QMainWindow>
#include <QMenu>
#include <QMenuBar>
#include <QString>
#include <QToolBar>

#include "gui/b_command.h"
#include "gui/b_commandlayout.h"

namespace bakuon::gui {

/**
 * CommandManager (命令目录/行为)
 *         │  查询
 *         ▼
 * CommandLayout (纯数据：TreeNode<CommandLayoutData> + 存盘，不依赖 QAbstractItemModel)
 *         │                              │
 *         │ 直接使用                      │ 包一层适配
 *         ▼                              ▼
 * CommandManager::render()       CommandModel (QAbstractItemModel，供 QTreeView 编辑)
 *         │                              │
 *         ▼                              ▼
 *      QMenuBar                     自定义菜单对话框
 */

class CommandManager : public QObject
{
    Q_OBJECT
public:
    explicit CommandManager(QObject* parent = nullptr);
    ~CommandManager() override = default;

    /// Command 管理

    // 注册一个新命令；若 id 已存在则直接返回已有实例（幂等），不会用新的 text 覆盖旧配置
    Command& registerCommand(const CommandId& id, const QString& text);
    void unregisterCommand(const CommandId& id);
    Command* command(const CommandId& id) const;

    // 枚举全部已注册命令，例如用于生成"自定义快捷键"设置面板
    std::vector<Command*> allCommands() const;
    std::vector<CommandId> allCommandIds() const;

    /// Command Layout Render 布局渲染

    // 默认菜单布局
    CommandLayout* menubarLayout() const;
    // 默认工具栏布局
    CommandLayout* toolbarLayout() const;

    /**
     * 把 CommandLayout 的树状布局“渲染”成一个真实的 QMenuBar。
     * 
     * 采用“整体重建”策略：每次 render() 都先清空、再按当前内容从头搭建一遍，
     * 而不是尝试增量 diff —— 菜单项数量通常在几十到一百量级，全量重建的开销可以忽略。
     */

    // 渲染菜单栏
    void renderMenuBar(CommandLayout* layout, QMenuBar* menubar) const;
    void renderMenuBar(CommandLayout::Item* parent, QMenuBar* menubar) const;
    void renderMenu(CommandLayout::Item* parent, QMenu* menu) const;
    // 渲染工具栏
    void renderToolBar(CommandLayout* layout, QMainWindow* window) const;
    void renderToolBar(CommandLayout::Item* parent, QToolBar* toolbar) const;

    /// Context 管理

    /** ContextManager：维护当前“激活上下文集合”（而非简单的单一上下文/栈）。
     *
     * 真实 GUI 场景中往往需要多个上下文同时生效，例如：
     *   "global"                    —— 程序启动后常驻激活
     *   "editor.image.focused"      —— 图像编辑画布获得焦点时激活
     *   "editor.image.objectSelected" —— 在画布上选中了某个对象时额外激活
     * 三者可以同时成立，因此用“按来源引用计数的集合”而不是“栈顶唯一”来建模，
     * 这样多个来源（例如焦点系统 + 选择系统）各自独立地 push/pop 同一个上下文时不会互相干扰。
     */

    // ActivationTier：声明一次 push 是否应该参与"最近激活"的仲裁竞争。
    //
    // 背景：Command::findAuthoritativeIndex() 在多个已注册上下文同时激活、且
    // priority 相同的情况下，用 activationOrder()（谁最近被激活）做最终裁决。
    // 这套"最近 push 的赢"的规则，隐含假设了"所有会 push 上下文的行为都代表
    // 用户此刻真实的注意力转移"——但异步后台任务（比如某个编辑器实例在后台跑一个
    // 3 秒的滤镜、期间 push 了一个 Temporary 上下文）恰恰不满足这个假设：它确实是
    // "最新 push 的"，但完全不代表用户此刻真的在关注它，如果用户同时已经把注意力
    // 切换到了另一个实例并开始交互，这类后台 push 反而会在仲裁时错误地压过用户
    // 真正当前关注的那个实例，导致快捷键/按钮触发的动作被张冠李戴地路由过去。
    //
    // 用 Background 层级 push 的上下文，仍然正常进入激活集合（isActiveContext() 为
    // true，绑定在它上面的 realAction 依然可以被启用/触发），但不会推进全局的
    // activationOrder 时钟——因此在优先级相同的情况下，它永远不可能仅凭"最近
    // 激活"就压过任何一个 Interactive 层级的上下文，哪怕它在挂钟时间上确实更晚。
    // 这与"给后台任务的 Command 绑定设置更低的 priority"是两道独立的防线，
    // 建议同时使用：priority 防的是"优先级配置正确"的情况，ActivationTier
    // 防的是"忘记配置 priority、或优先级恰好相同"的情况。
    enum class ActivationTier : quint8 {
        Foreground, // 交互式/前台：代表真实的用户交互 Interactive（获得焦点、点击选中……），参与仲裁竞争（默认）
        Background, // 后台/异步任务：进入激活集合，但不参与"最近激活"的仲裁竞争
    };

    // source 标识“是谁请求激活了这个上下文”，通常传入发起请求的 widget/editor 的 this 指针。
    // 同一个 (context, source) 组合内部计数，需要对应次数的 popContext 才会真正失活，
    // 因此允许同一来源出于不同原因多次 push 同一个上下文而不必自行去重。
    // tier 只在"这次 push 恰好是该上下文的首次激活"时决定要不要推进时钟，
    // 但对 Interactive 层级的 push，即使上下文已经因为别的来源处于激活状态，
    // 每一次新的 Interactive push 仍然会刷新它的 activationOrder（见 .cpp 实现注释）。
    void pushContext(const ContextId& context, const void* source,
                     ActivationTier tier = ActivationTier::Foreground);
    void popContext(const ContextId& context, const void* source);

    // 某个 source 生命周期结束时（如控件析构），一次性释放其持有的全部上下文引用
    void releaseContext(const void* source);

    std::unordered_set<ContextId> activeContexts();
    bool isActiveContext(const ContextId& context) const noexcept;

    // 返回某个上下文"最近一次被 Interactive 层级 push"时记录下的全局递增序号；
    // 从未被 Interactive 层级激活过，或当前未激活，则返回 0。
    // 供 Command 在多个已注册上下文同时激活时做仲裁：同优先级下，序号越大表示
    // 越晚被真实的用户交互触达，语义上等价于"最近一次获得焦点/被选中的上下文优先"。
    uint64_t activationOrder(const ContextId& context) const noexcept;

signals:
    // 激活集合发生变化（有上下文从"无引用"变为"有引用"，或反之）时发出
    // 借此统一刷新所有已绑定 QAction 的 enabled 状态。
    void contextChanged();

private:
    struct RefKey
    {
        ContextId context;
        const void* source                   = nullptr;
        bool operator==(const RefKey&) const = default;
    };

    struct RefKeyHash
    {
        size_t operator()(const RefKey& key) const noexcept
        {
            // 标准 hash_combine 公式，比简单异或有更好的位分布
            // 指针哈希的低位常因内存对齐而恒为 0，直接异或容易在这些位上退化。
            size_t seed    = std::hash<ContextId>{}(key.context);
            size_t srcHash = std::hash<const void*>{}(key.source);
            seed ^= srcHash + 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2);
            return seed;
        }
    };

    std::unordered_map<CommandId, std::unique_ptr<Command>> m_commands;

    // 菜单栏布局
    std::unique_ptr<CommandLayout> m_menubarLayout;
    std::unique_ptr<CommandLayout> m_toolbarLayout;

    // 每个 (context, source) 组合各自的引用计数
    std::unordered_map<RefKey, int, RefKeyHash> m_refCounts;
    // 每个 context 当前的总引用数，> 0 即视为激活
    std::unordered_map<ContextId, int> m_contextRefTotals;
    // 每个 context 最近一次由"无引用"变为"有引用"时记录下的全局递增序号
    std::unordered_map<ContextId, uint64_t> m_activationOrder;
    uint64_t m_activationClock = 0;
};

} // namespace bakuon::gui
