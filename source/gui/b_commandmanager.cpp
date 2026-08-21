#include "gui/b_commandmanager.h"

namespace bakuon::gui {

CommandManager::CommandManager(QObject* parent)
    : QObject(parent)
    , m_menubarLayout(std::make_unique<CommandLayout>())
    , m_toolbarLayout(std::make_unique<CommandLayout>())
{
}

Command& CommandManager::registerCommand(const CommandId& id, const QString& text)
{
    if (auto it = m_commands.find(id); it != m_commands.end()) {
        return *it->second;
    }
    auto command = std::make_unique<Command>(id, text, *this);
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

void CommandManager::pushContext(const ContextId& context, const void* source, ContextTier tier)
{
    const RefKey key{context, source};
    int& refCount = m_refCounts[key];
    ++refCount;
    if (refCount > 1) {
        return; // 同一来源(context, source, tier)组合重复 push，只增加自身计数，总激活状态未发生变化
    }

    TierCounts& tierCounts        = m_contextTierCounts[context]; // 首次访问时值初始化为全 0
    const bool wasOverallInactive = !anyTierActive(tierCounts);
    ++tierCounts[static_cast<size_t>(tier)];

    // 全局首次激活
    if (wasOverallInactive) {
        // 只在"整体从未激活变为激活"时刷新时序戳——tier 只是同一整体激活状态下
        // 的"生效层级"计算依据，不应该因为多来一个不同 tier 的引用就被刷新，
        // 否则会削弱"层级比较严格优先于时序"这个仲裁语义的清晰度。
        m_activationOrder[context] = ++m_activationClock;
    }

    // 无条件广播：即使 context 整体早已激活，这次 push 也可能改变 effectiveTier()
    // 的返回值（比如之前只有 Background 层级的持有者，这次新增了 Interactive
    // 层级的持有者，生效层级从 Background 跃升为 Interactive），必须让所有
    // Command 都有机会重新仲裁。
    emit contextChanged();
}

void CommandManager::popContext(const ContextId& context, const void* source, ContextTier tier)
{
    const RefKey key{context, source};
    auto it = m_refCounts.find(key);
    if (it == m_refCounts.end() || it->second <= 0) {
        return; // 未持有该上下文(context, source, tier)组合引用，忽略非法/多余的 pop 调用
    }
    --(it->second);
    if (it->second > 0) {
        return;
    }
    m_refCounts.erase(it);

    auto tierIt = m_contextTierCounts.find(context);
    if (tierIt == m_contextTierCounts.end()) {
        return; // 理论上不会发生：能查到 ref counts 就必然有对应的 tierCounts 条目
    }
    TierCounts& tierCounts = tierIt->second;
    --tierCounts[static_cast<size_t>(tier)];

    if (!anyTierActive(tierCounts)) {
        // 整体失活，清理；m_activationOrder 的记录保留，
        // 下次重新激活时会被覆盖，不需要在这里主动清理
        m_contextTierCounts.erase(tierIt);
    }

    // 同 pushContext：无条件广播，因为这次 pop 也可能改变 effectiveTier() 的返回值
    // （即便 context 整体仍然激活——比如刚好是最后一个 Foreground 层级持有者
    // 释放了，生效层级从 Foreground 回落到 Background）。
    emit contextChanged();
}

void CommandManager::releaseContext(const void* source)
{
    // 先收集该 source 持有引用的全部 (context, tier)组合上下文，避免遍历过程中修改容器
    std::vector<std::pair<ContextId, ContextTier>> owned;
    for (const auto& [key, count] : m_refCounts) {
        if (key.source == source && count > 0) {
            owned.emplace_back(key.context, key.tier);
        }
    }
    for (const auto& [ctx, tier] : owned) {
        auto it = m_refCounts.find(RefKey{ctx, source, tier});
        while (it != m_refCounts.end() && it->second > 0) {
            popContext(ctx, source, tier);
            it = m_refCounts.find(RefKey{ctx, source, tier});
        }
    }
}

std::unordered_set<ContextId> CommandManager::activeContexts()
{
    std::unordered_set<ContextId> result;
    for (const auto& [ctx, counts] : m_contextTierCounts) {
        if (anyTierActive(counts)) {
            result.insert(ctx);
        }
    }
    return result;
}

bool CommandManager::isActiveContext(const ContextId& context) const noexcept
{
    auto it = m_contextTierCounts.find(context);
    return it != m_contextTierCounts.end() && anyTierActive(it->second);
}

ContextTier CommandManager::effectiveTier(const ContextId& context) const noexcept
{
    auto it = m_contextTierCounts.find(context);
    if (it == m_contextTierCounts.end()) {
        // 未激活时的安全默认值，调用方应先用 isActiveContext() 判断
        return ContextTier::Foreground;
    }
    const TierCounts& counts = it->second;
    for (int t = static_cast<int>(TierCount) - 1; t >= 0; --t) {
        if (counts[static_cast<size_t>(t)] > 0) {
            return static_cast<ContextTier>(t);
        }
    }
    return ContextTier::Foreground; // 理论上不会走到这里：isActive() 为真必然有某个 tier > 0
}

uint64_t CommandManager::activationOrder(const ContextId& context) const noexcept
{
    if (!isActiveContext(context)) {
        return 0; // 当前未激活，不参与仲裁
    }
    auto it = m_activationOrder.find(context);
    return it != m_activationOrder.end() ? it->second : 0;
}

bool CommandManager::anyTierActive(const TierCounts& counts) noexcept
{
    return std::ranges::any_of(counts, [](int i) { return i > 0; });
}

} // namespace bakuon::gui
