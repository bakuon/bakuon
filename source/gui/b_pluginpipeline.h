#pragma once

#include <deque>
#include <functional>
#include <memory>
#include <optional>

#include <QtCore/QDateTime>
#include <QtCore/QMetaType>
#include <QtCore/QObject>
#include <QtCore/QPluginLoader>
#include <QtCore/QString>
#include <QtCore/QStringList>

#include <bakuon/gui/IPlugin.h>
#include <bakuon/gui/PluginContext.h>

#include "core/b_pluginlifecycle.h"
#include "gui/b_gui_export.h"
#include "gui/b_pluginmetadata.h"

namespace bakuon::gui {

// State machine enums + transition table live in core (pure C++, no Qt).
using PluginState          = core::PluginState;
using PluginEvent          = core::PluginEvent;
using PluginLifecycleRules = core::PluginLifecycleRules;

BAKUON_GUI_EXPORT QString toString(PluginState state);

class BAKUON_GUI_EXPORT PluginPipeline : public QObject
{
    Q_OBJECT
public:
    PluginPipeline(size_t id, QString filePath, QObject *parent = nullptr);
    PluginPipeline(size_t id, std::shared_ptr<IPlugin> instance, QObject *parent = nullptr);
    ~PluginPipeline() override;

    [[nodiscard]] size_t id() const noexcept { return m_id; }
    [[nodiscard]] PluginState state() const noexcept { return m_state; }
    [[nodiscard]] const QString &filePath() const noexcept { return m_filePath; }
    [[nodiscard]] const PluginMetadata &metadata() const noexcept { return m_metadata; }
    [[nodiscard]] QString lastError() const { return m_lastError; }
    [[nodiscard]] bool isFailed() const noexcept;

    [[nodiscard]] std::optional<QDateTime> discoveredAt() const noexcept { return m_discoveredAt; }
    [[nodiscard]] std::optional<QDateTime> loadedAt() const noexcept { return m_loadedAt; }
    [[nodiscard]] std::optional<QDateTime> initializedAt() const noexcept
    {
        return m_initializedAt;
    }
    [[nodiscard]] std::optional<QDateTime> runningAt() const noexcept { return m_runningAt; }

    [[nodiscard]] QStringList argumentValues() const noexcept { return m_argumentValues; }
    void setArgumentValues(QStringList values) { m_argumentValues = std::move(values); }

    using ResolveHook = std::function<std::optional<QString>(const PluginMetadata &)>;
    void setResolveHook(ResolveHook hook) { m_resolveHook = std::move(hook); }

    bool handle(PluginEvent event);
    bool launch();
    bool run();
    bool stop();
    bool unload();

Q_SIGNALS:
    void stateChanged(size_t id, PluginState state);
    void running(size_t id);
    void failed(size_t id, PluginState state, const QString &reason);

private:
    bool processQueue();
    bool reactState();
    void recordTimestamp(PluginState state);

    void executeDiscover();
    void executeValidate();
    void executeResolve();
    void executeLoad();
    void executeInitialize();
    void executeStop();
    void executeUnload();

private:
    size_t m_id;
    QString m_filePath;
    PluginState m_state = PluginState::Idle;
    QString m_lastError;
    PluginMetadata m_metadata;
    ResolveHook m_resolveHook;

    std::optional<QDateTime> m_discoveredAt;
    std::optional<QDateTime> m_loadedAt;
    std::optional<QDateTime> m_initializedAt;
    std::optional<QDateTime> m_runningAt;

    std::unique_ptr<QPluginLoader> m_loader;
    std::shared_ptr<IPlugin> m_instance;
    QStringList m_argumentValues;

    std::deque<PluginEvent> m_pendingEvents;
    bool m_processing = false;
};

} // namespace bakuon::gui

Q_DECLARE_METATYPE(bakuon::gui::PluginState)
