#include "gui/b_commandsystem.h"

#include <QtCore/QDebug>
#include <QtCore/QFile>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>

namespace bakuon::gui {

namespace {

// 动态属性的键名：用于在任意 QObject/QWidget 上标记"获得焦点时应激活的上下文集合"。
constexpr char kProviderContextsPropertyName[] = "bakuon_provider_contexts";

} // namespace

CommandWorkspace::CommandWorkspace()
    : m_shortcutManager(m_cmdManager)
{
    m_ctxArbiter.setCommandManager(&m_cmdManager);
}

/**
 * @brief 获取进程默认工作区（Construct-On-First-Use，无静态析构风险）
 *
 * 使用函数局部静态变量，C++11 起保证线程安全的初始化；
 * 堆分配后永不 delete（intentional leak），避免 exit-time destructor。
 */
static CommandWorkspace& defaultWorkspace()
{
    static auto* d = new CommandWorkspace;
    return *d;
}

CommandWorkspace& CommandSystem::workspace()
{
    return defaultWorkspace();
}

Command& CommandSystem::registerCommand(const CommandId& id, const QString& text)
{
    Command& cmd = *defaultWorkspace().commandManager().registerCommand(id, text);
    defaultWorkspace().shortcutManager().observeCommand(cmd.id());
    return cmd;
}

void CommandSystem::unregisterCommand(const CommandId& id)
{
    defaultWorkspace().shortcutManager().forgetCommand(id);
    defaultWorkspace().commandManager().unregisterCommand(id);
}

Command* CommandSystem::command(const CommandId& id)
{
    return defaultWorkspace().commandManager().command(id);
}

std::vector<Command*> CommandSystem::allCommands()
{
    return defaultWorkspace().commandManager().allCommands();
}

std::vector<CommandId> CommandSystem::allCommandIds()
{
    return defaultWorkspace().commandManager().allCommandIds();
}

CommandLayout* CommandSystem::menubarLayout()
{
    return defaultWorkspace().commandManager().menubarLayout();
}

CommandLayout* CommandSystem::toolbarLayout()
{
    return defaultWorkspace().commandManager().toolbarLayout();
}

void CommandSystem::renderMenuBar(CommandLayout* layout, QMenuBar* menubar)
{
    defaultWorkspace().commandManager().renderMenuBar(layout, menubar);
}

void CommandSystem::renderMenuBar(CommandLayout::Item* parent, QMenuBar* menubar)
{
    defaultWorkspace().commandManager().renderMenuBar(parent, menubar);
}

void CommandSystem::renderMenu(CommandLayout::Item* parent, QMenu* menu)
{
    defaultWorkspace().commandManager().renderMenu(parent, menu);
}

void CommandSystem::renderToolBar(CommandLayout* layout, QMainWindow* window)
{
    defaultWorkspace().commandManager().renderToolBar(layout, window);
}

void CommandSystem::renderToolBar(CommandLayout::Item* parent, QToolBar* toolbar)
{
    defaultWorkspace().commandManager().renderToolBar(parent, toolbar);
}

bool CommandSystem::saveLayout(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        qWarning() << "CommandSystem::saveLayout: Unable to open file for writing" << path
                   << file.errorString();
        return false;
    }

    QJsonObject obj;
    obj[QLatin1String("menubar")] = defaultWorkspace().commandManager().menubarLayout()->serialize();
    obj[QLatin1String("toolbar")] = defaultWorkspace().commandManager().toolbarLayout()->serialize();
    const auto size = file.write(QJsonDocument(obj).toJson(QJsonDocument::Indented));
    return size > 0;
}

bool CommandSystem::loadLayout(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "CommandSystem::loadLayout: Unable to open file for reading" << path
                   << file.errorString();
        return false;
    }

    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        qWarning() << "CommandSystem::loadLayout: JSON parsing failed" << err.errorString();
        return false;
    }

    const auto obj = doc.object();
    defaultWorkspace().commandManager().menubarLayout()->deserialize(
        obj.value(QLatin1String("menubar")).toObject());
    // BUGFIX: 原先错误地用了 "menubar" 键去反序列化 toolbar，导致工具栏布局永远被菜单栏覆盖
    defaultWorkspace().commandManager().toolbarLayout()->deserialize(
        obj.value(QLatin1String("toolbar")).toObject());
    return true;
}

std::shared_ptr<ContextState> CommandSystem::registerContext(const ContextId& id,
                                                             const QString& owner,
                                                             const QString& description)
{
    return defaultWorkspace().contextArbiter().registerContext(id, owner, description);
}

std::optional<ContextArbiter::ContextInfo> CommandSystem::contextInfo(const ContextId& id)
{
    return defaultWorkspace().contextArbiter().contextInfo(id);
}

std::vector<ContextId> CommandSystem::registeredContexts()
{
    return defaultWorkspace().contextArbiter().registeredContexts();
}

ContextId CommandSystem::declareContext(const QString& idString, const QString& owner,
                                        const QString& description)
{
    ContextId id{idString};
    defaultWorkspace().contextArbiter().registerContext(id, owner, description);
    return id;
}

std::shared_ptr<ContextState> CommandSystem::context(const ContextId& ctxId)
{
    return defaultWorkspace().contextArbiter().context(ctxId);
}

std::vector<ContextId> CommandSystem::contextsForCommand(const CommandId& cmdId)
{
    return defaultWorkspace().contextArbiter().contextsForCommand(cmdId);
}

void CommandSystem::pushContext(const ContextId& context, const void* source, ContextTier tier)
{
    defaultWorkspace().contextArbiter().pushContext(context, source, tier);
}

void CommandSystem::popContext(const ContextId& context, const void* source, ContextTier tier)
{
    defaultWorkspace().contextArbiter().popContext(context, source, tier);
}

void CommandSystem::releaseContext(const void* source)
{
    defaultWorkspace().contextArbiter().releaseContext(source);
}

std::unordered_set<ContextId> CommandSystem::activeContexts()
{
    return defaultWorkspace().contextArbiter().activeContexts();
}

bool CommandSystem::isActiveContext(const ContextId& context) noexcept
{
    return defaultWorkspace().contextArbiter().isActiveContext(context);
}

uint64_t CommandSystem::activationOrder(const ContextId& context) noexcept
{
    return defaultWorkspace().contextArbiter().activationOrder(context);
}

void CommandSystem::setProviderContexts(QObject* widget, const Context& context)
{
    Q_ASSERT(widget != nullptr);
    widget->setProperty(kProviderContextsPropertyName, context.toStringList());
}

Context CommandSystem::providerContext(const QObject* widget)
{
    if (!widget) {
        return {};
    }
    const QVariant v = widget->property(kProviderContextsPropertyName);
    if (!v.isValid()) {
        return {};
    }

    Context result;
    const QStringList names = v.toStringList();
    result.reserve(static_cast<size_t>(names.size()));
    for (const QString& name : names) {
        if (ContextId id{name}; id.isValid()) {
            result.append(id);
        }
    }
    return result;
}

QString CommandSystem::shortcutString(const QKeySequence& shortcut,
                                      QKeySequence::SequenceFormat format)
{
    return shortcut.toString(format);
}

QList<QKeySequence> CommandSystem::shortcuts(const CommandId& id)
{
    return defaultWorkspace().shortcutManager().shortcuts(id);
}

QList<QKeySequence> CommandSystem::defaultShortcuts(const CommandId& id)
{
    return defaultWorkspace().shortcutManager().defaultShortcuts(id);
}

bool CommandSystem::setShortcuts(const CommandId& id, const QList<QKeySequence>& shortcuts,
                                 bool checkConflicts)
{
    return defaultWorkspace().shortcutManager().setShortcuts(id, shortcuts, checkConflicts);
}

bool CommandSystem::addShortcut(const CommandId& id, const QKeySequence& shortcut,
                                bool checkConflicts)
{
    return defaultWorkspace().shortcutManager().addShortcut(id, shortcut, checkConflicts);
}

void CommandSystem::removeShortcut(const CommandId& id, const QKeySequence& shortcut)
{
    defaultWorkspace().shortcutManager().removeShortcut(id, shortcut);
}

void CommandSystem::clearShortcuts(const CommandId& id)
{
    defaultWorkspace().shortcutManager().clearShortcuts(id);
}

void CommandSystem::resetDefaultShortcut(const CommandId& id)
{
    defaultWorkspace().shortcutManager().resetToDefaults(id);
}

void CommandSystem::resetAllDefaultShortcut()
{
    defaultWorkspace().shortcutManager().resetAllToDefaults();
}

QList<CommandId> CommandSystem::conflictCommands(const QKeySequence& shortcut)
{
    return defaultWorkspace().shortcutManager().collectConflicts(shortcut);
}

QList<ShortcutConflict> CommandSystem::shortcutConflicts()
{
    return defaultWorkspace().shortcutManager().findAllConflicts();
}

QList<ShortcutBinding> CommandSystem::shortcutBindings()
{
    return defaultWorkspace().shortcutManager().allBindings();
}

QList<ShortcutBinding> CommandSystem::modifiedShortcutBindings()
{
    return defaultWorkspace().shortcutManager().modifiedBindings();
}

bool CommandSystem::isCommandModified(const CommandId& id)
{
    return defaultWorkspace().shortcutManager().isModified(id);
}

QList<CommandId> CommandSystem::commandsWithShortcut(const QKeySequence& shortcut)
{
    return defaultWorkspace().shortcutManager().commandsWithShortcut(shortcut);
}

bool CommandSystem::saveShortcuts(const QString& filePath)
{
    return defaultWorkspace().shortcutManager().saveToFile(filePath);
}

bool CommandSystem::restoreShortcuts(const QString& filePath)
{
    return defaultWorkspace().shortcutManager().loadFromFile(filePath);
}

CommandManager& CommandSystem::commandManager()
{
    return defaultWorkspace().commandManager();
}

ShortcutManager& CommandSystem::shortcutManager()
{
    return defaultWorkspace().shortcutManager();
}

} // namespace bakuon::gui
