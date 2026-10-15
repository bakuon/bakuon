#pragma once

#include <concepts>
#include <cstddef>
#include <functional>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

#include "core/b_components.h"
#include "core/b_entity.h"
#include "core/b_result.h"

namespace bakuon::core {

namespace tagger_detail {

/// 透明哈希：配合 std::equal_to<> 让 find()/contains() 直接用 string_view 查表，
/// 命中路径不需要构造临时 std::string（与 gui::Id 的驻留表同一思路）。
struct string_hash
{
    using is_transparent = void;
    [[nodiscard]] std::size_t operator()(std::string_view s) const noexcept
    {
        return std::hash<std::string_view>{}(s);
    }
};

} // namespace tagger_detail

/**
 * @brief 可作为"身份标签"的组件：持有一个 `std::string value` 字段，且可用 `T{std::string}` 构造。
 * @note components::Tag 满足；业务侧自定义的 `struct CommandKey { std::string value; };` 同样满足。
 */
template<typename T>
concept TagComponent = requires(T &t) {
    requires std::same_as<std::remove_cvref_t<decltype(t.value)>, std::string>;
    T{std::string{}};
};

/**
 * @brief 字符串标签 ↔ 实体 的唯一性注册表（一个标签最多对应一个实体）。
 *
 * ## 定位
 * CommandId / ContextId / 扩展点 id / 窗口 id 这类"字符串 id"在 ECS 视角下都是
 * "给一个实例打上唯一名字"。register / unregister 因此统一为：创建（或认领）一个
 * 实体、给它挂上身份组件；注销就是摘标签或销毁实体。业务语义由**业务侧再挂的
 * 种类组件（Kind，空结构体标签）**加固，本类不认识任何具体业务：
 * @code
 *   struct CommandKind {};   // 空组件，只表达"这是一条命令"
 *   struct ContextKind {};   // 空组件，只表达"这是一个上下文"
 *
 *   Tagger commands(registry);
 *   auto r = commands.create<CommandKind>("edit.delete");    // Result<Entity>
 *   registry.emplace<CommandText>(r.value(), "删除");        // 业务数据照常挂组件
 *   Entity e = commands.find<CommandKind>("edit.delete");    // 带种类校验的查找
 * @endcode
 *
 * ## 单一事实来源
 * 真相只有一份：实体上的 `Component` 身份组件。内部 `m_tags` 只是它的**索引**，
 * 完全由 on_construct / on_update / on_destroy 三个监听器维护——因此下列路径都
 * 不会让索引失步：本类自己的 create/assign/unset、`registry.destroy()`、
 * `registry.clear()` + 重新灌入（UndoStack::undo()/redo()、Serializer::load()）。
 *
 * ## 多个 Tagger 共存（id 空间隔离）
 * 一个实体只有一个 `Component`，所以"同一份 Component 类型"的两个 Tagger 会互相看到
 * 对方的实体。要隔离命令/上下文这类不同 id 空间，请用**不同的身份组件**：
 * `BasicTagger<CommandKey>` / `BasicTagger<ContextKey>`。每个 Tagger 只认自己的
 * 组件；tag()/unset() 对不属于自己索引的实体一律当作"没有标签"。
 *
 * @warning components::Tag 的原始语义是"用户自由文本分类标签"（可重复），与本类的
 *          "唯一身份"语义冲突。默认的 `Tagger = BasicTagger<components::Tag>` 仅为保持
 *          与旧接口兼容；承载 id 的场景请使用专用组件。
 *
 * ## 约束
 *  - 唯一性：同一标签只能对应一个实体；冲突通过 Result 的 AlreadyExists 报告，不静默覆盖。
 *  - 空串不是合法标签（InvalidArgument）。
 *  - **不要绕过本类直接 emplace / replace / patch 身份组件**。on_update 会按
 *    O(n) 扫描兜底修复索引，但无法否决重复值：外部写入造成的重名，先到先得，
 *    后者不进索引（可用 verify() 在测试/调试中检查）。
 *  - 非线程安全，只应在 Registry 所在线程使用（与 Container/Registry 一致）。
 *  - 不可拷贝、不可移动（监听器捕获 this）。必须先于 Registry 销毁
 *    （作为 Domain 服务时由 Domain 的逆序销毁保证，见 b_domain.h）。
 */
template<TagComponent Component = components::Tag>
class BasicTagger
{
public:
    using component_type = Component;

    /// ensure() 的返回值：实体 + 是否是本次新建的。
    struct Ensured
    {
        Entity entity{NullEntity};
        bool created{false};
    };

    explicit BasicTagger(Registry &registry)
        : m_registry(registry)
    {
        // 认领构造之前就已存在的身份组件（先到先得，重名者不进索引）。
        m_registry.template view<Component>().each(
            [this](Entity entity, const Component &c) { index(c.value, entity); });

        m_constructed = m_registry.template on_construct<Component>()
                            .template connect<&BasicTagger::constructed>(*this);
        m_updated     = m_registry.template on_update<Component>()
                            .template connect<&BasicTagger::updated>(*this);
        m_destroyed   = m_registry.template on_destroy<Component>()
                            .template connect<&BasicTagger::destroyed>(*this);
    }

    ~BasicTagger() = default; // ScopedConnection 成员自动断开监听

    BasicTagger(const BasicTagger &)            = delete;
    BasicTagger &operator=(const BasicTagger &) = delete;
    BasicTagger(BasicTagger &&)                 = delete;
    BasicTagger &operator=(BasicTagger &&)      = delete;

    // ------------------------------------------------------------------
    // 注册
    // ------------------------------------------------------------------

    /**
     * @brief 注册：创建一个新实体并打上标签，同时挂上 Kinds... 种类组件。
     * @tparam Kinds 业务种类组件（必须可默认构造，通常是空结构体）。
     * @return 新实体；标签为空返回 InvalidArgument，已被占用返回 AlreadyExists（不创建任何实体）。
     * @note Kinds 先于身份组件挂载：监听 on_construct<Component> 的观察者收到通知时，
     *       实体的种类组件已经就位。
     */
    template<typename... Kinds>
    [[nodiscard]] Result<Entity> create(std::string_view tag)
    {
        if (auto available = checkAvailable(tag, NullEntity); available.error()) {
            return Fail<Entity>(available.status());
        }
        const Entity entity = m_registry.create();
        (m_registry.template emplace<Kinds>(entity), ...);
        m_registry.template emplace<Component>(entity, Component{std::string(tag)});
        return Ok(entity);
    }

    /**
     * @brief 取或建：标签已存在则返回既有实体（并补齐缺失的 Kinds...），否则新建。
     * @note 对既有实体只"补缺"种类组件，绝不覆盖已有组件的值。
     */
    template<typename... Kinds>
    [[nodiscard]] Result<Ensured> ensure(std::string_view tag)
    {
        if (tag.empty()) {
            return Fail<Ensured>(StatusCode::InvalidArgument, "tag must not be empty");
        }
        if (const Entity existing = find(tag); m_registry.valid(existing)) {
            (ensureKind<Kinds>(existing), ...);
            return Ok(Ensured{existing, false});
        }
        // 在 create 内部 checkAvailable 的实现逻辑与前面的一样，不再调用 create 做重复检测：
        // auto created = create<Kinds...>(tag);
        // if (created.error()) {
        //     return Fail<Ensured>(created.status());
        // }
        const Entity entity = m_registry.create();
        (m_registry.template emplace<Kinds>(entity), ...);
        m_registry.template emplace<Component>(entity, Component{std::string(tag)});
        return Ok(Ensured{entity, true});
    }

    /**
     * @brief 给已有实体设置/更改标签（改名）。
     * @return 实体无效或标签为空 → InvalidArgument；标签被**另一个**实体占用 → AlreadyExists。
     *         标签已经是该实体自己的 → Ok（幂等）。失败时不产生任何副作用。
     */
    [[nodiscard]] Result<void> assign(Entity entity, std::string_view tag)
    {
        if (!m_registry.valid(entity)) {
            return Fail<void>(StatusCode::InvalidArgument, "assign: invalid entity");
        }
        if (auto available = checkAvailable(tag, entity); available.error()) {
            return available;
        }

        if (Component *current = m_registry.template try_get<Component>(entity)) {
            if (current->value == tag) {
                return Ok();
            }
            // 先摘旧键，再改值；on_update 里只需要插入新键（见 m_selfMutation）。
            eraseIfOwned(current->value, entity);
            m_selfMutation = true;
            m_registry.template patch<Component>(entity,
                                                 [&](Component &c) { c.value = std::string(tag); });
            m_selfMutation = false;
        } else {
            m_registry.template emplace<Component>(entity, Component{std::string(tag)});
        }
        return Ok();
    }

    // ------------------------------------------------------------------
    // 注销
    // ------------------------------------------------------------------

    /// 只摘标签、保留实体（及其业务组件）。实体不属于本 Tagger 时返回 false。
    bool unset(Entity entity)
    {
        if (!tagged(entity)) {
            return false;
        }
        m_registry.template remove<Component>(entity); // on_destroy 自动清索引
        return true;
    }

    bool unset(std::string_view tag)
    {
        const Entity entity = find(tag);
        return m_registry.valid(entity) && unset(entity);
    }

    /// 注销并销毁实体（连同全部组件）。标签不存在返回 false。
    bool destroy(std::string_view tag)
    {
        const Entity entity = find(tag);
        if (!m_registry.valid(entity)) {
            return false;
        }
        m_registry.destroy(entity);
        return true;
    }

    /// 销毁本 Tagger 持有的全部实体（典型：插件卸载时批量注销）。
    void clear()
    {
        for (const Entity entity : entities()) { // entities() 是快照，销毁中改索引是安全的
            if (m_registry.valid(entity)) {
                m_registry.destroy(entity);
            }
        }
    }

    // ------------------------------------------------------------------
    // 查询
    // ------------------------------------------------------------------

    /// 正向：标签 → 实体，O(1)，无堆分配。未找到返回 nullentity。
    [[nodiscard]] Entity find(std::string_view tag) const
    {
        const auto it = m_tags.find(tag);
        return it != m_tags.end() ? it->second : Entity{NullEntity};
    }

    /// 带种类校验的正向查找：实体存在且同时携带 Kind, More... 才返回，否则 nullentity。
    template<typename Kind, typename... More>
    [[nodiscard]] Entity find(std::string_view tag) const
    {
        const Entity entity = find(tag);
        return (m_registry.valid(entity) && m_registry.template all_of<Kind, More...>(entity))
                   ? entity
                   : Entity{NullEntity};
    }

    /**
     * @brief 反向：实体 → 标签，O(1)。不属于本 Tagger 的实体返回空视图。
     * @note 返回的视图指向索引节点里的键，在该标签被改名/注销之前一直有效
     *       （unordered_map 节点地址稳定），不会因组件池重排而悬空。
     */
    [[nodiscard]] std::string_view tag(Entity entity) const
    {
        if (!m_registry.valid(entity)) {
            return {};
        }
        const Component *c = m_registry.template try_get<Component>(entity);
        if (!c) {
            return {};
        }
        const auto it = m_tags.find(std::string_view(c->value));
        return (it != m_tags.end() && it->second == entity) ? std::string_view(it->first)
                                                            : std::string_view{};
    }

    [[nodiscard]] bool tagged(Entity entity) const { return !tag(entity).empty(); }
    [[nodiscard]] bool owned(Entity entity) const
    {
        return m_registry.template all_of<Component>(entity);
    }
    [[nodiscard]] bool contains(std::string_view tag) const noexcept
    {
        return m_tags.find(tag) != m_tags.end();
    }
    [[nodiscard]] std::size_t size() const noexcept { return m_tags.size(); }
    [[nodiscard]] bool empty() const noexcept { return m_tags.empty(); }

    /// 快照（顺序不保证）。拿到之后再遍历/销毁，不会因为回调里改索引而失效。
    [[nodiscard]] std::vector<Entity> entities() const
    {
        std::vector<Entity> result;
        result.reserve(m_tags.size());
        for (const auto &[tag, entity] : m_tags) {
            result.push_back(entity);
        }
        return result;
    }

    [[nodiscard]] std::vector<std::string> tags() const
    {
        std::vector<std::string> result;
        result.reserve(m_tags.size());
        for (const auto &[tag, entity] : m_tags) {
            result.push_back(tag);
        }
        return result;
    }

    /**
     * @brief 遍历（可选按种类过滤）：func 签名 `void(std::string_view tag, Entity)`。
     * @note 先快照再回调，回调里 create/assign/unset/destroy 都是安全的；
     *       回调期间已被销毁的实体会被跳过。
     */
    template<typename... Kinds, typename Func>
    void each(Func &&func) const
    {
        const std::vector<std::pair<std::string, Entity>> snapshot(m_tags.begin(), m_tags.end());
        for (const auto &[tag, entity] : snapshot) {
            if (m_registry.valid(entity) && m_registry.template all_of<Kinds...>(entity)) {
                std::invoke(func, std::string_view(tag), entity);
            }
        }
    }

    /// 一致性自检（测试/调试用）：索引里的每一项都指向存活实体，且其组件值与键一致。
    [[nodiscard]] bool verify() const
    {
        for (const auto &[tag, entity] : m_tags) {
            if (!m_registry.valid(entity)) {
                return false;
            }
            const Component *c = m_registry.template try_get<Component>(entity);
            if (!c || c->value != tag) {
                return false;
            }
        }
        return true;
    }

private:
    [[nodiscard]] Result<void> checkAvailable(std::string_view tag, Entity self) const
    {
        if (tag.empty()) {
            return Fail<void>(StatusCode::InvalidArgument, "tag must not be empty");
        }
        if (const Entity holder = find(tag); m_registry.valid(holder) && holder != self) {
            return Fail<void>(StatusCode::AlreadyExists,
                              "tag already registered: " + std::string(tag));
        }
        return Ok();
    }

    template<typename Kind>
    void ensureKind(Entity entity)
    {
        if (!m_registry.template all_of<Kind>(entity)) {
            m_registry.template emplace<Kind>(entity);
        }
    }

    /// 先到先得：键已被别的实体占用则不覆盖。
    void index(const std::string &value, Entity entity)
    {
        if (!value.empty()) {
            m_tags.try_emplace(value, entity);
        }
    }

    /// 只摘"确实指向该实体"的键，避免误删别的实体的索引。
    void eraseIfOwned(std::string_view value, Entity entity)
    {
        if (const auto it = m_tags.find(value); it != m_tags.end() && it->second == entity) {
            m_tags.erase(it);
        }
    }

    // ---- entt 监听器：索引的唯一维护者 ----

    void constructed(Registry &registry, Entity entity)
    {
        index(registry.get<Component>(entity).value, entity);
    }

    void updated(Registry &registry, Entity entity)
    {
        const std::string &value = registry.get<Component>(entity).value;
        if (!m_selfMutation) {
            // 外部直接改了组件：on_update 拿不到旧值，只能按实体扫描摘掉过期键（O(n)，罕见路径）。
            for (auto it = m_tags.begin(); it != m_tags.end();) {
                if (it->second == entity && it->first != value) {
                    it = m_tags.erase(it);
                } else {
                    ++it;
                }
            }
        }
        index(value, entity);
    }

    void destroyed(Registry &registry, Entity entity)
    {
        // on_destroy 触发时组件仍在，可读到最后一个值。
        eraseIfOwned(registry.get<Component>(entity).value, entity);
    }

private:
    Registry &m_registry;
    std::unordered_map<std::string, Entity, tagger_detail::string_hash, std::equal_to<>> m_tags;
    bool m_selfMutation = false; // assign() 内部改名期间为 true，让 updated() 跳过 O(n) 扫描

    // 声明在索引之后：析构时先断开监听，再销毁索引。
    ScopedConnection m_constructed;
    ScopedConnection m_updated;
    ScopedConnection m_destroyed;
};

/// 默认实例：以 components::Tag 为身份组件。承载 id 的场景请用专用组件特化 BasicTagger。
using Tagger = BasicTagger<>;

} // namespace bakuon::core
