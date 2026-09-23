#include "core/command/b_command.h"

namespace bakuon::core::command {

CommandRegistry::CommandRegistry(Registry& registry)
    : m_registry(registry)
{
    m_constructConn = registry.on_construct<Command>().connect<&CommandRegistry::constructed>(*this);
    m_destroyConn = registry.on_destroy<Command>().connect<&CommandRegistry::destroyed>(*this);
}

CommandRegistry::~CommandRegistry() = default;

Entity CommandRegistry::registerCommand(std::string_view id, std::string name)
{
    if (const Entity existing = find(id); m_registry.valid(existing)) {
        return existing; // 幂等：不覆盖已有配置
    }

    const Entity entity = m_registry.create();
    // emplace<Command> 同步触发 on_construct -> constructed()，索引在这里建立，
    // 不需要在本函数里手写 m_idToEntity[...] = entity 那一行（否则两处各写一次，
    // 迟早会漂移，见 Identifier::ensure() 同样的取舍）。
    m_registry.emplace<Command>(entity, std::string(id));
    m_registry.emplace<CommandName>(entity, std::move(name));
    m_registry.emplace<CommandAttributes>(entity);
    return entity;
}

void CommandRegistry::unregisterCommand(std::string_view id)
{
    if (const Entity entity = find(id); m_registry.valid(entity)) {
        m_registry.destroy(entity); // 触发 on_destroy -> destroyed()，索引在这里摘除
    }
}

Entity CommandRegistry::find(std::string_view id) const noexcept
{
    const auto it = m_idToEntity.find(std::string(id));
    return it != m_idToEntity.end() ? it->second : nullentity;
}

bool CommandRegistry::contains(std::string_view id) const noexcept
{
    return m_idToEntity.find(std::string(id)) != m_idToEntity.end();
}

std::vector<Entity> CommandRegistry::all() const
{
    std::vector<Entity> result;
    result.reserve(m_idToEntity.size());
    for (const auto& [id, entity] : m_idToEntity) {
        result.push_back(entity);
    }
    return result;
}

void CommandRegistry::constructed(Registry& registry, Entity entity)
{
    if (const auto* cmd = registry.try_get<Command>(entity)) {
        m_idToEntity[cmd->id] = entity;
    }
}

void CommandRegistry::destroyed(Registry& /*registry*/, Entity entity)
{
    // 表不大（命令数量是"几十到一百量级"，见 CommandManager::renderMenuBar 注释里
    // 的同一个数量级假设），线性扫描摘除比额外维护一张反向 Entity->id 表更简单，
    // 出错空间也更小。
    for (auto it = m_idToEntity.begin(); it != m_idToEntity.end(); ++it) {
        if (it->second == entity) {
            m_idToEntity.erase(it);
            break;
        }
    }
}

} // namespace bakuon::core::command
