#include "gui/b_shortcutmanager.h"

#include <QtCore/QDebug>
#include <QtCore/QFile>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>

#include "gui/b_command.h"
#include "gui/b_commandmanager.h"

namespace bakuon::gui {

ShortcutManager::ShortcutManager(CommandManager& manager)
    : m_manager(manager)
{
}

void ShortcutManager::initialize()
{
    for (const auto& cmd : m_manager.allCommands()) {
        observeCommand(cmd->id());
    }
    rebuildIndex();
}

void ShortcutManager::observeCommand(const CommandId& id)
{
    captureDefaults(id);
    if (m_shortcuts.find(id) == m_shortcuts.end()) {
        auto current = commandLiveShortcuts(id);
        if (!current.isEmpty()) {
            m_shortcuts[id] = current;
        }
    }
    rebuildIndex();
}

void ShortcutManager::forgetCommand(const CommandId& id)
{
    m_shortcuts.erase(id);
    m_defaults.erase(id);
    m_modified.erase(id);
    rebuildIndex();
}

void ShortcutManager::captureDefaults(const CommandId& id)
{
    if (m_defaults.find(id) != m_defaults.end()) {
        return;
    }
    auto defaults = commandLiveDefaults(id);
    if (!defaults.isEmpty()) {
        m_defaults[id] = defaults;
    }
}

QList<QKeySequence> ShortcutManager::commandLiveShortcuts(const CommandId& id) const
{
    if (auto* cmd = m_manager.command(id)) {
        return cmd->shortcuts();
    }
    return {};
}

QList<QKeySequence> ShortcutManager::commandLiveDefaults(const CommandId& id) const
{
    if (auto* cmd = m_manager.command(id)) {
        return cmd->defaultShortcuts();
    }
    return {};
}

void ShortcutManager::rebuildIndex()
{
    m_shortcutIndex.clear();

    for (const auto& cmd : m_manager.allCommands()) {
        const auto seqs = shortcuts(cmd->id());
        for (const auto& shortcut : seqs) {
            if (!shortcut.isEmpty()) {
                m_shortcutIndex[shortcut].insert(cmd->id());
            }
        }
    }
}

QList<QKeySequence> ShortcutManager::shortcuts(const CommandId& id) const
{
    auto it = m_shortcuts.find(id);
    if (it != m_shortcuts.end()) {
        return it->second;
    }
    return commandLiveShortcuts(id);
}

QList<QKeySequence> ShortcutManager::defaultShortcuts(const CommandId& id) const
{
    auto it = m_defaults.find(id);
    if (it != m_defaults.end()) {
        return it->second;
    }
    return commandLiveDefaults(id);
}

bool ShortcutManager::setShortcuts(const CommandId& id, const QList<QKeySequence>& shortcuts,
                                   bool checkConflicts)
{
    if (!m_manager.command(id)) {
        qWarning() << "ShortcutManager: Command not found:" << id;
        return false;
    }

    // Check for conflicts if requested
    if (checkConflicts) {
        for (const auto& shortcut : shortcuts) {
            QList<CommandId> conflicts = collectConflicts(shortcut);
            // Remove self from conflicts
            conflicts.removeAll(id);

            if (!conflicts.isEmpty()) {
                ShortcutConflict conflict{shortcut, conflicts};
                conflict.conflictingCommands.prepend(id);

                qWarning() << "ShortcutManager: Conflict detected for" << shortcut
                           << "with commands:" << conflict.conflictingCommands;

                // Q_EMIT conflictDetected(conflict);
                Q_EMIT conflictDetected(shortcut, conflicts);

                return false;
            }
        }
    }

    // Update shortcuts
    captureDefaults(id);
    m_shortcuts[id] = shortcuts;
    updateCommandShortcuts(id, shortcuts);

    // Track modification: 与默认列表整体比较，允许多个默认快捷键
    if (shortcuts == defaultShortcuts(id)) {
        m_modified.erase(id);
    } else {
        m_modified.insert(id);
    }

    rebuildIndex();

    Q_EMIT shortcutsChanged(id, shortcuts);

    qDebug() << "ShortcutManager: Set shortcuts for" << id << ":" << shortcuts;

    return true;
}

bool ShortcutManager::addShortcut(const CommandId& id, const QKeySequence& shortcut,
                                  bool checkConflicts)
{
    QList<QKeySequence> current = shortcuts(id);

    // Check if already exists
    if (current.contains(shortcut)) {
        qDebug() << "ShortcutManager: Shortcut already exists for command:" << id;
        return true;
    }

    current.append(shortcut);
    return setShortcuts(id, current, checkConflicts);
}

void ShortcutManager::removeShortcut(const CommandId& id, const QKeySequence& shortcut)
{
    QList<QKeySequence> current = shortcuts(id);
    current.removeAll(shortcut);
    setShortcuts(id, current, false);
}

void ShortcutManager::clearShortcuts(const CommandId& id)
{
    setShortcuts(id, QList<QKeySequence>(), false);
}

void ShortcutManager::resetToDefaults(const CommandId& id)
{
    QList<QKeySequence> defaultSeqs = defaultShortcuts(id);

    if (defaultSeqs.isEmpty()) {
        clearShortcuts(id);
    } else {
        setShortcuts(id, defaultSeqs, false);
    }

    m_modified.erase(id);

    qDebug() << "ShortcutManager: Reset shortcuts for" << id << "to default";
}

void ShortcutManager::resetAllToDefaults()
{
    for (const auto& cmd : m_manager.allCommands()) {
        resetToDefaults(cmd->id());
    }

    Q_EMIT shortcutsReset();

    qDebug() << "ShortcutManager: All shortcuts reset to defaults";
}

QList<CommandId> ShortcutManager::collectConflicts(const QKeySequence& shortcut) const
{
    QList<CommandId> conflicts;

    auto it = m_shortcutIndex.find(shortcut);
    if (it != m_shortcutIndex.end()) {
        for (const auto& cmdId : it->second) {
            conflicts.append(cmdId);
        }
    }

    return conflicts;
}

QList<ShortcutConflict> ShortcutManager::findAllConflicts() const
{
    QList<ShortcutConflict> conflicts;

    for (const auto& [shortcut, commands] : m_shortcutIndex) {
        if (commands.size() > 1) {
            ShortcutConflict conflict;
            conflict.shortcut = shortcut;
            for (const auto& cmdId : commands) {
                conflict.conflictingCommands.append(cmdId);
            }
            conflicts.append(conflict);
        }
    }

    return conflicts;
}

QList<ShortcutBinding> ShortcutManager::allBindings() const
{
    QList<ShortcutBinding> bindings;

    for (const auto& cmd : m_manager.allCommands()) {
        ShortcutBinding binding;
        binding.commandId        = cmd->id();
        binding.shortcuts        = shortcuts(cmd->id());
        binding.defaultShortcuts = defaultShortcuts(cmd->id());
        binding.isModified       = isModified(cmd->id());

        bindings.append(binding);
    }

    return bindings;
}

QList<ShortcutBinding> ShortcutManager::modifiedBindings() const
{
    QList<ShortcutBinding> bindings;

    for (const auto& id : m_modified) {
        ShortcutBinding binding;
        binding.commandId        = id;
        binding.shortcuts        = shortcuts(id);
        binding.defaultShortcuts = defaultShortcuts(id);
        binding.isModified       = true;

        bindings.append(binding);
    }

    return bindings;
}

bool ShortcutManager::isModified(const CommandId& id) const
{
    return m_modified.find(id) != m_modified.end();
}

QList<CommandId> ShortcutManager::commandsWithShortcut(const QKeySequence& shortcut) const
{
    return collectConflicts(shortcut);
}

QJsonObject ShortcutManager::exportToJson() const
{
    QJsonObject root;
    QJsonArray commandsArray;

    for (const auto& binding : modifiedBindings()) {
        QJsonObject cmdObj;
        cmdObj["id"] = binding.commandId.toString();

        QJsonArray shortcutsArray;
        for (const auto& shortcut : binding.shortcuts) {
            shortcutsArray.append(shortcut.toString(QKeySequence::PortableText));
        }
        cmdObj["shortcuts"] = shortcutsArray;

        commandsArray.append(cmdObj);
    }

    root["version"]  = 1;
    root["commands"] = commandsArray;

    return root;
}

int ShortcutManager::importFromJson(const QJsonObject& json)
{
    int imported = 0;

    if (json["version"].toInt() != 1) {
        qWarning() << "ShortcutManager: Unsupported JSON version";
        return 0;
    }

    QJsonArray commandsArray = json["commands"].toArray();
    for (const auto value : commandsArray) {
        QJsonObject cmdObj = value.toObject();

        CommandId id = CommandId(cmdObj["id"].toString());
        if (!m_manager.command(id)) {
            qWarning() << "ShortcutManager: Unknown command in JSON:" << id;
            continue;
        }

        QList<QKeySequence> shortcuts;
        QJsonArray shortcutsArray = cmdObj["shortcuts"].toArray();
        for (const auto shortcutValue : shortcutsArray) {
            QString shortcutStr = shortcutValue.toString();
            QKeySequence seq    = QKeySequence::fromString(shortcutStr, QKeySequence::PortableText);
            if (!seq.isEmpty()) {
                shortcuts.append(seq);
            }
        }

        if (setShortcuts(id, shortcuts, false)) {
            imported++;
        }
    }

    qDebug() << "ShortcutManager: Imported" << imported << "shortcut bindings";

    return imported;
}

bool ShortcutManager::saveToFile(const QString& filePath) const
{
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        qWarning() << "ShortcutManager: Failed to open file for writing:" << filePath;
        return false;
    }

    QJsonObject json = exportToJson();
    QJsonDocument doc(json);

    qint64 written = file.write(doc.toJson(QJsonDocument::Indented));
    file.close();

    if (written == -1) {
        qWarning() << "ShortcutManager: Failed to write to file:" << filePath;
        return false;
    }

    qDebug() << "ShortcutManager: Saved shortcuts to" << filePath;
    return true;
}

bool ShortcutManager::loadFromFile(const QString& filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "ShortcutManager: Failed to open file for reading:" << filePath;
        return false;
    }

    QByteArray data = file.readAll();
    file.close();

    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (doc.isNull() || !doc.isObject()) {
        qWarning() << "ShortcutManager: Invalid JSON in file:" << filePath;
        return false;
    }

    int imported = importFromJson(doc.object());

    qDebug() << "ShortcutManager: Loaded" << imported << "shortcuts from" << filePath;

    return true;
}

QString ShortcutManager::shortcutString(const QKeySequence& shortcut,
                                        QKeySequence::SequenceFormat format)
{
    return shortcut.toString(format);
}

void ShortcutManager::updateCommandShortcuts(const CommandId& id,
                                             const QList<QKeySequence>& shortcuts)
{
    if (auto* cmd = m_manager.command(id)) {
        cmd->setShortcuts(shortcuts);
    }
}

} // namespace bakuon::gui
