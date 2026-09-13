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

#include "gui/b_gui_export.h"
#include "core/b_pluginlifecycle.h"
#include "gui/b_pluginmetadata.h"

namespace bakuon::gui {

/// ----------------------------------------------------------------------------
/// Pipeline 管道流转
///  事件是以 “多米诺骨牌（Pipeline）” 方式流转的,分为两类状态角色：
/// 1.过程态（-ing 状态，如 Validating, Resolving）：这类状态由主动事件（
/// 如 StartValidate）触发进入。进入后，状态机立刻执行对应的阻塞同步业务函数
/// （如执行 executeValidate()）。业务函数执行完毕后，根据其内部的
/// true/false 结果，主动向状态机投递 Success 或 Fail 事件。
///
/// 2.稳定态/结果态（-ed 状态，如 Validated, Resolved, Loaded）：这类状态由上一阶段
/// 的 Success 事件驱动进入。一旦进入结果态，onStateEntered/stateReact 路由表会
/// 立刻自动向下投递下一阶段的启动事件（如进入 Validated 后自动投递 StartResolve）。
/// -----------------------------------------------------------------------------

// 状态机枚举与转移表已抽到 core（纯 C++，无 Qt）。gui 再导出同名类型，保持既有调用点不变。
using PluginState = core::PluginState;
using PluginEvent = core::PluginEvent;
using PluginLifecycleRules = core::PluginLifecycleRules;

/// 供日志/UI/测试使用；不参与状态机逻辑本身。
BAKUON_GUI_EXPORT QString toString(PluginState state);

/**
 * @brief 单个插件从发现到卸载的完整生命周期管道。
 *
 * 用法（动态库插件）：
 *   PluginPipeline pipeline(id, filePath);
 *   pipeline.launch();           // 自动跑完 Discovering → ... → Initialized
 *   pipeline.run();              // Initialized → Running
 *   pipeline.stop();
 *   pipeline.unload();
 */
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
