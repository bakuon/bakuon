#pragma once

#include <functional>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

#include "core/b_entity.h"
#include "core/b_stableid.h"

namespace bakuon::core {

/// old StableId -> new (cloned) StableId, scoped to a single batch-clone call.
using StableIdRemap = std::unordered_map<StableId, StableId>;

/** 
 * @brief 类型擦除后的描述，用于克隆一个组件类型，
 *        以及一个可选的“修复”步骤，用于重写嵌入其中的 StableId 字段。
 * @note  切勿在此处注册 bakuon::core::StableId 本身—
 *        —标识由 BatchCloner 的前瞻/剥离阶段显式处理，而不是由通
 *        用的组件级克隆循环处理。
 */
struct ComponentEntry
{
    using Clone       = std::function<void(const Registry&, Entity, Registry&, Entity)>;
    using Remap       = std::function<void(Registry&, Entity, const StableIdRemap&)>;
    using Serialize   = std::function<std::vector<std::byte>(const Registry&, Entity)>;
    using Deserialize = std::function<void(Registry&, Entity, std::span<const std::byte>)>;

    std::uint32_t type{};
    std::string name; // diagnostic only
    bool snapshotEnabled{false};
    Clone clone;
    Remap remap; // may be empty: most components hold no cross-entity references
    Serialize serialize;
    Deserialize deserialize;
};

class ComponentMapper
{
public:
    struct TypeInfo
    {
        std::uint32_t type{};
        std::string name;
        bool snapshotEnabled{false};
    };

    struct Blob
    {
        std::uint32_t type{};
        std::string type_name;
        std::vector<std::byte> bytes;
    };

    ComponentMapper() = default;

    template<typename Component>
    void addMapping(std::string name = {})
    {
        static_assert(std::is_copy_constructible_v<Component> || std::is_empty_v<Component>,
                      "Component must be copy-constructible to participate in cloning");

        ComponentEntry entry;
        entry.type  = typeOf<Component>();
        entry.name  = name.empty() ? std::string{typeName<Component>()} : std::move(name);
        entry.clone = &cloner<Component>;

        if constexpr (std::is_empty_v<Component> || std::is_trivially_copyable_v<Component>) {
            entry.serialize   = &serialize_trivial<Component>;
            entry.deserialize = &deserialize_trivial<Component>;
        }

        entry.snapshotEnabled = true;
        m_entries[entry.type] = std::move(entry);
    }

    template<typename Component>
    void addMapping(std::function<std::vector<std::byte>(const Component&)> serialize,
                    std::function<Component(std::span<const std::byte>)> deserialize)
    {
        static_assert(std::is_copy_constructible_v<Component> || std::is_empty_v<Component>,
                      "Component must be copy-constructible to participate in cloning");
        ComponentEntry entry;
        entry.type      = typeOf<Component>();
        entry.name      = std::string{ComponentName<Component>().value()};
        entry.clone     = &cloner<Component>;
        entry.serialize = [serialize](const Registry& registry, Entity entity) {
            if (!registry.template all_of<Component>(entity)) {
                return std::vector<std::byte>{};
            }
            if constexpr (std::is_empty_v<Component>) {
                return std::vector<std::byte>{};
            } else {
                return serialize(registry.get<Component>(entity));
            }
        };
        entry.deserialize =
            [deserialize](Registry& registry, Entity entity, std::span<const std::byte> bytes) {
                if constexpr (std::is_empty_v<Component>) {
                    if (!registry.all_of<Component>(entity)) {
                        registry.emplace<Component>(entity);
                    }
                } else {
                    auto value = deserialize(bytes);
                    if (registry.all_of<Component>(entity)) {
                        registry.replace<Component>(entity, std::move(value));
                    } else {
                        registry.emplace<Component>(entity, std::move(value));
                    }
                }
            };
        entry.snapshotEnabled = true;
        m_entries[entry.type] = std::move(entry);
    }

    template<typename Component>
    void removeMapping()
    {
        const auto id = typeOf<Component>();
        auto it       = m_entries.find(id);
        if (it == m_entries.end()) {
            return;
        }
        m_entries.erase(it);
    }

    /// Relationship fix-up after a batch clone. `fn` sees the *clone's* component.
    /// Fn signature: `void fn(Component& component, const StableIdRemap& remap)`
    template<typename Component, typename Fn>
    void setRemapper(Fn fn)
    {
        const auto id = typeOf<Component>();
        auto it       = m_entries.find(id);
        if (it == m_entries.end()) {
            add<Component>();
            it = m_entries.find(id);
        }
        it->second.remap = [fn](Registry& registry, Entity entity, const StableIdRemap& remap) {
            if (!registry.template all_of<Component>(entity)) {
                return;
            }
            if constexpr (std::is_empty_v<Component>) {
                (void) fn;
                (void) remap;
            } else {
                fn(registry.template get<Component>(entity), remap);
            }
        };
    }

    void remap(Registry& registry, Entity entity, const StableIdRemap& remap) const
    {
        for (const auto& [id, entry] : m_entries) {
            if (entry.remap) {
                entry.remap(registry, entity, remap);
            }
        }
    }

    void clone(const Registry& src, Entity from, Registry& dst, Entity to,
               std::span<const std::uint32_t> exclude = {}) const
    {
        for (const auto& [id, entry] : m_entries) {
            if (containsId(exclude, id)) {
                continue;
            }
            entry.clone(src, from, dst, to);
        }
    }

    // TODO: 使用内置的 DocumentSerializer 类型来实现序列化功能
    [[nodiscard]] std::vector<Blob> serialize(const Registry& registry, Entity entity,
                                              std::span<const std::uint32_t> exclude = {}) const
    {
        std::vector<Blob> blobs;
        blobs.reserve(m_entries.size());
        for (const auto& [id, entry] : m_entries) {
            if (!entry.snapshotEnabled || !entry.serialize) {
                continue;
            }
            if (containsId(exclude, id)) {
                continue;
            }
            if (!containsType(registry, entity, id)) {
                continue;
            }
            Blob blob;
            blob.type      = id;
            blob.type_name = entry.name;
            blob.bytes     = entry.serialize(registry, entity);
            blobs.push_back(std::move(blob));
        }
        return blobs;
    }

    // TODO: 使用内置的 DocumentSerializer 类型来实现序列化功能
    void deserialize(Registry& registry, Entity entity, std::span<const Blob> blobs) const
    {
        for (const auto& blob : blobs) {
            auto it = m_entries.find(blob.type);
            if (it == m_entries.end() || !it->second.deserialize) {
                continue;
            }
            it->second.deserialize(registry, entity, blob.bytes);
        }
    }

    [[nodiscard]] bool contains(std::uint32_t typeId) const noexcept
    {
        return m_entries.find(typeId) != m_entries.end();
    }

    [[nodiscard]] std::size_t size() const noexcept { return m_entries.size(); }

    template<typename Component>
    [[nodiscard]] static constexpr std::uint32_t typeOf() noexcept
    {
        return typeHash<Component>();
    }

    [[nodiscard]] static constexpr std::uint32_t stableidType() noexcept
    {
        return typeHash<StableId>();
    }

    [[nodiscard]] std::vector<TypeInfo> types() const
    {
        std::vector<TypeInfo> out;
        out.reserve(m_entries.size());
        for (const auto& [id, entry] : m_entries) {
            out.push_back(TypeInfo{id, entry.name, entry.snapshotEnabled});
        }
        return out;
    }

    /// Test-only: resets the registry to empty.
    void clear();

private:
    static bool containsId(std::span<const std::uint32_t> ids, std::uint32_t needle) noexcept
    {
        return std::ranges::any_of(ids, [needle](std::uint32_t id) { return id == needle; });
    }

    static bool containsType(const Registry& registry, Entity entity, std::uint32_t type)
    {
        const auto* pool = registry.storage(type);
        return pool != nullptr && pool->contains(entity);
    }

    template<typename Component>
    static void cloner(const Registry& src, Entity from, Registry& dst, Entity to)
    {
        if (!src.all_of<Component>(from)) {
            return;
        }
        if constexpr (std::is_empty_v<Component>) {
            dst.template emplace_or_replace<Component>(to);
        } else {
            const auto& value = src.get<Component>(from);
            dst.template emplace_or_replace<Component>(to, value);
        }
    }

    template<typename Component>
    static std::vector<std::byte> serialize_trivial(const Registry& registry, Entity entity)
    {
        if (!registry.template all_of<Component>(entity)) {
            return {};
        }
        if constexpr (std::is_empty_v<Component>) {
            return {};
        } else {
            std::vector<std::byte> bytes(sizeof(Component));
            const auto& value = registry.template get<Component>(entity);
            std::memcpy(bytes.data(), std::addressof(value), sizeof(Component));
            return bytes;
        }
    }

    template<typename Component>
    static void deserialize_trivial(Registry& registry, Entity entity,
                                    std::span<const std::byte> bytes)
    {
        if constexpr (std::is_empty_v<Component>) {
            registry.template emplace_or_replace<Component>(entity);
        } else {
            if (bytes.size() != sizeof(Component)) {
                throw StableIdError("snapshot size mismatch for component");
            }
            Component value{};
            std::memcpy(std::addressof(value), bytes.data(), sizeof(Component));
            registry.template emplace_or_replace<Component>(entity, value);
        }
    }

    std::unordered_map<std::uint32_t, ComponentEntry> m_entries;
};

} // namespace bakuon::core
