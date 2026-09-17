#pragma once

#include <algorithm>
#include <functional>
#include <utility>
#include <vector>

#include "core/b_components.h"
#include "core/b_connection.h"
#include "core/b_entity.h"

// ============================================================================
// 选择集：Selected 标签为唯一真相源；Selection 类额外维护用户选择顺序与 primary。
//
// 不做成"当前选择存在 registry.ctx() 里的一份 vector"——那样会和标签组件
// 变成双份事实来源。选择就是"带 Selected 的实体"，查询走 each<Selected>()。
// Selection 的 ordered() 只是顺序视图，任何写操作必须同步标签。
// ============================================================================

namespace bakuon::core::selection {

using namespace components;

/// 加入选择集；已选中则是空操作。无效 entity 被忽略。
inline void add(Registry& registry, Entity entity)
{
    if (!registry.valid(entity) || registry.all_of<Selected>(entity)) {
        return;
    }
    registry.emplace<Selected>(entity);
}

/// 移出选择集；未选中则是空操作。
inline void remove(Registry& registry, Entity entity)
{
    if (registry.valid(entity)) {
        registry.remove<Selected>(entity);
    }
}

/**
 * @brief 清空整个选择集。
 * @note 必须先收集再 remove：each() 遍历期间改同一类组件存储是未定义行为。
 */
inline void clear(Registry& registry)
{
    std::vector<Entity> selected;
    registry.view<Selected>().each([&](Entity entity) { selected.push_back(entity); });
    for (Entity entity : selected) {
        registry.remove<Selected>(entity);
    }
}

/// 只选这一个（先 clear 再 add）。无效 entity 时结果是"选择集被清空"。
inline void exclusive(Registry& registry, Entity entity)
{
    clear(registry);
    add(registry, entity);
}

/// 当前全部选中实体。顺序与 each<Selected>() 一致（稀疏集迭代序，不是用户选择序）。
[[nodiscard]] inline std::vector<Entity> all(Registry& registry)
{
    std::vector<Entity> selected;
    registry.view<Selected>().each([&](Entity entity) { selected.push_back(entity); });
    return selected;
}

/**
 * @brief 有序选择集 + Primary。
 *
 * 不变量：
 *  - ordered() 中每一个仍 valid 且 all_of<Selected> 的 Entity 都在列表中；
 *  - 写路径（add/remove/toggle/...）始终同步 Selected 标签；
 *  - primary() == ordered().back()（空集时返回无效 Handle）；
 *  - setPrimary 将目标移到末尾（成为新的 primary）。
 *
 * 实体被 Registry::destroy 后，调用 prune() 或依赖 onDestroy 监听清理失效项。
 * 本类不拥有 Registry 生命周期。
 */
class Selection
{
public:
    using ChangedCallback = std::function<void(const Selection&)>;

    explicit Selection(Registry& registry)
        : m_registry(registry)
    {
        // 实体销毁时 Selected 会先走 onDestroy，顺带从有序列表剔除。
        // m_destroyConn = m_registry.on_destroy<Selected>().connect([this](Registry&, Entity entity) {
        //     eraseFromOrdered(entity);
        //     // 不在这里 notify：destroy 路径上调用方往往还有批量操作；
        //     // 若需要即时 UI 刷新，由 RegistryBridge 的 entityDestroyed 驱动。
        // });
        m_registry.on_destroy<Selected>().connect<&Selection::eraseFromOrdered>(*this);
    }

    ~Selection() = default;

    Selection(const Selection&)            = delete;
    Selection& operator=(const Selection&) = delete;

    // ---- 查询 ----
    [[nodiscard]] std::size_t count() const noexcept { return m_ordered.size(); }
    [[nodiscard]] bool empty() const noexcept { return m_ordered.empty(); }

    [[nodiscard]] bool contains(Entity entity) const
    {
        return std::find(m_ordered.begin(), m_ordered.end(), entity) != m_ordered.end();
    }

    /// 主选：有序列表最后一个；空集返回无效 Handle。
    [[nodiscard]] Entity primary() const noexcept
    {
        return m_ordered.empty() ? nullentity : m_ordered.back();
    }

    [[nodiscard]] const std::vector<Entity>& ordered() const noexcept { return m_ordered; }

    // ---- 修改（同步 Selected 标签）----

    /// 加入选择；已存在则移到末尾成为 primary。
    void add(Entity entity)
    {
        if (!m_registry.valid(entity)) {
            return;
        }
        if (contains(entity)) {
            // 已选中：提到末尾
            eraseFromOrdered(m_registry, entity);
            m_ordered.push_back(entity);
            notifyChanged();
            return;
        }
        m_registry.emplace<Selected>(entity);
        m_ordered.push_back(entity);
        notifyChanged();
    }

    void remove(Entity entity)
    {
        if (!contains(entity)) {
            return;
        }
        if (m_registry.valid(entity)) {
            m_registry.remove<Selected>(entity);
        }
        eraseFromOrdered(m_registry, entity);
        notifyChanged();
    }

    void toggle(Entity entity)
    {
        if (!m_registry.valid(entity)) {
            return;
        }
        if (contains(entity)) {
            remove(entity);
        } else {
            add(entity);
        }
    }

    void clear()
    {
        if (m_ordered.empty()) {
            return;
        }
        // 先快照再改标签，避免遍历中修改
        const std::vector<Entity> snapshot = m_ordered;
        m_ordered.clear();
        for (Entity h : snapshot) {
            if (m_registry.valid(h)) {
                m_registry.remove<Selected>(h);
            }
        }
        notifyChanged();
    }

    void exclusive(Entity entity)
    {
        clear();
        add(entity);
    }

    /// 整表替换，保持传入顺序；无效 entity 被跳过。
    void set(const std::vector<Entity>& handles)
    {
        // 去重并过滤无效，保持首次出现顺序
        std::vector<Entity> unique;
        unique.reserve(handles.size());
        for (Entity h : handles) {
            if (!m_registry.valid(h)) {
                continue;
            }
            if (std::find(unique.begin(), unique.end(), h) == unique.end()) {
                unique.push_back(h);
            }
        }

        // 移除不再需要的标签
        for (Entity h : m_ordered) {
            if (std::find(unique.begin(), unique.end(), h) == unique.end() && m_registry.valid(h)) {
                m_registry.remove<Selected>(h);
            }
        }
        // 补上新标签
        for (Entity h : unique) {
            if (!m_registry.all_of<Selected>(h)) {
                m_registry.emplace<Selected>(h);
            }
        }
        m_ordered = std::move(unique);
        notifyChanged();
    }

    /// 要求已在选中集内：移到末尾成为 primary；否则空操作。
    void setPrimary(Entity entity)
    {
        if (!contains(entity) || !m_registry.valid(entity)) {
            return;
        }
        if (primary() == entity) {
            return;
        }
        eraseFromOrdered(m_registry, entity);
        m_ordered.push_back(entity);
        notifyChanged();
    }

    /// 剔除已销毁或不再带 Selected 的 Handle。
    void prune()
    {
        const auto oldSize = m_ordered.size();
        m_ordered.erase(std::remove_if(m_ordered.begin(),
                                       m_ordered.end(),
                                       [this](Entity h) {
                                           return !m_registry.valid(h)
                                                  || !m_registry.all_of<Selected>(h);
                                       }),
                        m_ordered.end());
        if (m_ordered.size() != oldSize) {
            notifyChanged();
        }
    }

    /**
     * @brief 订阅集合级变更。返回的 Connection 析构时取消订阅。
     * @note 回调在写操作末尾同步触发；调用方勿在回调内再次改选择集以免重入。
     */
    [[nodiscard]] Connection onChanged(ChangedCallback cb)
    {
        const std::size_t id = m_nextCallbackId++;
        m_callbacks.emplace_back(id, std::move(cb));
        return Connection([this, id]() {
            m_callbacks.erase(std::remove_if(m_callbacks.begin(),
                                             m_callbacks.end(),
                                             [id](const auto& p) { return p.first == id; }),
                              m_callbacks.end());
        });
    }

private:
    void eraseFromOrdered(Registry& /*registry*/, Entity entity)
    {
        m_ordered.erase(std::remove(m_ordered.begin(), m_ordered.end(), entity), m_ordered.end());
    }

    void notifyChanged()
    {
        // 拷贝一份，防止回调里 onChanged/disconnect 改容器
        const auto snapshot = m_callbacks;
        for (const auto& [id, cb] : snapshot) {
            if (cb) {
                cb(*this);
            }
        }
    }

    Registry& m_registry;
    std::vector<Entity> m_ordered;
    std::vector<std::pair<std::size_t, ChangedCallback>> m_callbacks;
    std::size_t m_nextCallbackId{1};
    Connection m_destroyConn;
};

} // namespace bakuon::core::selection
