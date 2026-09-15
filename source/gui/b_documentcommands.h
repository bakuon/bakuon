#pragma once

#include <vector>

#include <QtCore/QObject>
#include <QtGui/QAction>

#include <bakuon/core/Handle.h>

#include "gui/b_gui_export.h"
#include "gui/b_types.h"

namespace bakuon::gui {

class DocumentSession;

/**
 * @brief 标准文档编辑命令与 DocumentSession 的绑定器。
 *
 * 在进程级 CommandSystem 中注册：
 *  - edit.delete
 *  - edit.rename
 *  - edit.duplicate
 *
 * 并把对应 QAction 挂到 session.selectionContextId() 上。
 * 上下文激活时 ContextArbiter 将这些 action 设为 Command 的 realAction，
 * 从而菜单/快捷键在选中非空时可用。
 *
 * ## 语义
 * - **Delete**：销毁选中实体的完整子树（hierarchy::destroy）。若父子同时选中，
 *   只销毁“选中集中无已选祖先”的节点，避免重复销毁。
 * - **Rename**：仅对 primary 生效；触发时发出 renameRequested(handle)，
 *   由 UI（对话框/就地编辑）决定新名后调用 applyRename()。
 * - **Duplicate**：复制 Name（若有），挂到原节点的下一兄弟位；无父则成为新根。
 *
 * Locked 实体：Delete 跳过；Duplicate 仍允许；Rename 跳过。
 *
 * 本类不拥有 DocumentSession；session 必须比本对象活得更久，或先销毁本对象。
 */
class BAKUON_GUI_EXPORT DocumentCommands : public QObject
{
    Q_OBJECT
public:
    static CommandId idDelete() { return CommandId{"edit.delete"}; }
    static CommandId idRename() { return CommandId{"edit.rename"}; }
    static CommandId idDuplicate() { return CommandId{"edit.duplicate"}; }

    explicit DocumentCommands(DocumentSession& session, QObject* parent = nullptr);
    ~DocumentCommands() override;

    DocumentCommands(const DocumentCommands&)            = delete;
    DocumentCommands& operator=(const DocumentCommands&) = delete;

    [[nodiscard]] QAction* deleteAction() const noexcept { return m_deleteAction; }
    [[nodiscard]] QAction* renameAction() const noexcept { return m_renameAction; }
    [[nodiscard]] QAction* duplicateAction() const noexcept { return m_duplicateAction; }

    /// UI 确认新名后调用；仅当 handle 仍有效且未锁定时写入 Name
    bool applyRename(core::Handle handle, const QString& newName);

public Q_SLOTS:
    void executeDelete();
    void executeDuplicate();
    /// 发出 renameRequested(primary)；无 primary 时为空操作
    void requestRename();

Q_SIGNALS:
    /// UI 应弹出重命名对话框 / 启动就地编辑
    void renameRequested(bakuon::core::Handle handle);

private:
    void registerCommands();
    void bindActionsToContext();
    void updateActionEnabled();
    void onSelectionChanged();

    /// 选中集中无已选祖先的节点（可安全独立 destroy）
    [[nodiscard]] std::vector<core::Handle> topLevelSelected() const;

    DocumentSession& m_session;
    QAction* m_deleteAction    = nullptr;
    QAction* m_renameAction    = nullptr;
    QAction* m_duplicateAction = nullptr;
};

} // namespace bakuon::gui
