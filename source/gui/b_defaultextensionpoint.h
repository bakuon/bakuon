#pragma once

#include <algorithm>
#include <mutex>
#include <shared_mutex>

#include <bakuon/gui/IExtensionPoint.h>

namespace bakuon::gui {

/* ============================================================
 *  DefaultExtensionPoint<T>：框架内置的默认存储实现
 * ============================================================ */

/**
 * @brief 默认扩展点实现：基于 vector 的有序存储，读写锁保护
 * @tparam T 扩展接口类型
 *
 * 内部元素按 (priority desc, registerOrder asc) 稳定排序；所有写操作在
 * unique_lock 下进行，并触发重新排序；读操作返回快照（shared_lock 下拷贝）。
 *
 * 该实现属于框架默认提供的"内置实现"，使用者可以通过继承 ExtensionPoint<T>
 * 提供自定义策略（如延迟实例化、权限校验、远程代理等）而无需修改框架，
 * 符合开闭原则。
 */
template<typename T>
class DefaultExtensionPoint : public IExtensionPoint<T>
{
public:
    using typename IExtensionPoint<T>::ExtensionPtr;
    using typename IExtensionPoint<T>::ExtensionList;
    using typename IExtensionPoint<T>::FilterFunc;

    /**
     * @brief 构造默认扩展点
     * @param id          扩展点唯一 IID
     * @param description 扩展点描述
     */
    explicit DefaultExtensionPoint(std::string id, std::string description = {})
        : m_id(std::move(id))
        , m_description(std::move(description))
        , m_orderCounter(0)
    {
    }

    /* ---------- 元信息 ---------- */

    [[nodiscard]] std::string id() const override { return m_id; }
    [[nodiscard]] std::string description() const override { return m_description; }

    /* ---------- 注册 / 注销 ---------- */

    bool registerExtension(ExtensionPtr extension, int priority) override
    {
        if (!extension) {
            return false;
        }

        std::unique_lock<std::shared_mutex> lock(m_mutex);
        for (const auto& entry : m_entries) {
            if (entry.ext == extension) {
                return false; // 重复注册
            }
        }

        m_entries.push_back({std::move(extension), priority, m_orderCounter++});
        sortEntries();
        return true;
    }

    bool unregisterExtension(const ExtensionPtr& extension) override
    {
        if (!extension) {
            return false;
        }
        std::unique_lock<std::shared_mutex> lock(m_mutex);
        const auto it = std::find_if(m_entries.begin(), m_entries.end(), [&](const Entry& e) {
            return e.ext == extension;
        });
        if (it == m_entries.end()) {
            return false;
        }
        m_entries.erase(it);
        return true;
    }

    /* ---------- 查询 ---------- */

    [[nodiscard]] ExtensionList extensions() const override
    {
        std::shared_lock<std::shared_mutex> lock(m_mutex);
        ExtensionList result;
        result.reserve(m_entries.size());
        for (const auto& e : m_entries) {
            result.push_back(e.ext);
        }
        return result;
    }

    [[nodiscard]] ExtensionList extensions(FilterFunc filter) const override
    {
        std::shared_lock<std::shared_mutex> lock(m_mutex);
        ExtensionList result;
        for (const auto& e : m_entries) {
            if (!filter || filter(e.ext)) {
                result.push_back(e.ext);
            }
        }
        return result;
    }

    [[nodiscard]] int priority(const ExtensionPtr& extension) const override
    {
        if (!extension) {
            return -1;
        }
        std::shared_lock<std::shared_mutex> lock(m_mutex);
        const auto it = std::find_if(m_entries.begin(), m_entries.end(), [&](const Entry& e) {
            return e.ext == extension;
        });
        return it != m_entries.end() ? it->priority : -1;
    }

    bool setPriority(const ExtensionPtr& extension, int newPriority) override
    {
        if (!extension) {
            return false;
        }
        std::unique_lock<std::shared_mutex> lock(m_mutex);
        const auto it = std::find_if(m_entries.begin(), m_entries.end(), [&](const Entry& e) {
            return e.ext == extension;
        });
        if (it == m_entries.end()) {
            return false;
        }
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
        std::size_t order; ///< 注册顺序，用于稳定排序
    };

    /**
     * @brief 按 priority 降序、order 升序排列（需在写锁保护下调用）
     */
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
