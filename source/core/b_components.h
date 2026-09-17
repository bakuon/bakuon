#pragma once

#include <string>

#include <nlohmann/json.hpp>

#include "core/b_entity.h"
#include "core/b_serializer.h"

// ============================================================================
// 第一组"共享词汇"组件 —— 给文档/图层/节点/面板条目这类 GUI 领域对象用。
//
// 设计约束（和 Registry 类文档同一条原则）：
//  - 组件是普通 C++ 结构体，不继承、不带虚函数、不认识 Qt。
//  - 空结构体是标签组件（tag component）：只表达"有/无"，EnTT 对它们零存储。
//  - 插件可以、也应该定义自己的组件（滤镜栈、锁定范围……）；这里只放
//    **跨插件都会查询** 的那一小撮。新增一种稀疏属性 = 新增一个组件类型，
//  - 不要往 Name / Parent 上堆可选字段。
//  - 不要把 QWidget* / QAction* / QString 放进组件。字符串用 std::string；
//  - 持久化身份用 StableId，不要把 Handle 写入文件或跨进程消息
//    （Handle 底层是 entt::entity，销毁后会被回收，见 b_handle.h）。
// ============================================================================

namespace bakuon::core::components {

/**
 * @brief 一小组"开箱即用"的通用组件——文档/场景树类应用几乎总会用到的基础属性
 * （名称、可见性、锁定态、选中态），提前定义好、配好序列化支持，省得每个
 * 具体项目都要重新发明一遍这几个最基础的组件。
 *
 * @details 这些类型本身没有任何特殊地位——对 Registry 而言它们就是普通的
 * 组件类型，和调用方自己定义的业务组件完全平等（core 不会因为"内置"就给
 * 它们开小灶）。之所以放进 core 统一提供，纯粹是因为它们出现的频率高到
 * 值得被当作一份"标准词汇表"，避免不同项目/不同插件各自用不同的字段名
 * （比如有的叫 visible，有的叫 isVisible，有的叫 hidden 取反）表达同一个概念，
 * 导致互相之间无法直接复用彼此的 GUI 绑定代码（比如通用的"图层面板"widget）。
 *
 * ## 可平凡拷贝 vs 需要 JSON
 * Visible/Locked/Enabled/Selected 都是 std::is_trivially_copyable_v 的，可以
 * 直接用于 UndoStack<Components...>（b_undostack.h，逐字节内存快照）；
 * Name/Tag 内部持有 std::string，不满足这个约束，只能用于
 * DocumentSerializer<Components...>（b_serializer.h，JSON 归档）。这正是
 * b_undostack.h 类文档里提到的"持有堆内存的字段应该拆到不参与 undo 追踪的
 * 组件里"的一个具体示例——如果既想要名称可撤销、又不想为了这一个字符串字段
 * 放弃逐字节快照的性能，可以在业务层自己包一层"索引/id 而不是完整字符串"
 * 的可平凡拷贝组件，这里不越俎代庖替调用方做这个取舍。
 *
 * 全部类型都已经用 BAKUON_DECLARE_COMPONENT_NAME 声明好了稳定的序列化键名
 * （与类型同名的字符串），可以直接作为 DocumentSerializer<Components...> 的
 * 模板参数使用，不需要调用方自己再声明一遍。
 */

/// 人类可读的显示名称——图层面板/大纲视图最基本的一列。
/// 属性面板、树视图、状态栏都读它；改名走 Registry::patch<Name>() 以便触发 onUpdate
struct Name
{
    std::string value;
};

/// 是否在视觉上可见（图层面板的"眼睛"图标）。默认可见。
struct Visible
{
    bool value = true;
};

/// 是否禁止编辑/移动（图层面板的"锁"图标）。默认不锁定。
struct Locked
{
    bool value = false;
};

/// 是否参与业务逻辑（区别于 Visible——一个实体可以"看得见但被禁用"，
/// 或者"看不见但仍在参与计算"，两个维度刻意分开，不合并成一个字段）。
struct Enabled
{
    bool value = true;
};

/// 是否处于（多选）选中状态——空结构体标签组件，只表达"有/无"，不携带数据。
/// 空标签：用 has<Selected>() / each<Selected>() 查询，不要做成 bool 字段。
struct Selected
{
};

/// 相对上次保存/提交已修改。保存成功后 remove<Dirty>() 即可。
struct Dirty
{
};

/// 用户自定义的自由文本分类标签（区别于 C++ 类型系统层面的"组件类型"这个
/// 概念——这是给最终用户在 GUI 里自己打的、任意字符串形式的标记，比如给
/// 一批图层统一打上 "背景" 标签，方便后续按标签筛选/批量操作）。
struct Tag
{
    std::string value;
};

/**
 * @brief 父节点。缺席 = 根级实体。
 *
 * @warning 不要直接 emplace/remove 这个组件来改树结构——那样 Children 另一侧
 *          会立刻失去同步。一律走 hierarchy::setParent() / detach()
 *          （见 b_hierarchy.h）。
 */
struct Parent
{
    Entity entity{nullentity};
};

/**
 * @brief 有序子节点列表。view/each 本身无序，同级顺序只存在这份向量里。
 *
 * 空容器仍然可以挂在"文件夹"实体上（表示这是一个容器，只是还没有孩子）。
 * 维护规则与 Parent 相同：只通过 hierarchy:: 下的函数改。
 */
struct Children
{
    std::vector<Entity> entities;
};

inline void to_json(nlohmann::json& j, const Name& v)
{
    j = {{"value", v.value}};
}
inline void from_json(const nlohmann::json& j, Name& v)
{
    j.at("value").get_to(v.value);
}

inline void to_json(nlohmann::json& j, const Visible& v)
{
    j = {{"value", v.value}};
}
inline void from_json(const nlohmann::json& j, Visible& v)
{
    j.at("value").get_to(v.value);
}

inline void to_json(nlohmann::json& j, const Locked& v)
{
    j = {{"value", v.value}};
}
inline void from_json(const nlohmann::json& j, Locked& v)
{
    j.at("value").get_to(v.value);
}

inline void to_json(nlohmann::json& j, const Enabled& v)
{
    j = {{"value", v.value}};
}
inline void from_json(const nlohmann::json& j, Enabled& v)
{
    j.at("value").get_to(v.value);
}

/// Selected 是空结构体标签组件：entt 的空类型优化意味着序列化时根本不会有
/// "值"需要写入/读出（见 Registry::each()、tests/core/serializer_test.cpp 里
/// 对同一现象的说明），这两个重载的存在只是为了满足 nlohmann::json /
/// DocumentSerializer 在类型层面对 to_json/from_json 的静态要求。
inline void to_json(nlohmann::json& j, const Selected&)
{
    j = nlohmann::json::object();
}
inline void from_json(const nlohmann::json&, Selected&)
{
}

inline void to_json(nlohmann::json& j, const Tag& v)
{
    j = {{"value", v.value}};
}
inline void from_json(const nlohmann::json& j, Tag& v)
{
    j.at("value").get_to(v.value);
}

} // namespace bakuon::core::components

BAKUON_DECLARE_COMPONENT_NAME(bakuon::core::components::Name, "Name")
BAKUON_DECLARE_COMPONENT_NAME(bakuon::core::components::Visible, "Visible")
BAKUON_DECLARE_COMPONENT_NAME(bakuon::core::components::Locked, "Locked")
BAKUON_DECLARE_COMPONENT_NAME(bakuon::core::components::Enabled, "Enabled")
BAKUON_DECLARE_COMPONENT_NAME(bakuon::core::components::Selected, "Selected")
BAKUON_DECLARE_COMPONENT_NAME(bakuon::core::components::Tag, "Tag")
