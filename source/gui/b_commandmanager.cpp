#include "gui/b_commandmanager.h"

namespace bakuon::gui {

CommandManager::CommandManager(IContextArbiter& arbiter, QObject* parent)
    : QObject(parent)
    , m_arbiter(arbiter)
    , m_menubarLayout(std::make_unique<CommandLayout>())
    , m_toolbarLayout(std::make_unique<CommandLayout>())
{
}

Command& CommandManager::registerCommand(const CommandId& id, const QString& text)
{
    if (auto it = m_commands.find(id); it != m_commands.end()) {
        return *it->second;
    }
    auto command = std::make_unique<Command>(id, text, m_arbiter);
    Command& ref = *command;
    m_commands.emplace(id, std::move(command));
    return ref;
}

void CommandManager::unregisterCommand(const CommandId& id)
{
    auto it = m_commands.find(id);
    if (it != m_commands.end()) {
        m_commands.erase(it);
    }
}

Command* CommandManager::command(const CommandId& id) const
{
    auto it = m_commands.find(id);
    return it != m_commands.end() ? it->second.get() : nullptr;
}

std::vector<Command*> CommandManager::allCommands() const
{
    std::vector<Command*> result;
    result.reserve(m_commands.size());
    for (auto& [_, command] : m_commands) {
        result.push_back(command.get());
    }
    return result;
}

std::vector<CommandId> CommandManager::allCommandIds() const
{
    std::vector<CommandId> result;
    result.reserve(m_commands.size());
    for (const auto& [id, _] : m_commands) {
        result.push_back(id);
    }
    return result;
}

void CommandManager::resyncAllCommands()
{
    for (auto& [_, command] : m_commands) {
        command->resyncAuthoritativeBinding();
    }
}

CommandLayout* CommandManager::menubarLayout() const
{
    return m_menubarLayout.get();
}

CommandLayout* CommandManager::toolbarLayout() const
{
    return m_toolbarLayout.get();
}

void CommandManager::renderMenuBar(CommandLayout* layout, QMenuBar* menubar) const
{
    if (!layout)
        return;

    // QMenuBar::clear() 只清空"显示列表"，不会删除子 QMenu 对象——它们作为 QObject
    // 子对象仍然挂在 menubar 下；反复调用 build() 会不断积累"隐形"的孤儿 QMenu。
    // 直接 qDeleteAll 更彻底：QMenu 的父子关系是链式的（嵌套子菜单是其父菜单/menubar
    // 的 QObject 子对象），只删除 menubar 的直接子 QMenu 即可级联删除整棵子菜单树。
    qDeleteAll(menubar->findChildren<QMenu*>(QString(), Qt::FindDirectChildrenOnly));
    menubar->clear();

    renderMenuBar(layout->root(), menubar);
}

void CommandManager::renderMenuBar(CommandLayout::Item* parent, QMenuBar* menubar) const
{
    for (CommandLayout::Item* child : parent->children()) {
        const CommandLayoutData& v = child->data();
        switch (v.type) {
        case CommandLayoutData::Type::Container: {
            QMenu* sub = menubar->addMenu(v.title);
            renderMenu(child, sub);
            break;
        }
        case CommandLayoutData::Type::Command: {
            if (Command* cmd = this->command(CommandId(v.commandId))) {
                menubar->addAction(cmd->action());
            } else {
                // 布局引用了一个当前未注册的命令：只跳过显示，不修改数据本身，
                // 保留这条引用——命令后续被注册回来后，下一次 build() 会自动补上。
                qWarning()
                    << "CommandManager::renderMenuBar: No registered command was found, skipped:"
                    << v.commandId;
            }
            break;
        }
        case CommandLayoutData::Type::Separator:
            // QMenuBar 没有 addSeparator()（顶层菜单之间没有"分隔线"这个概念），
            // CommandLayout::addSeparator() 已经在数据层拒绝了这种结构。
            qWarning() << "CommandManager::renderMenuBar: "
                          "The top-level menu bar does not support separators; one separator node "
                          "has been ignored.";
            break;

        case CommandLayoutData::Type::Root: // 不会出现：Root 只应该是最外层，不会作为某个节点的子节点
        default                           : break;
        }
    }
}

void CommandManager::renderMenu(CommandLayout::Item* parent, QMenu* menu) const
{
    for (CommandLayout::Item* child : parent->children()) {
        const CommandLayoutData& v = child->data();
        switch (v.type) {
        case CommandLayoutData::Type::Container: {
            QMenu* sub = menu->addMenu(v.title);
            renderMenu(child, sub);
            break;
        }
        case CommandLayoutData::Type::Command: {
            if (Command* cmd = this->command(CommandId(v.commandId))) {
                menu->addAction(cmd->action());
            } else {
                // 布局引用了一个当前未注册的命令：只跳过显示，不修改数据本身，
                // 保留这条引用——命令后续被注册回来后，下一次 build() 会自动补上。
                qWarning()
                    << "CommandManager::renderMenu: No registered command was found, skipped:"
                    << v.commandId;
            }
            break;
        }
        case CommandLayoutData::Type::Separator: {
            menu->addSeparator();
            break;
        }
        case CommandLayoutData::Type::Root: // 不会出现：Root 只应该是最外层，不会作为某个节点的子节点
        default                           : break;
        }
    }
}

void CommandManager::renderToolBar(CommandLayout* layout, QMainWindow* window) const
{
    if (!layout)
        return;

    // 移除旧的动态工具栏（生产环境可根据 tag 精确清理）
    for (auto* toolbar : window->findChildren<QToolBar*>(QString(), Qt::FindDirectChildrenOnly)) {
        window->removeToolBar(toolbar);
        delete toolbar;
    }

    // 顶层每个 parent 是一个工具栏
    auto* root = layout->root();
    for (CommandLayout::Item* child : root->children()) {
        const QString title = child->data().title;
        auto* toolbar       = new QToolBar(title, window);
        toolbar->setObjectName(QString("Dynamic_%1").arg(title));
        renderToolBar(child, toolbar);
        window->addToolBar(toolbar);
    }
}

void CommandManager::renderToolBar(CommandLayout::Item* parent, QToolBar* toolbar) const
{
    if (!parent)
        return;
    for (CommandLayout::Item* child : parent->children()) {
        const CommandLayoutData& v = child->data();
        switch (v.type) {
        case CommandLayoutData::Type::Container: {
            // 不支持第二层嵌套
            break;
        }
        case CommandLayoutData::Type::Command: {
            if (Command* cmd = this->command(CommandId(v.commandId))) {
                toolbar->addAction(cmd->action());
            } else {
                qWarning()
                    << "CommandManager::renderToolBar: No registered command was found, skipped:"
                    << v.commandId;
            }
            break;
        }
        case CommandLayoutData::Type::Separator: {
            toolbar->addSeparator();
            break;
        }
        case CommandLayoutData::Type::Root: // 不会出现：Root 只应该是最外层，不会作为某个节点的子节点
        default                           : break;
        }
    }
}

} // namespace bakuon::gui
