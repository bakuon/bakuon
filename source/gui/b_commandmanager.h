#pragma once

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
    explicit CommandManager(IContextArbiter& arbiter, QObject* parent = nullptr);
    ~CommandManager() override = default;

    // 注册一个新命令；若 id 已存在则直接返回已有实例（幂等），不会用新的 text 覆盖旧配置
    Command& registerCommand(const CommandId& id, const QString& text);
    void unregisterCommand(const CommandId& id);
    Command* command(const CommandId& id) const;

    // 枚举全部已注册命令，例如用于生成"自定义快捷键"设置面板
    std::vector<Command*> allCommands() const;
    std::vector<CommandId> allCommandIds() const;

    /**
     * @brief 对全部已注册命令统一触发一次重新仲裁。
     *
     * CommandManager 本身不认识具体的 ContextTracker 类型、也不 connect 它的
     * contextChanged 信号——上下文集合何时变化由持有具体 ContextTracker 的上层
     * （通常是 CommandSystem）感知，感知到之后调用这个方法即可，不需要自己遍历
     * allCommands() 逐个调 Command::resyncAuthoritativeBinding()。
     * 这样 CommandManager 的单元测试可以用一个最小假 IContextArbiter 实现构造，
     * 不需要依赖真正的 ContextTracker/QObject 信号机制。
     */
    void resyncAllCommands();

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

private:
    IContextArbiter& m_arbiter;

    // 默认菜单栏/工具栏布局
    std::unique_ptr<CommandLayout> m_menubarLayout;
    std::unique_ptr<CommandLayout> m_toolbarLayout;

    std::unordered_map<CommandId, std::unique_ptr<Command>> m_commands;
};

} // namespace bakuon::gui
