#pragma once

#include <utility>
#include <QtCore/QDataStream>

#include "gui/b_command.h"
#include "gui/b_commandlayout.h"
#include "gui/b_commandmanager.h"
#include "gui/b_shortcutmanager.h"
#include "gui/detail/b_types.h"

namespace bakuon::gui {

/**
 * @brief 命令系统门面 Command Facade
 * @todo 可能使用命名空间代替静态类
 */
class CommandSystem
{
public:
    /// 命令管理

    static Command& registerCommand(const CommandId& id, const QString& text);
    static void unregisterCommand(const CommandId& id);
    static Command* command(const CommandId& id);
    static std::vector<Command*> allCommands();
    static std::vector<CommandId> allCommandIds();

    /// 命令菜单栏和工具栏

    static CommandLayout* menubarLayout(); // 默认菜单布局
    static CommandLayout* toolbarLayout(); // 默认工具栏布局
    static void renderMenuBar(CommandLayout* layout, QMenuBar* menubar);
    static void renderMenuBar(CommandLayout::Item* parent, QMenuBar* menubar);
    static void renderMenu(CommandLayout::Item* parent, QMenu* menu);
    static void renderToolBar(CommandLayout* layout, QMainWindow* window);
    static void renderToolBar(CommandLayout::Item* parent, QToolBar* toolbar);
    static bool saveLayout(const QString& path);
    static bool loadLayout(const QString& path);

    /// 命令上下文

    static void pushContext(const ContextId& context, const void* source);
    static void popContext(const ContextId& context, const void* source);
    static void releaseContext(const void* source);
    static std::unordered_set<ContextId> activeContexts();
    static bool isActiveContext(const ContextId& context) noexcept;
    static uint64_t activationOrder(const ContextId& context) noexcept;

    /// 快捷键管理

    static QString shortcutString(const QKeySequence& shortcut,
                                  QKeySequence::SequenceFormat format = QKeySequence::NativeText);
    static QList<QKeySequence> shortcuts(const CommandId& id);
    static QList<QKeySequence> defaultShortcuts(const CommandId& id);
    static bool setShortcuts(const CommandId& id, const QList<QKeySequence>& shortcuts,
                             bool checkConflicts = true);
    static bool addShortcut(const CommandId& id, const QKeySequence& shortcut,
                            bool checkConflicts = true);
    static void removeShortcut(const CommandId& id, const QKeySequence& shortcut);
    static void clearShortcuts(const CommandId& id);
    static void resetDefaultShortcut(const CommandId& id);
    static void resetAllDefaultShortcut();
    static QList<CommandId> conflictCommands(const QKeySequence& shortcut);
    static QList<ShortcutConflict> shortcutConflicts();
    static QList<ShortcutBinding> shortcutBindings();
    static QList<ShortcutBinding> modifiedShortcutBindings();
    static bool isCommandModified(const CommandId& id);
    static QList<CommandId> commandsWithShortcut(const QKeySequence& shortcut);
    static bool saveShortcuts(const QString& filePath);
    static bool restoreShortcuts(const QString& filePath);

    // 命令管理器
    static CommandManager& commandManager();

    // 快捷键管理器
    static ShortcutManager& shortcutManager();

private:
    CommandSystem() = default;
};

// RAII 生命周期管理器
class ContextGuard
{
public:
    ContextGuard(ContextId context, const void* source)
        : m_context(std::move(context))
        , m_source(source)
        , m_dismissed(false)
    {
        CommandSystem::pushContext(m_context, m_source);
    }

    ~ContextGuard()
    {
        if (!m_dismissed) {
            CommandSystem::popContext(m_context, m_source);
        }
    }

    // 允许提前解除或手动控制
    void dismiss() { m_dismissed = true; }

private:
    ContextId m_context;
    const void* m_source;
    bool m_dismissed;
};

} // namespace bakuon::gui
