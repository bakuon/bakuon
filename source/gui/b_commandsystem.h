#pragma once

#include <QtCore/QDataStream>

#include "gui/b_command.h"
#include "gui/b_commandlayout.h"
#include "gui/b_commandmanager.h"
#include "gui/b_context.h"
#include "gui/b_contextarbiter.h"
#include "gui/b_contextstatus.h"
#include "gui/b_shortcutmanager.h"
#include "gui/b_types.h"

namespace bakuon::gui {

/**
 * @brief 一套独立的命令 / 上下文 / 快捷键工作区。
 *
 * CommandSystem 静态门面委托给进程默认实例；多窗口 / 插件沙箱可以各自持有
 * 一个 CommandWorkspace，彼此的命令注册表和激活集合完全隔离。
 */
class CommandWorkspace
{
public:
    CommandWorkspace();
    CommandWorkspace(const CommandWorkspace&)            = delete;
    CommandWorkspace& operator=(const CommandWorkspace&) = delete;
    CommandWorkspace(CommandWorkspace&&)                 = delete;
    CommandWorkspace& operator=(CommandWorkspace&&)      = delete;
    ~CommandWorkspace()                                  = default;

    CommandManager& commandManager() noexcept { return m_cmdManager; }
    const CommandManager& commandManager() const noexcept { return m_cmdManager; }

    ShortcutManager& shortcutManager() noexcept { return m_shortcutManager; }
    const ShortcutManager& shortcutManager() const noexcept { return m_shortcutManager; }

    ContextArbiter& contextArbiter() noexcept { return m_ctxArbiter; }
    const ContextArbiter& contextArbiter() const noexcept { return m_ctxArbiter; }

private:
    ContextArbiter m_ctxArbiter;
    CommandManager m_cmdManager;
    ShortcutManager m_shortcutManager;
};

/**
 * @brief 命令系统门面 Command Facade
 * @todo 未来可能会走向“多个独立的命令系统实例”，
  * 进程默认工作区的静态入口。需要隔离实例时直接构造 CommandWorkspace。
 * 因为每个插件沙箱都可能有各自隔离的一套命令和上下文，尤其是多文档多窗口的编辑器。
 * 
 * 而使用命名空间的原则是：
 * Google C++ 风格指南和 C++ Core Guidelines 都明确建议:
 * 如果一个类里只有 static 成员、没有任何实例状态,就不该是类,
 * 应该用命名空间。这条建议的落脚点很直接——class 的存在意义是
 * "封装状态 + 行为不变量",一旦你发现自己在写 private: SomeClass() = default; 
 * 只是为了"防止被 new 出来",这本身就是一个信号:
 * 你其实想要的是命名空间,只是习惯性地用类的语法在表达它。
 *
 * // 拆成语义明确的子命名空间,而不是维持一个大而全的静态门面
 * namespace bakuon::gui::commands { Command& registerCommand(...); ... }
 * namespace bakuon::gui::context  { bool registerContext(...); void pushContext(...); ... }
 * namespace bakuon::gui::shortcuts{ QString shortcutString(...); ... }
 * namespace bakuon::gui::layout   { CommandLayout* menubarLayout(); ... }
 *
 * // 拆散之后失去"一个入口好记好敲代码补全"的体验可加一层薄的汇聚命名空间做门面
 * namespace bakuon::gui::cmd {
 *     using namespace commands;
 *     using namespace context;
 *     using namespace shortcuts;
 *     using namespace layout;
 * }
 *
 * 唯一真正值得权衡、不能一句话打发的点是:
 * 如果未来真的会走向"多个独立的命令系统实例"(比如每个文档窗口、
 * 每个插件沙箱各自隔离一套命令注册表和上下文),那从"静态门面类"演进到"可实
 * 例化的类"是渐进式改动;而从"命名空间自由函数"演进到那一步,是结构性重写。
 * 给定是 IDE/工业软件编辑器这种有可能走向多文档、多窗口架构的项目,这确实是一个不能忽略的伏笔。
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
    // 详细的冲突判定规则见 ContextTracker::registerContext() 的注释。
    static std::shared_ptr<ContextState> registerContext(const ContextId& id, const QString& owner,
                                                         const QString& description);
    static std::optional<ContextArbiter::ContextInfo> contextInfo(const ContextId& id);
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
     *
     * @todo 之后可以内置声明标准命令（StandardCommands）如保存、复制、删除等
     */
    static ContextId declareContext(const QString& id, const QString& owner,
                                    const QString& description);

    static std::shared_ptr<ContextState> context(const ContextId& ctxId);
    static std::vector<ContextId> contextsForCommand(const CommandId& cmdId);

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

    // 进程默认工作区（Construct-On-First-Use）
    static CommandWorkspace& workspace();

private:
    CommandSystem() = default;
};

// RAII 生命周期管理器：构造时 pushContext，析构时自动 popContext（除非 dismiss）。
// 不可拷贝；支持移动以便从工厂函数返回。
class ContextGuard
{
public:
    ContextGuard(const ContextId& context, const void* source,
                 ContextTier tier = ContextTier::Foreground)
        : m_context(context)
        , m_source(source)
        , m_tier(tier)
        , m_dismissed(false)
    {
        CommandSystem::pushContext(m_context, m_source, m_tier);
    }

    ContextGuard(const ContextGuard&)            = delete;
    ContextGuard& operator=(const ContextGuard&) = delete;

    ContextGuard(ContextGuard&& other) noexcept
        : m_context(other.m_context)
        , m_source(other.m_source)
        , m_tier(other.m_tier)
        , m_dismissed(other.m_dismissed)
    {
        other.m_dismissed = true; // 源对象不再负责 pop
    }

    ContextGuard& operator=(ContextGuard&& other) noexcept
    {
        if (this != &other) {
            if (!m_dismissed) {
                CommandSystem::popContext(m_context, m_source, m_tier);
            }
            m_context         = other.m_context;
            m_source          = other.m_source;
            m_tier            = other.m_tier;
            m_dismissed       = other.m_dismissed;
            other.m_dismissed = true;
        }
        return *this;
    }

    ~ContextGuard()
    {
        if (!m_dismissed) {
            CommandSystem::popContext(m_context, m_source, m_tier);
        }
    }

    /// 提前解除职责：析构时不再 pop（调用方需自行保证对称的 pop/release）
    void dismiss() noexcept { m_dismissed = true; }

    bool isActive() const noexcept { return !m_dismissed; }
    const ContextId& context() const noexcept { return m_context; }
    ContextTier tier() const noexcept { return m_tier; }

private:
    ContextId m_context;
    const void* m_source;
    ContextTier m_tier;
    bool m_dismissed;
};

} // namespace bakuon::gui
