#include "gui/b_commandsystem.h"

#include <QtCore/QFile>
#include <QtCore/QJsonDocument>

#include "gui/b_contexttracker.h"

namespace bakuon::gui {

namespace {
// 动态属性的键名：用于在任意 QObject/QWidget 上标记"获得焦点时应激活的上下文集合"。
constexpr char kProviderContextsPropertyName[] = "bakuon_provider_contexts";
} // namespace

// TODO: 把 CommandSystemPrivate 从藏在 .cpp 里的匿名类,升级成一个正式的、公开可见的类型
// 建立"一套独立的命令/上下文/快捷键体系" CommandWorkspace
class CommandSystemPrivate
{
public:
    CommandSystemPrivate()
        : ctxTracker()
        , cmdManager(ctxTracker)
        , shortcutManager(cmdManager)
    {
        // Command/CommandManager 不再自己认识具体的 ContextTracker 类型（只依赖
        // IContextArbiter 这个纯接口做仲裁查询），"上下文集合变了该找谁重新仲裁"
        // 这条连线因此要在这里、由真正同时持有两者的一方来接——这是 Command/
        // CommandManager 摆脱 ContextTracker 具体依赖之后，唯一新增的装配工作。
        QObject::connect(&ctxTracker,
                         &ContextTracker::contextChanged,
                         &cmdManager,
                         &CommandManager::resyncAllCommands);
    }

    ContextTracker ctxTracker;
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
    return get().ctxTracker.registerContext(id, owner, description);
}

std::optional<ContextTracker::ContextInfo> CommandSystem::contextInfo(const ContextId& id)
{
    return get().ctxTracker.contextInfo(id);
}

std::vector<ContextId> CommandSystem::registeredContexts()
{
    return get().ctxTracker.registeredContexts();
}

ContextId CommandSystem::declareContext(const QString& idString, const QString& owner,
                                        const QString& description)
{
    ContextId id{idString};
    get().ctxTracker.registerContext(id, owner, description);
    return id;
}

void CommandSystem::pushContext(const ContextId& context, const void* source, ContextTier tier)
{
    get().ctxTracker.pushContext(context, source, tier);
}

void CommandSystem::popContext(const ContextId& context, const void* source, ContextTier tier)
{
    get().ctxTracker.popContext(context, source, tier);
}

void CommandSystem::releaseContext(const void* source)
{
    get().ctxTracker.releaseContext(source);
}

std::unordered_set<ContextId> CommandSystem::activeContexts()
{
    return get().ctxTracker.activeContexts();
}

bool CommandSystem::isActiveContext(const ContextId& context) noexcept
{
    return get().ctxTracker.isActiveContext(context);
}

uint64_t CommandSystem::activationOrder(const ContextId& context) noexcept
{
    return get().ctxTracker.activationOrder(context);
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
