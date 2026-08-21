#include "gui/b_commandsystem.h"

#include <QtCore/QFile>
#include <QtCore/QJsonDocument>

namespace bakuon::gui {

class CommandSystemPrivate
{
public:
    CommandSystemPrivate()
        : cmdManager()
        , shortcutManager(cmdManager)
    {
    }

    CommandManager cmdManager;
    ShortcutManager shortcutManager;
};

/**
 * @brief 获取进程单例（Construct-On-First-Use，无静态析构风险）
 *
 * 使用函数局部静态变量，C++11 起保证线程安全的初始化；
 * 堆分配后永不 delete（intentional leak），避免 exit-time destructor。
 */
static CommandSystemPrivate& get()
{
    // 指针本身是 trivial，不触发 exit-time destructor。
    static auto* d = new CommandSystemPrivate;
    return *d;
}

Command& CommandSystem::registerCommand(const CommandId& id, const QString& text)
{
    return get().cmdManager.registerCommand(id, text);
}

void CommandSystem::unregisterCommand(const CommandId& id)
{
    get().cmdManager.unregisterCommand(id);
}

Command* CommandSystem::command(const CommandId& id)
{
    return get().cmdManager.command(id);
}

std::vector<Command*> CommandSystem::allCommands()
{
    return get().cmdManager.allCommands();
}

std::vector<CommandId> CommandSystem::allCommandIds()
{
    return get().cmdManager.allCommandIds();
}

CommandLayout* CommandSystem::menubarLayout()
{
    return get().cmdManager.menubarLayout();
}

CommandLayout* CommandSystem::toolbarLayout()
{
    return get().cmdManager.toolbarLayout();
}

void CommandSystem::renderMenuBar(CommandLayout* layout, QMenuBar* menubar)
{
    get().cmdManager.renderMenuBar(layout, menubar);
}

void CommandSystem::renderMenuBar(CommandLayout::Item* parent, QMenuBar* menubar)
{
    get().cmdManager.renderMenuBar(parent, menubar);
}

void CommandSystem::renderMenu(CommandLayout::Item* parent, QMenu* menu)
{
    get().cmdManager.renderMenu(parent, menu);
}

void CommandSystem::renderToolBar(CommandLayout* layout, QMainWindow* window)
{
    get().cmdManager.renderToolBar(layout, window);
}

void CommandSystem::renderToolBar(CommandLayout::Item* parent, QToolBar* toolbar)
{
    get().cmdManager.renderToolBar(parent, toolbar);
}

bool CommandSystem::saveLayout(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        qWarning() << "CommandSystem::loadLayout: Unable to open file for writing" << path
                   << file.errorString();
        return false;
    }

    QJsonObject obj;
    obj[QLatin1String("menubar")] = get().cmdManager.menubarLayout()->serialize();
    obj[QLatin1String("toolbar")] = get().cmdManager.toolbarLayout()->serialize();
    auto size                     = file.write(QJsonDocument(obj).toJson(QJsonDocument::Indented));
    return size > 0;
}

bool CommandSystem::loadLayout(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "CommandLayout::loadFromFile: Unable to open file for reading" << path
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
    get().cmdManager.menubarLayout()->deserialize(obj.value(QLatin1String("menubar")).toObject());
    get().cmdManager.toolbarLayout()->deserialize(obj.value(QLatin1String("menubar")).toObject());
    return true;
}

bool CommandSystem::registerContext(const ContextId& id, const QString& owner,
                                    const QString& description)
{
    return get().cmdManager.registerContext(id, owner, description);
}

std::optional<CommandManager::ContextInfo> CommandSystem::contextInfo(const ContextId& id)
{
    return get().cmdManager.contextInfo(id);
}

std::vector<ContextId> CommandSystem::registeredContexts()
{
    return get().cmdManager.registeredContexts();
}

ContextId CommandSystem::declareContext(const QString& idString, const QString& description,
                                        const QString& owner)
{
    ContextId id{idString};
    get().cmdManager.registerContext(id, description, owner);
    return id;
}

void CommandSystem::pushContext(const ContextId& context, const void* source, ContextTier tier)
{
    get().cmdManager.pushContext(context, source, tier);
}

void CommandSystem::popContext(const ContextId& context, const void* source, ContextTier tier)
{
    get().cmdManager.popContext(context, source, tier);
}

void CommandSystem::releaseContext(const void* source)
{
    get().cmdManager.releaseContext(source);
}

std::unordered_set<ContextId> CommandSystem::activeContexts()
{
    return get().cmdManager.activeContexts();
}

bool CommandSystem::isActiveContext(const ContextId& context) noexcept
{
    return get().cmdManager.isActiveContext(context);
}

uint64_t CommandSystem::activationOrder(const ContextId& context) noexcept
{
    return get().cmdManager.activationOrder(context);
}

QString CommandSystem::shortcutString(const QKeySequence& shortcut,
                                      QKeySequence::SequenceFormat format)
{
    return shortcut.toString(format);
}

QList<QKeySequence> CommandSystem::shortcuts(const CommandId& id)
{
    return get().shortcutManager.shortcuts(id);
}

QList<QKeySequence> CommandSystem::defaultShortcuts(const CommandId& id)
{
    return get().shortcutManager.defaultShortcuts(id);
}

bool CommandSystem::setShortcuts(const CommandId& id, const QList<QKeySequence>& shortcuts,
                                 bool checkConflicts)
{
    return get().shortcutManager.setShortcuts(id, shortcuts, checkConflicts);
}

bool CommandSystem::addShortcut(const CommandId& id, const QKeySequence& shortcut,
                                bool checkConflicts)
{
    return get().shortcutManager.addShortcut(id, shortcut, checkConflicts);
}

void CommandSystem::removeShortcut(const CommandId& id, const QKeySequence& shortcut)
{
    get().shortcutManager.removeShortcut(id, shortcut);
}

void CommandSystem::clearShortcuts(const CommandId& id)
{
    get().shortcutManager.clearShortcuts(id);
}

void CommandSystem::resetDefaultShortcut(const CommandId& id)
{
    get().shortcutManager.resetToDefaults(id);
}

void CommandSystem::resetAllDefaultShortcut()
{
    get().shortcutManager.resetAllToDefaults();
}

QList<CommandId> CommandSystem::conflictCommands(const QKeySequence& shortcut)
{
    return get().shortcutManager.collectConflicts(shortcut);
}

QList<ShortcutConflict> CommandSystem::shortcutConflicts()
{
    return get().shortcutManager.findAllConflicts();
}

QList<ShortcutBinding> CommandSystem::shortcutBindings()
{
    return get().shortcutManager.allBindings();
}

QList<ShortcutBinding> CommandSystem::modifiedShortcutBindings()
{
    return get().shortcutManager.modifiedBindings();
}

bool CommandSystem::isCommandModified(const CommandId& id)
{
    return get().shortcutManager.isModified(id);
}

QList<CommandId> CommandSystem::commandsWithShortcut(const QKeySequence& shortcut)
{
    return get().shortcutManager.commandsWithShortcut(shortcut);
}

bool CommandSystem::saveShortcuts(const QString& filePath)
{
    return get().shortcutManager.saveToFile(filePath);
}

bool CommandSystem::restoreShortcuts(const QString& filePath)
{
    return get().shortcutManager.loadFromFile(filePath);
}

CommandManager& CommandSystem::commandManager()
{
    return get().cmdManager;
}

ShortcutManager& CommandSystem::shortcutManager()
{
    return get().shortcutManager;
}

} // namespace bakuon::gui
