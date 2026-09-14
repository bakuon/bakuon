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

    // 首次安装时，Registry 里可能已经存在若干 StableId 组件——典型场景是刚从
    // DocumentSerializer::load() 或 UndoStack 的 undo()/redo() 里整体恢复出来
    // 的实体：这些组件的构造事件发生在本次 installHooks() 被调用之前，此刻
    // 才第一次挂上的钩子根本没机会捕捉到那些"已经发生过"的构造。只挂钩子、
    // 不回填索引，会让这些实体在索引里"查无此人"——先完整扫一遍现状，再挂
    // 钩子接管"从此刻起"的后续变化，两者合起来才能保证索引任何时候都完整。
    registry.template each<StableId>([&index](Handle handle, const StableId& id) {
        if (id.isValid()) {
            index.values[id.value] = handle;
            index.entities[handle] = id.value;
        }
    });

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

namespace identity {

StableId ensure(Registry& registry, Handle handle)
{
    if (!registry.valid(handle)) {
        return {};
    }
    if (const StableId* existing = registry.tryGet<StableId>(handle)) {
        return *existing;
    }
    return registry.emplace<StableId>(handle, detail::mint(registry));
}

Handle find(const Registry& registry, StableId id)
{
    if (!id.isValid()) {
        return {};
    }
    const detail::Index* index = detail::indexOf(registry);
    if (index == nullptr) {
        return {};
    }
    const auto it = index->values.find(id.value);
    if (it == index->values.end()) {
        return {};
    }
    return it->second;
}

StableId get(const Registry& registry, Handle handle)
{
    const StableId* id = registry.tryGet<StableId>(handle);
    return id ? *id : StableId{};
}

void sync(Registry& registry)
{
    detail::installHooks(registry);
}

} // namespace identity
} // namespace bakuon::core
