#include "core/command/b_commandcontext.h"

namespace bakuon::core::command {

ContextRegistry::ContextRegistry(Registry& registry, CommandRegistry& commands)
    : m_registry(registry)
    , m_commands(commands)
{
}

ContextRegistry::~ContextRegistry() = default;

Entity ContextRegistry::registerContext(std::string_view id, std::string owner,
                                        std::string description, int priority)
{
    if (const Entity existing = find(id); m_registry.valid(existing)) {
        return existing;
    }

    const Entity entity = m_registry.create();
    m_registry.emplace<CommandContext>(entity,
                                       std::string(id),
                                       std::move(owner),
                                       std::move(description));
    m_registry.emplace<ContextPriority>(entity, priority);
    m_registry.emplace<ContextActivationState>(entity);
    m_registry.emplace<ContextCommands>(entity);
    m_idToEntity[std::string(id)] = entity;
    return entity;
}

Entity ContextRegistry::find(std::string_view id) const noexcept
{
    const auto it = m_idToEntity.find(std::string(id));
    return it != m_idToEntity.end() ? it->second : nullentity;
}

bool ContextRegistry::contains(std::string_view id) const noexcept
{
    return m_idToEntity.find(std::string(id)) != m_idToEntity.end();
}

std::vector<Entity> ContextRegistry::all() const
{
    std::vector<Entity> result;
    result.reserve(m_idToEntity.size());
    for (const auto& [id, entity] : m_idToEntity) {
        result.push_back(entity);
    }
    return result;
}

void ContextRegistry::bindCommand(Entity context, std::string_view commandId)
{
    if (!m_registry.valid(context) || !m_registry.all_of<ContextCommands>(context)) {
        return;
    }
    m_registry.get<ContextCommands>(context).commandIds.emplace(commandId);
    m_commandToContexts[std::string(commandId)].insert(context);
}

void ContextRegistry::unbindCommand(Entity context, std::string_view commandId)
{
    if (m_registry.valid(context) && m_registry.all_of<ContextCommands>(context)) {
        m_registry.get<ContextCommands>(context).commandIds.erase(std::string(commandId));
    }
    if (auto it = m_commandToContexts.find(std::string(commandId));
        it != m_commandToContexts.end()) {
        it->second.erase(context);
    }
}

void ContextRegistry::pushContext(Entity context, const void* source, ContextTier tier)
{
    if (!m_registry.valid(context) || !m_registry.all_of<ContextActivationState>(context)) {
        return;
    }
    auto& state = m_registry.get<ContextActivationState>(context);
    if (state.activation.retain(source, tier)) {
        // 从未激活 -> 激活：分配本层单调序，语义与旧 ContextArbiter::pushContext() 一致。
        state.activation.setActivationOrder(m_clock.next(tier));
    }
    if (const auto* cmds = m_registry.try_get<ContextCommands>(context)) {
        refreshCommands(cmds->commandIds);
    }
}

void ContextRegistry::popContext(Entity context, const void* source, ContextTier tier)
{
    if (!m_registry.valid(context) || !m_registry.all_of<ContextActivationState>(context)) {
        return;
    }
    auto& state = m_registry.get<ContextActivationState>(context);
    state.activation.release(source, tier);
    if (const auto* cmds = m_registry.try_get<ContextCommands>(context)) {
        refreshCommands(cmds->commandIds);
    }
}

void ContextRegistry::releaseSource(const void* source)
{
    if (!source) {
        return;
    }
    std::unordered_set<std::string> dirty;
    m_registry.view<ContextActivationState, ContextCommands>().each(
        [&](Entity /*entity*/, ContextActivationState& state, const ContextCommands& cmds) {
            state.activation.releaseAll(source);
            dirty.insert(cmds.commandIds.begin(), cmds.commandIds.end());
        });
    refreshCommands(dirty);
}

bool ContextRegistry::isActive(Entity context) const noexcept
{
    return m_registry.valid(context) && m_registry.all_of<ContextActivationState>(context)
           && m_registry.get<ContextActivationState>(context).activation.isActive();
}

ContextTier ContextRegistry::effectiveTier(Entity context) const noexcept
{
    if (!m_registry.valid(context) || !m_registry.all_of<ContextActivationState>(context)) {
        return ContextTier::Foreground;
    }
    return m_registry.get<ContextActivationState>(context).activation.effectiveTier();
}

std::uint64_t ContextRegistry::activationOrder(Entity context) const noexcept
{
    if (!m_registry.valid(context) || !m_registry.all_of<ContextActivationState>(context)) {
        return 0;
    }
    return m_registry.get<ContextActivationState>(context).activation.activationOrder();
}

Entity ContextRegistry::findActiveContext(std::string_view commandId) const
{
    Entity best = nullentity;
    ContextArbitrationKey bestKey{};
    bool hasBest = false;

    const auto it = m_commandToContexts.find(std::string(commandId));
    if (it == m_commandToContexts.end()) {
        return nullentity;
    }

    for (Entity context : it->second) {
        if (!isActive(context)) {
            continue;
        }
        const int priority = m_registry.get<ContextPriority>(context).value;
        const ContextArbitrationKey key{effectiveTier(context), priority, activationOrder(context)};
        if (beats(key, bestKey, hasBest)) {
            best    = context;
            bestKey = key;
            hasBest = true;
        }
    }
    return best;
}

Entity ContextRegistry::arbitrate(std::string_view commandId)
{
    const Entity winner = findActiveContext(commandId);

    if (const Entity command = m_commands.find(commandId); m_registry.valid(command)) {
        // emplace_or_replace：不管命令之前有没有 ActiveContext 组件都能写入，
        // 且总是触发 on_construct 或 on_update——GUI 侧只需要订阅这两个信号
        // 中的一个组合，不需要区分"第一次仲裁"和"重新仲裁"。
        m_registry.emplace_or_replace<ActiveContext>(command, winner);
    }
    return winner;
}

void ContextRegistry::refreshCommands(const std::unordered_set<std::string>& commandIds)
{
    for (const auto& id : commandIds) {
        arbitrate(id);
    }
}

} // namespace bakuon::core::command
