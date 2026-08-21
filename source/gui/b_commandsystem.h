#pragma once

#include <QtCore/QDataStream>

#include "gui/b_command.h"
#include "gui/b_commandlayout.h"
#include "gui/b_commandmanager.h"
#include "gui/b_context.h"
#include "gui/b_shortcutmanager.h"
#include "gui/b_types.h"

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

    /// 命令上下文注册表

    // 显式登记一个上下文 id（owner 必填，通常传入模块/插件的唯一标识），
    // 详细的冲突判定规则见 CommandManager::registerContext() 的注释。
    static bool registerContext(const ContextId& id, const QString& owner,
                                const QString& description);
    static std::optional<CommandManager::ContextInfo> contextInfo(const ContextId& id);
    static std::vector<ContextId> registeredContexts();
    /**
     * declareContext：注册 + 取得 ContextId 的一步到位版本，专为"模块级常量
     * 声明文件"设计，典型用法：
     *
     *   // EditorContexts.h
     *   namespace editor::contexts {
     *   inline const gui::ContextId kFocused =
     *       gui::CommandSystem::declareContext("editor.image.focused",
     *                                          "plugin.image",
     *                                          "图像编辑器获得焦点");
     *   }
     *
     * 这样业务代码里永远不会出现裸的 ContextId("字符串") 字面量——只会引用
     * 已经声明好的常量，重复/拼写不一致的问题从"运行时才会暴露"变成
     * "登记时就地址空间统一"。owner 不同的两次 declareContext 撞到同一个
     * 字符串会在这里被 qWarning 出来（见 registerContext），但仍然返回一个
     * 可用的 ContextId——不让命名冲突直接导致编译期/启动期崩溃，只是足够刺眼。
     */
    static ContextId declareContext(const QString& id, const QString& owner,
                                    const QString& description);

    /// 命令上下文激活/失活

    static void pushContext(const ContextId& context, const void* source,
                            ContextTier tier = ContextTier::Foreground);
    static void popContext(const ContextId& context, const void* source,
                           ContextTier tier = ContextTier::Foreground);
    static void releaseContext(const void* source);
    static std::unordered_set<ContextId> activeContexts();
    static bool isActiveContext(const ContextId& context) noexcept;
    static uint64_t activationOrder(const ContextId& context) noexcept;

    // 为焦点部件设置属性标签上下文
    static void setProviderContexts(QObject* widget, const Context& context);
    // 单上下文场景的语法糖
    static inline void setProviderContext(QObject* widget, const ContextId& context)
    {
        setProviderContexts(widget, Context{context});
    }
    static Context providerContext(const QObject* widget);

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
    ContextGuard(const ContextId& context, const void* source)
        : m_context(context)
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
