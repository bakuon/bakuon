
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
StableId ensure(Registry &registry, Handle handle); // acquire

/// 按稳定身份反查当前 Handle；未绑定或对应实体已销毁时返回无效 Handle。
[[nodiscard]] Handle find(const Registry &registry, StableId id);

/// 读实体当前的 StableId；尚未 ensure() 时返回 {0}。
[[nodiscard]] StableId get(const Registry &registry, Handle handle);

/**
 * @brief 显式让索引与 Registry 当前实际持有的全部 StableId 组件保持一致。
 *
 * @details 装钩子这件事本身只能感知"从安装那一刻起"发生的构造/销毁事件——
 * 如果一批带着 StableId 的实体是在钩子安装 *之前* 就已经进了 Registry
 * （最典型的场景：DocumentSerializer::load()（b_serializer.h）把一份文档
 * 整体载入一个此前从未被任何 identity:: 函数碰过的 Registry；这种 Registry
 * 上钩子还没装过，因为 ensure()/mint() 从来没被调用过），装钩子这一步本身
 * 完全没有机会"回头看"那些已经落地的组件，find() 因此会一直查无此人，
 * 哪怕对应的 StableId 组件确确实实已经存在于 Registry 里。
 *
 * 调用本函数会（在第一次调用时）先完整扫描一遍当前所有 StableId 组件重建
 * 索引，再挂上钩子；对已经装过钩子的 Registry 重复调用是安全的空操作
 * （这种情况下索引本来就是靠钩子持续保持同步的，不需要重新扫描）。
 *
 * @note UndoStack（b_undostack.h）/ DocumentSerializer 的 load()/undo()/redo()
 * 走的是"clear() 再原地 reload"，只要钩子在那之前已经装好过，就能全程
 * 自动保持索引同步、不需要调用本函数（见对应测试用例）——只有"这个 Registry
 * 从一开始就是靠批量载入获得初始内容，从未调用过任何 identity:: 函数"这一种
 * 场景才需要显式调用一次 sync()。
 */
void sync(Registry &registry);
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
