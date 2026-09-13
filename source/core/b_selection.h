#pragma once

#include "core/b_components.h"
#include "core/b_registry.h"

// ============================================================================
// 选择集：对 Selected 标签的一组显式操作。
//
// 不做成"当前选择存在 registry.ctx() 里的一份 vector"——那样会和标签组件
// 变成双份事实来源。选择就是"带 Selected 的实体"，查询走 each<Selected>()。
// ============================================================================

namespace bakuon::core::selection {

using namespace components;

/// 加入选择集；已选中则是空操作。无效 handle 被忽略。
inline void add(Registry& registry, Handle handle)
{
    if (!registry.valid(handle) || registry.has<Selected>(handle)) {
        return;
    }
    registry.emplace<Selected>(handle);
}

/// 移出选择集；未选中则是空操作。
inline void remove(Registry& registry, Handle handle)
{
    if (registry.valid(handle)) {
        registry.remove<Selected>(handle);
    }
}

/**
 * @brief 清空整个选择集。
 * @note 必须先收集再 remove：each() 遍历期间改同一类组件存储是未定义行为。
 */
inline void clear(Registry& registry)
{
    std::vector<Handle> selected;
    registry.each<Selected>([&](Handle handle) { selected.push_back(handle); });
    for (Handle handle : selected) {
        registry.remove<Selected>(handle);
    }
}

/// 只选这一个（先 clear 再 add）。无效 handle 时结果是"选择集被清空"。
inline void exclusive(Registry& registry, Handle handle)
{
    clear(registry);
    add(registry, handle);
}

/// 当前全部选中实体。顺序与 each<Selected>() 一致（稀疏集迭代序，不是用户选择序）。
[[nodiscard]] inline std::vector<Handle> all(Registry& registry)
{
    std::vector<Handle> selected;
    registry.each<Selected>([&](Handle handle) { selected.push_back(handle); });
    return selected;
}

} // namespace bakuon::core::selection
