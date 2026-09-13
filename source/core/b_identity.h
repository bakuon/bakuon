
#pragma once

#include <cstdint>

#include "core/b_handle.h"
#include "core/b_registry.h"

namespace bakuon::core {

/**
 * @brief 稳定身份：跨销毁/回收、跨保存、跨进程消息仍然有效的主键。
 *
 * Handle 不能当主键（entt 会复用 handle id）。需要进撤销栈、会话文件、
 * 沙箱 RPC 的引用，一律存 StableId::value，再用 identity::find() 解回 Handle。
 * 0 是哨兵（"未分配"），合法 id 从 1 起。分配与查找见 b_identity.h。
 */
struct StableId
{
    std::uint64_t value = 0;

    [[nodiscard]] constexpr bool isValid() const noexcept { return value != 0; }

    friend constexpr bool operator==(StableId lhs, StableId rhs) noexcept
    {
        return lhs.value == rhs.value;
    }
    friend constexpr bool operator!=(StableId lhs, StableId rhs) noexcept { return !(lhs == rhs); }
};

namespace identity {
/**
 * @brief 保证 handle 带有 StableId：已有则原样返回，没有就 mint 一个并 emplace。
 * @return 无效 handle 时返回 StableId{0}。
 *
 * 第一次在某个 Registry 上调用 ensure/bind/mint 时会自动装好销毁钩子，
 * 之后 Registry::destroy() 会把索引摘干净，避免 Handle 回收后 find() 指到新实体。
 */
StableId ensure(Registry &registry, Handle handle);

/// 按稳定身份反查当前 Handle；未绑定或对应实体已销毁时返回无效 Handle。
[[nodiscard]] Handle find(const Registry &registry, StableId id);

/// 读实体当前的 StableId；尚未 ensure() 时返回 {0}。
[[nodiscard]] StableId get(const Registry &registry, Handle handle);
} // namespace identity

} // namespace bakuon::core

namespace std {
template<>
struct hash<bakuon::core::StableId>
{
    size_t operator()(bakuon::core::StableId id) const noexcept
    {
        return std::hash<std::uint64_t>{}(id.value);
    }
};
} // namespace std
