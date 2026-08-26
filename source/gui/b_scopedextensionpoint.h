#pragma once

#include <algorithm>
#include <shared_mutex>
#include <unordered_set>

#include <bakuon/gui/IExtensionPoint.h>

namespace bakuon::gui {

/* ============================================================
 *  1) ScopedExtensionPoint<T, Scope>
 *     作用域 / 权限过滤扩展点
 * ============================================================ */

/**
 * @brief 作用域 / 权限过滤扩展点
 * @tparam T     扩展接口类型
 * @tparam Scope 作用域标签类型，要求可哈希 + 可相等比较（默认 std::string）
 *
 * 典型用法：
 *  - 给每个插件实现打上"可见作用域"（例如项目类型、编辑器模式、用户角色）；
 *  - 消费者查询时传入当前上下文作用域，自动过滤不可见扩展；
 *  - 未显式指定作用域的扩展视为"全局扩展"，在任意作用域下均可见。
 *
 * 示例：
 * @code
 *   enum class ProjectKind { Cpp, Rust, Python };
 *   auto ep = std::make_shared<ScopedExtensionPoint<IBuilder, ProjectKind>>(
 *       "...id", "...desc");
 *   ep->registerExtension(cppBuilder, 100, {ProjectKind::Cpp});
 *   ep->registerExtension(fallback, 10);   // 全局可见
 *   auto list = ep->extensionsIn(ProjectKind::Cpp); // 返回 cppBuilder + fallback
 * @endcode
 */
template<typename T, typename Scope = std::string>
class ScopedExtensionPoint final : public IExtensionPoint<T>
{
public:
    using typename IExtensionPoint<T>::ExtensionPtr;
    using typename IExtensionPoint<T>::ExtensionList;
    using typename IExtensionPoint<T>::FilterFunc;
    using ScopeSet = std::unordered_set<Scope>;

    /**
     * @brief 构造作用域扩展点
     * @param id          IID
     * @param description 描述
     */
    explicit ScopedExtensionPoint(std::string id, std::string description = {})
        : m_id(std::move(id))
        , m_description(std::move(description))
        , m_orderCounter(0)
    {
    }

    /* ---------- 元信息 ---------- */

    [[nodiscard]] std::string id() const override { return m_id; }
    [[nodiscard]] std::string description() const override { return m_description; }

    /* ---------- 注册 / 注销 ---------- */

    /**
     * @brief 注册扩展（全局可见，任意作用域下均返回）
     * @param extension 扩展实现
     * @param priority  优先级
     * @return true 注册成功
     */
    bool registerExtension(ExtensionPtr extension, int priority) override
    {
        return registerScoped(std::move(extension), priority, ScopeSet{});
    }

    /**
     * @brief 注册扩展到指定作用域集合
     * @param extension 扩展实现
     * @param priority  优先级
     * @param scopes    可见作用域列表
     * @return true 注册成功
     */
    bool registerExtension(ExtensionPtr extension, int priority, std::initializer_list<Scope> scopes)
    {
        return registerScoped(std::move(extension),
                              priority,
                              ScopeSet(scopes.begin(), scopes.end()));
    }

    /**
     * @brief 注册扩展到单个作用域
     */
    bool registerExtension(ExtensionPtr extension, int priority, Scope scope)
    {
        ScopeSet s;
        s.emplace(std::move(scope));
        return registerScoped(std::move(extension), priority, std::move(s));
    }

    bool unregisterExtension(const ExtensionPtr& extension) override
    {
        if (!extension)
            return false;
        std::unique_lock<std::shared_mutex> lock(m_mutex);
        const auto it = std::find_if(m_entries.begin(), m_entries.end(), [&](const Entry& e) {
            return e.ext == extension;
        });
        if (it == m_entries.end())
            return false;
        m_entries.erase(it);
        return true;
    }

    /* ---------- 无过滤查询（实现基类纯虚接口，返回全部扩展） ---------- */

    [[nodiscard]] ExtensionList extensions() const override
    {
        std::shared_lock<std::shared_mutex> lock(m_mutex);
        return snapshotAll();
    }

    [[nodiscard]] ExtensionList extensions(FilterFunc filter) const override
    {
        std::shared_lock<std::shared_mutex> lock(m_mutex);
        ExtensionList out;
        for (const auto& e : m_entries) {
            if (!filter || filter(e.ext))
                out.push_back(e.ext);
        }
        return out;
    }

    /* ---------- 作用域过滤查询 ---------- */

    /**
     * @brief 返回在指定作用域下可见的扩展（含全局扩展）
     * @param scope 当前上下文作用域
     * @return 按优先级排序的扩展快照
     */
    [[nodiscard]] ExtensionList extensionsIn(const Scope& scope) const
    {
        std::shared_lock<std::shared_mutex> lock(m_mutex);
        ExtensionList out;
        for (const auto& e : m_entries) {
            if (e.scopes.empty() || e.scopes.contains(scope)) {
                out.push_back(e.ext);
            }
        }
        return out;
    }

    /**
     * @brief 返回在任意给定作用域下均可见的扩展（AND 语义，全局扩展永远可见）
     */
    [[nodiscard]] ExtensionList extensionsInAll(std::initializer_list<Scope> scopes) const
    {
        const ScopeSet required(scopes.begin(), scopes.end());
        std::shared_lock<std::shared_mutex> lock(m_mutex);
        ExtensionList out;
        for (const auto& e : m_entries) {
            if (e.scopes.empty()) {
                out.push_back(e.ext);
                continue;
            }
            bool ok = true;
            for (const auto& s : required) {
                if (!e.scopes.contains(s)) {
                    ok = false;
                    break;
                }
            }
            if (ok)
                out.push_back(e.ext);
        }
        return out;
    }

    /**
     * @brief 返回在任一给定作用域下可见的扩展（OR 语义，全局扩展永远可见）
     */
    [[nodiscard]] ExtensionList extensionsInAny(std::initializer_list<Scope> scopes) const
    {
        const ScopeSet allowed(scopes.begin(), scopes.end());
        std::shared_lock<std::shared_mutex> lock(m_mutex);
        ExtensionList out;
        for (const auto& e : m_entries) {
            if (e.scopes.empty()) {
                out.push_back(e.ext);
                continue;
            }
            for (const auto& s : allowed) {
                if (e.scopes.contains(s)) {
                    out.push_back(e.ext);
                    break;
                }
            }
        }
        return out;
    }
    /**
     * @brief 指定作用域下的责任链查找
     * @param scope        当前上下文作用域
     * @param handler      处理回调
     * @param defaultValue 默认值
     */
    template<typename Result, typename Handler>
    Result tryExtensionsIn(const Scope& scope, Handler&& handler, Result defaultValue = {}) const
    {
        const auto snapshot = extensionsIn(scope);
        for (const auto& ext : snapshot) {
            Result r = std::invoke(std::forward<Handler>(handler), ext);
            if (r != defaultValue)
                return r;
        }
        return defaultValue;
    }

    /* ---------- 其他 ---------- */

    [[nodiscard]] int priority(const ExtensionPtr& extension) const override
    {
        if (!extension)
            return -1;
        std::shared_lock<std::shared_mutex> lock(m_mutex);
        const auto it = std::find_if(m_entries.begin(), m_entries.end(), [&](const Entry& e) {
            return e.ext == extension;
        });
        return it != m_entries.end() ? it->priority : -1;
    }

    bool setPriority(const ExtensionPtr& extension, int newPriority) override
    {
        if (!extension)
            return false;
        std::unique_lock<std::shared_mutex> lock(m_mutex);
        const auto it = std::find_if(m_entries.begin(), m_entries.end(), [&](const Entry& e) {
            return e.ext == extension;
        });
        if (it == m_entries.end())
            return false;
        it->priority = newPriority;
        sortEntries();
        return true;
    }

    [[nodiscard]] std::size_t count() const override
    {
        std::shared_lock<std::shared_mutex> lock(m_mutex);
        return m_entries.size();
    }

    void clear() override
    {
        std::unique_lock<std::shared_mutex> lock(m_mutex);
        m_entries.clear();
        m_orderCounter = 0;
    }

private:
    struct Entry
    {
        ExtensionPtr ext;
        int priority;
        std::size_t order;
        ScopeSet scopes; ///< 空集合 = 全局可见
    };

    bool registerScoped(ExtensionPtr ext, int priority, ScopeSet scopes)
    {
        if (!ext)
            return false;
        std::unique_lock<std::shared_mutex> lock(m_mutex);
        for (const auto& e : m_entries) {
            if (e.ext == ext)
                return false;
        }
        m_entries.push_back({std::move(ext), priority, m_orderCounter++, std::move(scopes)});
        sortEntries();
        return true;
    }

    [[nodiscard]] ExtensionList snapshotAll() const
    {
        ExtensionList out;
        out.reserve(m_entries.size());
        for (const auto& e : m_entries)
            out.push_back(e.ext);
        return out;
    }

    void sortEntries()
    {
        std::sort(m_entries.begin(), m_entries.end(), [](const Entry& a, const Entry& b) {
            if (a.priority != b.priority)
                return a.priority > b.priority;
            return a.order < b.order;
        });
    }

private:
    std::string m_id;
    std::string m_description;
    std::vector<Entry> m_entries;
    mutable std::shared_mutex m_mutex;
    std::size_t m_orderCounter;
};

} // namespace bakuon::gui
