#pragma once

#include <unordered_map>
#include <unordered_set>

#include <QtCore/QJsonObject>
#include <QtCore/QObject>
#include <QtGui/QKeySequence>

#include "gui/b_types.h"

// QKeySequence hash support for unordered containers
namespace std {
template<>
struct hash<QKeySequence>
{
    size_t operator()(const QKeySequence& seq) const noexcept
    {
        return static_cast<size_t>(qHash(seq.toString(QKeySequence::PortableText)));
    }
};

template<>
struct equal_to<QKeySequence>
{
    bool operator()(const QKeySequence& lhs, const QKeySequence& rhs) const noexcept
    {
        return lhs == rhs;
    }
};
} // namespace std

namespace bakuon::gui {

/**
 * @brief Shortcut conflict information
 */
struct ShortcutConflict
{
    QKeySequence shortcut;
    QList<CommandId> conflictingCommands;

    bool hasConflict() const { return conflictingCommands.size() > 1; }
};

/**
 * @brief Shortcut binding for a command
 */
struct ShortcutBinding
{
    CommandId commandId;
    QList<QKeySequence> shortcuts;
    QList<QKeySequence> defaultShortcuts;
    bool isModified = false; // Whether user has customized it

    bool hasShortcuts() const { return !shortcuts.isEmpty(); }
    bool isDefault() const { return !isModified && shortcuts == defaultShortcuts; }
};

class CommandManager;

/**
 * @brief 命令快捷键配置 Shortcut Configure / ShortcutMapper/Mapping
 */
class ShortcutManager : public QObject
{
    Q_OBJECT

public:
    explicit ShortcutManager(CommandManager& manager);
    ~ShortcutManager() override = default;

    /**
     * @brief Get current shortcuts for a command
     */
    QList<QKeySequence> shortcuts(const CommandId& id) const;

    /**
     * @brief Get default shortcut for a command
     */
    QList<QKeySequence> defaultShortcuts(const CommandId& id) const;

    /**
     * @brief Set shortcuts for a command
     * @param id Command ID
     * @param shortcuts New shortcuts
     * @param checkConflicts Whether to check for conflicts (default: true)
     * @return true if set successfully, false if conflicts detected and checkConflicts is true
     */
    bool setShortcuts(const CommandId& id, const QList<QKeySequence>& shortcuts,
                      bool checkConflicts = true);

    /**
     * @brief Add an additional shortcut to a command
     */
    bool addShortcut(const CommandId& id, const QKeySequence& shortcut, bool checkConflicts = true);

    /**
     * @brief Remove a shortcut from a command
     */
    void removeShortcut(const CommandId& id, const QKeySequence& shortcut);

    /**
     * @brief Clear all shortcuts for a command
     */
    void clearShortcuts(const CommandId& id);

    /**
     * @brief Reset command shortcuts to default
     */
    void resetToDefaults(const CommandId& id);

    /**
     * @brief Reset all command shortcuts to defaults
     */
    void resetAllToDefaults();

    /**
     * @brief Check if a shortcut conflicts with existing bindings
     * @return List of commands that would conflict
     */
    QList<CommandId> collectConflicts(const QKeySequence& shortcut) const;

    /**
     * @brief Find all conflicting shortcuts
     */
    QList<ShortcutConflict> findAllConflicts() const;

    /**
     * @brief Get all shortcut bindings
     */
    QList<ShortcutBinding> allBindings() const;

    /**
     * @brief Get modified bindings (customized by user)
     */
    QList<ShortcutBinding> modifiedBindings() const;

    /**
     * @brief Check if command has custom shortcuts
     */
    bool isModified(const CommandId& id) const;

    /**
     * @brief Get commands using a specific shortcut
     */
    QList<CommandId> commandsWithShortcut(const QKeySequence& shortcut) const;

    /**
     * @brief Export shortcuts to JSON
     */
    QJsonObject exportToJson() const;

    /**
     * @brief Import shortcuts from JSON
     * @return Number of shortcuts imported successfully
     */
    int importFromJson(const QJsonObject& json);

    /**
     * @brief Save shortcuts to file
     */
    bool saveToFile(const QString& filePath) const;

    /**
     * @brief Load shortcuts from file
     */
    bool loadFromFile(const QString& filePath);

    /**
     * @brief Snapshot defaults/current shortcuts from the command manager.
     * Safe to call after a batch of registerCommand + setDefaultShortcut.
     */
    void initialize();

    /**
     * @brief Remember a newly registered command (idempotent).
     */
    void observeCommand(const CommandId& id);

    /**
     * @brief Drop cached bindings when a command is unregistered.
     */
    void forgetCommand(const CommandId& id);

    /**
     * @brief Get shortcut display string
     * @param shortcut The key sequence
     * @param format Display format (NativeText or PortableText)
     */
    static QString shortcutString(const QKeySequence& shortcut,
                                  QKeySequence::SequenceFormat format = QKeySequence::NativeText);

Q_SIGNALS:
    /**
     * @brief Emitted when shortcuts change for a command
     */
    void shortcutsChanged(const CommandId& id, const QList<QKeySequence>& shortcuts);

    /**
     * @brief Emitted when a conflict is detected
     */
    void conflictDetected(const ShortcutConflict& conflict);
    void conflictDetected(const QKeySequence& conflictShortcut,
                          const QList<CommandId>& conflictingCommands);

    /**
     * @brief Emitted when shortcuts are reset to defaults
     */
    void shortcutsReset();

private:
    void rebuildIndex();
    void updateCommandShortcuts(const CommandId& id, const QList<QKeySequence>& shortcuts);
    void captureDefaults(const CommandId& id);
    QList<QKeySequence> commandLiveShortcuts(const CommandId& id) const;
    QList<QKeySequence> commandLiveDefaults(const CommandId& id) const;

    CommandManager& m_manager;

    // Command ID -> Shortcuts mapping
    std::unordered_map<CommandId, QList<QKeySequence>> m_shortcuts;

    // Command ID -> Default shortcut
    std::unordered_map<CommandId, QList<QKeySequence>> m_defaults;

    // Track which shortcuts have been modified
    std::unordered_set<CommandId> m_modified;

    // Reverse index: Shortcut -> Commands
    std::unordered_map<QKeySequence, std::unordered_set<CommandId>> m_shortcutIndex;
};

} // namespace bakuon::gui
