#include "gui/b_documentcommands.h"

#include <algorithm>
#include <string>

#include <bakuon/core/Components.h>
#include <bakuon/core/Hierarchy.h>

#include "gui/b_commandsystem.h"
#include "gui/b_contextstatus.h"
#include "gui/b_documentsession.h"

namespace bakuon::gui {
namespace h = bakuon::core::hierarchy;
using bakuon::core::Handle;
using bakuon::core::components::Locked;
using bakuon::core::components::Name;

DocumentCommands::DocumentCommands(DocumentSession& session, QObject* parent)
    : QObject(parent)
    , m_session(session)
{
    registerCommands();
    bindActionsToContext();
    updateActionEnabled();

    connect(&m_session, &DocumentSession::selectionChanged, this,
            &DocumentCommands::onSelectionChanged);
}

DocumentCommands::~DocumentCommands()
{
    // 从上下文摘掉 action，避免悬空 QPointer 触发 actionsChanged
    if (auto ctx = CommandSystem::context(m_session.selectionContextId())) {
        ctx->removeAction(idDelete());
        ctx->removeAction(idRename());
        ctx->removeAction(idDuplicate());
    }
}

void DocumentCommands::registerCommands()
{
    auto& delCmd = CommandSystem::registerCommand(idDelete(), QStringLiteral("Delete"));
    delCmd.setDefaultShortcut(QKeySequence::Delete);

    CommandSystem::registerCommand(idRename(), QStringLiteral("Rename"));
    // F2 在部分平台是 Rename；用字面量保持跨平台一致
    if (auto* renameCmd = CommandSystem::command(idRename())) {
        renameCmd->setDefaultShortcut(QKeySequence(Qt::Key_F2));
    }

    CommandSystem::registerCommand(idDuplicate(), QStringLiteral("Duplicate"));
    if (auto* dupCmd = CommandSystem::command(idDuplicate())) {
        dupCmd->setDefaultShortcut(QKeySequence(QStringLiteral("Ctrl+D")));
    }

    m_deleteAction = new QAction(QStringLiteral("Delete"), this);
    m_deleteAction->setShortcut(QKeySequence::Delete);
    connect(m_deleteAction, &QAction::triggered, this, &DocumentCommands::executeDelete);

    m_renameAction = new QAction(QStringLiteral("Rename"), this);
    m_renameAction->setShortcut(QKeySequence(Qt::Key_F2));
    connect(m_renameAction, &QAction::triggered, this, &DocumentCommands::requestRename);

    m_duplicateAction = new QAction(QStringLiteral("Duplicate"), this);
    m_duplicateAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+D")));
    connect(m_duplicateAction, &QAction::triggered, this, &DocumentCommands::executeDuplicate);
}

void DocumentCommands::bindActionsToContext()
{
    const ContextId ctxId = m_session.selectionContextId();
    // 确保上下文已登记（DocumentSession 构造时也会 declare）
    auto ctx = CommandSystem::registerContext(ctxId, QStringLiteral("bakuon.gui"),
                                              QStringLiteral("Document selection is non-empty"));
    if (!ctx) {
        return;
    }
    ctx->addAction(idDelete(), m_deleteAction);
    ctx->addAction(idRename(), m_renameAction);
    ctx->addAction(idDuplicate(), m_duplicateAction);
}

void DocumentCommands::onSelectionChanged()
{
    updateActionEnabled();
}

void DocumentCommands::updateActionEnabled()
{
    const auto& sel = m_session.selection();
    const bool any  = !sel.empty();
    const Handle primary = sel.primary();

    bool canDelete = false;
    if (any) {
        for (Handle h : sel.ordered()) {
            if (!m_session.registry().valid(h)) {
                continue;
            }
            if (const Locked* locked = m_session.registry().tryGet<Locked>(h); locked && locked->value) {
                continue;
            }
            canDelete = true;
            break;
        }
    }

    bool canRename = false;
    if (primary.isValid() && m_session.registry().valid(primary)) {
        const Locked* locked = m_session.registry().tryGet<Locked>(primary);
        canRename = !(locked && locked->value);
    }

    m_deleteAction->setEnabled(canDelete);
    m_renameAction->setEnabled(canRename);
    m_duplicateAction->setEnabled(any);
}

std::vector<Handle> DocumentCommands::topLevelSelected() const
{
    const auto& ordered = m_session.selection().ordered();
    std::vector<Handle> result;
    result.reserve(ordered.size());

    auto& reg = m_session.registry();
    for (Handle h : ordered) {
        if (!reg.valid(h)) {
            continue;
        }
        bool ancestorSelected = false;
        for (Handle p = h::parent(reg, h); p.isValid(); p = h::parent(reg, p)) {
            if (m_session.selection().contains(p)) {
                ancestorSelected = true;
                break;
            }
        }
        if (!ancestorSelected) {
            result.push_back(h);
        }
    }
    return result;
}

void DocumentCommands::executeDelete()
{
    auto& reg = m_session.registry();
    const std::vector<Handle> targets = topLevelSelected();
    if (targets.empty()) {
        return;
    }

    // 先清空选择，避免销毁过程中 selection 持有失效 Handle
    m_session.selection().clear();

    for (Handle h : targets) {
        if (!reg.valid(h)) {
            continue;
        }
        if (const Locked* locked = reg.tryGet<Locked>(h); locked && locked->value) {
            continue;
        }
        h::destroy(reg, h);
    }
}

void DocumentCommands::executeDuplicate()
{
    auto& reg = m_session.registry();
    const std::vector<Handle> sources = m_session.selection().ordered();
    if (sources.empty()) {
        return;
    }

    std::vector<Handle> created;
    created.reserve(sources.size());

    for (Handle src : sources) {
        if (!reg.valid(src)) {
            continue;
        }

        const Handle dst = reg.create();
        if (const Name* name = reg.tryGet<Name>(src)) {
            reg.emplace<Name>(dst, Name{name->value + " Copy"});
        }

        const Handle parent = h::parent(reg, src);
        if (parent.isValid()) {
            // 插到 src 之后
            h::insertAfter(reg, dst, src);
        } else {
            // 成为独立根：ensure Hierarchy 组件（append 到一个临时？）
            // 无父时 attach 需要父节点；仅 ensure 组件即可被 roots() 收集
            reg.getOrEmplace<h::Hierarchy>(dst);
        }
        created.push_back(dst);
    }

    if (!created.empty()) {
        m_session.selection().set(std::move(created));
    }
}

void DocumentCommands::requestRename()
{
    const Handle primary = m_session.selection().primary();
    if (!primary.isValid() || !m_session.registry().valid(primary)) {
        return;
    }
    if (const Locked* locked = m_session.registry().tryGet<Locked>(primary);
        locked && locked->value) {
        return;
    }
    Q_EMIT renameRequested(primary);
}

bool DocumentCommands::applyRename(Handle handle, const QString& newName)
{
    auto& reg = m_session.registry();
    if (!reg.valid(handle)) {
        return false;
    }
    if (const Locked* locked = reg.tryGet<Locked>(handle); locked && locked->value) {
        return false;
    }

    const std::string value = newName.toStdString();
    if (reg.has<Name>(handle)) {
        reg.patch<Name>(handle, [&](Name& n) { n.value = value; });
    } else {
        reg.emplace<Name>(handle, Name{value});
    }
    return true;
}

} // namespace bakuon::gui
