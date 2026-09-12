#include "core/b_identity.h"

#include <unordered_map>

#include "core/b_connection.h"

namespace bakuon::core {

namespace detail {
/// 存在 registry.ctx() 里的索引。
struct Index
{
    std::uint64_t next = 1; // 是否需要原子操作
    std::unordered_map<std::uint64_t, Handle> values;
    std::unordered_map<Handle, std::uint64_t> entities;
    bool hooksInstalled = false;
};

inline Index& indexOf(Registry& registry)
{
    auto& ctx = registry.native().ctx();
    if (!ctx.contains<Index>()) {
        ctx.emplace<Index>();
    }
    return ctx.get<Index>();
}

inline const Index* indexOf(const Registry& registry)
{
    return registry.native().ctx().find<Index>();
}

/// 订阅 StableId 的构造/销毁，让 Registry::destroy() 不会留下指向回收 Handle 的脏索引。
inline void installHooks(Registry& registry)
{
    Index& index = indexOf(registry);
    if (index.hooksInstalled) {
        return;
    }
    index.hooksInstalled = true;

    registry
        .onConstruct<StableId>([](Registry& reg, Handle handle) {
            const StableId* id = reg.tryGet<StableId>(handle);
            if (id == nullptr || !id->isValid()) {
                return;
            }
            Index& idx            = indexOf(reg);
            idx.values[id->value] = handle;
            idx.entities[handle]  = id->value;
        })
        .dismiss();

    registry
        .onDestroy<StableId>([](Registry& reg, Handle handle) {
            Index& idx = indexOf(reg);
            auto it    = idx.entities.find(handle);
            if (it == idx.entities.end()) {
                return;
            }
            idx.values.erase(it->second);
            idx.entities.erase(it);
        })
        .dismiss();
}

/// 分配一个从未用过的 StableId
[[nodiscard]] inline StableId mint(Registry& registry)
{
    installHooks(registry);
    Index& index = indexOf(registry);
    return StableId{index.next++};
}

} // namespace detail

Identity::Identity(Registry& registry)
    : m_reg(registry)
{
}

StableId Identity::ensure(Handle handle)
{
    if (!m_reg.valid(handle)) {
        return {};
    }
    if (const StableId* existing = m_reg.tryGet<StableId>(handle)) {
        return *existing;
    }
    return m_reg.emplace<StableId>(handle, detail::mint(m_reg));
}

Handle Identity::find(StableId id) const
{
    if (!id.isValid()) {
        return {};
    }
    const detail::Index* index = detail::indexOf(static_cast<const Registry&>(m_reg));
    if (index == nullptr) {
        return {};
    }
    const auto it = index->values.find(id.value);
    if (it == index->values.end()) {
        return {};
    }
    return it->second;
}

StableId Identity::get(Handle handle) const
{
    const StableId* id = m_reg.tryGet<StableId>(handle);
    return id ? *id : StableId{};
}

} // namespace bakuon::core
