#pragma once

#include <memory>

#include <QtCore/QObject>

#include <bakuon/core/Container.h>
#include <bakuon/core/Selection.h>

#include "gui/b_gui_export.h"
#include "gui/b_registrybridge.h"
#include "gui/b_types.h"

namespace bakuon::gui {

/**
 * @brief 单文档/场景会话：持有 core::Registry、有序 Selection、RegistryBridge，
 * 并把选中集非空状态映射为命令上下文激活。
 *
 * ## 职责边界
 * - **拥有** Registry 与 Selection（会话生命周期内有效）。
 * - **桥接** 到 Qt：RegistryBridge 供 HierarchyModel / 属性面板订阅。
 * - **上下文**：选中集从空→非空时 push 标准上下文；非空→空时 pop。
 *   默认 ContextId 为 `editor.selection`（可在构造时覆盖）。
 * - **不** 实现大纲 UI 本身（见 HierarchyModel）；**不** 实现具体业务命令。
 *
 * ## 线程
 * 与 core::Registry 相同：仅 GUI 主线程使用。
 *
 * ## 典型用法
 * @code
 *   auto* session = new DocumentSession(this);
 *   auto* model = new HierarchyModel(*session, this);
 *   treeView->setModel(model);
 *   // 命令侧已 declareContext("editor.selection", ...)
 *   // 选中实体后 Command 的 enabled 由 ContextArbiter 刷新
 * @endcode
 */
class BAKUON_GUI_EXPORT DocumentSession : public QObject
{
    Q_OBJECT
public:
    /**
     * @param selectionContext 选中非空时激活的上下文；默认 editor.selection。
     * @param parent Qt 父对象
     */
    explicit DocumentSession(ContextId selectionContext = ContextId{"editor.selection"},
                             QObject* parent            = nullptr);
    ~DocumentSession() override;

    DocumentSession(const DocumentSession&)            = delete;
    DocumentSession& operator=(const DocumentSession&) = delete;

    [[nodiscard]] core::Container& container() noexcept { return *m_container; }
    [[nodiscard]] const core::Container& container() const noexcept { return *m_container; }

    [[nodiscard]] core::selection::Selection& selection() noexcept { return *m_selection; }
    [[nodiscard]] const core::selection::Selection& selection() const noexcept
    {
        return *m_selection;
    }

    [[nodiscard]] RegistryBridge& bridge() noexcept { return *m_bridge; }
    [[nodiscard]] const RegistryBridge& bridge() const noexcept { return *m_bridge; }

    [[nodiscard]] const ContextId& selectionContextId() const noexcept
    {
        return m_selectionContext;
    }

    /// 当前是否因选中而激活了 selection 上下文
    [[nodiscard]] bool isSelectionContextActive() const noexcept { return m_selectionCtxActive; }

Q_SIGNALS:
    /// Selection 集合级变更（在 core Selection::onChanged 之后）
    void selectionChanged();

private:
    void onSelectionChanged();
    void syncSelectionContext();

    std::unique_ptr<core::Container> m_container;
    std::unique_ptr<core::selection::Selection> m_selection;
    std::unique_ptr<RegistryBridge> m_bridge;
    core::Connection m_selectionConn;

    ContextId m_selectionContext;
    bool m_selectionCtxActive = false;
};

} // namespace bakuon::gui
