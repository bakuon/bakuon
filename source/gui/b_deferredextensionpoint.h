#pragma once

#include <algorithm>
#include <mutex>
#include <shared_mutex>

#include <bakuon/gui/IExtensionPoint.h>

namespace bakuon::gui {

/* ============================================================
 *  2) DeferredExtensionPoint<T>
 *     带工厂的延迟实例化扩展点
 * ============================================================ */

namespace extension {
/**
 * @brief 延迟实例化策略
 */
enum class DeferredPolicy {
    Singleton, ///< 单例的，首次使用时实例化一次，之后缓存共享
    Transient, ///< 瞬态的，每次访问均调用工厂创建新实例
};
} // namespace extension

/**
 * @brief 延迟工厂扩展点：注册工厂函数而非实例，按需构造
 * @tparam T 扩展接口类型
 *
 * 典型用法：
 *  - 插件实现构造开销较大（需要加载资源、初始化 GPU 句柄等），且在
 *    某次会话中可能根本不会被命中，此时使用延迟工厂可显著降低启动成本；
 *  - 需要多实例场景（每个请求一个独立实例）使用 Transient 策略；
 *  - 单例共享场景（如全局缓存）使用 Singleton 策略。
 *
 * 性能特性：
 *  - tryExtensions 责任链遍历时，对 Transient 工厂仅在真正需要调用
 *    handler 时才会触发构造；若前面的扩展已命中，后续工厂完全不执行；
 *  - Singleton 缓存采用 std::once_flag，线程安全、无锁读。
 */
template<typename T>
class DeferredExtensionPoint final : public IExtensionPoint<T>
{
public:
    using typename IExtensionPoint<T>::ExtensionPtr;
    using typename IExtensionPoint<T>::ExtensionList;
    using typename IExtensionPoint<T>::FilterFunc;
    using Factory = std::function<ExtensionPtr()>;

    /**
     * @brief 构造默认扩展点
     * @param id          扩展点唯一 IID
     * @param description 扩展点描述
     */
    explicit DeferredExtensionPoint(std::string id, std::string description = {})
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
     * @brief 注册工厂
     * @param factory  无参工厂函数，返回 shared_ptr<T>
     * @param priority 优先级
     * @param policy   实例化策略，默认 Singleton
     * @return true 注册成功
     */
    bool registerFactory(Factory factory, int priority,
                         extension::DeferredPolicy policy = extension::DeferredPolicy::Singleton)
    {
        if (!factory)
            return false;
        std::unique_lock<std::shared_mutex> lock(m_mutex);
        // cachedOnce 在这里（写锁保护下）就创建好，resolve() 才能放心地在只持有读锁、
        // 甚至完全不持锁的情况下调用 std::call_once（见 resolve() 的注释）。
        m_entries.push_back({std::move(factory),
                             priority,
                             m_orderCounter++,
                             policy,
                             nullptr,
                             std::make_unique<std::once_flag>()});
        sortEntries();
        return true;
    }

    /**
     * @brief 注册"已构造好的"实例（等价于注册一个返回该实例的 Singleton 工厂）
     * @param extension 已存在的实例
     * @param priority  优先级
     * @return true 注册成功
     */
    bool registerExtension(ExtensionPtr extension, int priority) override
    {
        if (!extension) {
            return false;
        }

        auto ext  = extension; // 拷贝，避免 lambda 捕获移动后状态
        Factory f = [ret = std::move(ext)]() mutable -> ExtensionPtr { return ret; };
        std::unique_lock<std::shared_mutex> lock(m_mutex);
        Entry entry;
        entry.factory    = std::move(f);
        entry.priority   = priority;
        entry.order      = m_orderCounter++;
        entry.policy     = extension::DeferredPolicy::Singleton;
        entry.cached     = extension; // 直接填充缓存
        // 同样在写锁保护下预先创建好，见 registerFactory() 和 resolve() 的注释。
        entry.cachedOnce = std::make_unique<std::once_flag>();
        m_entries.push_back(std::move(entry));
        sortEntries();
        return true;
    }

    bool unregisterExtension(const ExtensionPtr& extension) override
    {
        if (!extension) {
            return false;
        }
        std::unique_lock<std::shared_mutex> lock(m_mutex);
        // 对于已实例化的 Singleton，可通过 cached 查找
        const auto it = std::find_if(m_entries.begin(), m_entries.end(), [&](const Entry& e) {
            return e.cached && e.cached == extension;
        });
        if (it == m_entries.end()) {
            return false;
        }
        m_entries.erase(it);
        return true;
    }

    /**
     * @brief 按优先级顺序注销第一个工厂（如果无法拿到实例指针时使用）
     * @return true 成功注销
     */
    bool unregisterFactoryAt(std::size_t index)
    {
        std::unique_lock<std::shared_mutex> lock(m_mutex);
        if (index >= m_entries.size())
            return false;
        m_entries.erase(m_entries.begin() + static_cast<std::ptrdiff_t>(index));
        return true;
    }

    /* ---------- 查询 ---------- */

    /**
     * @brief 强制实例化所有 Singleton 扩展并返回（Transient 策略每次新建临时实例）
     * @note  Transient 策略返回的实例仅在本次快照生命周期内共享；若需持久引用，
     *        请使用 getOrCreate(index) 自行管理。
     */
    [[nodiscard]] ExtensionList extensions() const override
    {
        std::shared_lock<std::shared_mutex> lock(m_mutex);
        ExtensionList result;
        result.reserve(m_entries.size());
        for (const auto& e : m_entries) {
            result.push_back(resolve(e));
        }
        return result;
    }

    /**
     * @brief 强制实例化后再过滤
     */
    [[nodiscard]] ExtensionList extensions(FilterFunc filter) const override
    {
        std::shared_lock<std::shared_mutex> lock(m_mutex);
        ExtensionList result;
        for (const auto& e : m_entries) {
            auto inst = resolve(e);
            if (!filter || filter(inst)) {
                result.push_back(std::move(inst));
            }
        }
        return result;
    }

    /* ---------- 责任链：按需实例化，命中即停 ---------- */

    /**
     * @brief 重写 tryExtensions：按优先级顺序，仅当真正要访问扩展时才实例化
     * @tparam Result 返回类型
     * @param handler      回调，接收 const ExtensionPtr&（实例），返回 Result；
     *                     返回 Result{} 表示这个扩展没有答案，继续下一个（未命中的 Transient 工厂不会被调用）
     * @param defaultValue 全部扩展都没有答案时的兜底值（不要求等于 Result{}，见 IExtensionPoint::tryExtensions() 的说明）
     *
     * 注意：Transient 策略下，未命中的工厂不会被调用；命中的工厂在调用
     * handler 前被创建，handler 返回后该临时实例立即释放（除非 handler 自己
     * 保存了 shared_ptr）。
     */
    template<typename Result, typename Handler>
    Result tryExtensions(Handler&& handler, Result defaultValue = {}) const
    {
        // 拷贝索引表，避免遍历期间持锁
        std::vector<const Entry*> snapshot;
        {
            std::shared_lock<std::shared_mutex> lock(m_mutex);
            snapshot.reserve(m_entries.size());
            for (const auto& e : m_entries)
                snapshot.push_back(&e);
        }

        for (const auto* pe : snapshot) {
            ExtensionPtr inst = resolve(*pe);
            if (!inst)
                continue;
            Result r = std::invoke(std::forward<Handler>(handler), inst);
            if (r != Result{})
                return r;
        }
        return defaultValue;
    }

    /* ---------- 缓存管理 ---------- */

    /**
     * @brief 清空所有 Singleton 缓存（下次访问时重新构造）
     * @note Transient 策略不受影响
     */
    void clearCache()
    {
        std::unique_lock<std::shared_mutex> lock(m_mutex);
        for (auto& e : m_entries) {
            if (e.policy == extension::DeferredPolicy::Singleton) {
                e.cached.reset();
                // 直接换一个新的 once_flag（仍在写锁保护下），不能 reset() 成 nullptr——
                // 那样下次 resolve() 又会看到空指针，见 resolve() 里"绝不在这里懒加载"的注释。
                e.cachedOnce = std::make_unique<std::once_flag>();
            }
        }
    }

    /**
     * @brief 预实例化所有 Singleton 工厂（通常在启动阶段后台调用以预热）
     */
    void instantiateAll() const
    {
        std::shared_lock<std::shared_mutex> lock(m_mutex);
        for (const auto& e : m_entries) {
            (void) resolve(e);
        }
    }

    /**
     * @brief 返回已实例化（缓存非空）的 Singleton 数量（用于诊断）
     */
    [[nodiscard]] std::size_t instantiatedCount() const
    {
        std::shared_lock<std::shared_mutex> lock(m_mutex);
        std::size_t n = 0;
        for (const auto& e : m_entries) {
            if (e.cached)
                ++n;
        }
        return n;
    }

    [[nodiscard]] int priority(const ExtensionPtr& extension) const override
    {
        if (!extension) {
            return -1;
        }
        std::shared_lock<std::shared_mutex> lock(m_mutex);
        const auto it = std::find_if(m_entries.begin(), m_entries.end(), [&](const Entry& e) {
            return e.cached && e.cached == extension;
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
            return e.cached && e.cached == extension;
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
        Factory factory;
        int priority{};
        std::size_t order{};
        extension::DeferredPolicy policy{extension::DeferredPolicy::Singleton};
        mutable ExtensionPtr cached;
        mutable std::unique_ptr<std::once_flag> cachedOnce;
    };

    /**
     * @brief 根据策略解析扩展实例（调用时必须持有 m_mutex 的读/写锁，
     *        以保证 m_entries 生命周期稳定；但不要求持有任何锁来保护 e.cached/e.cachedOnce
     *        本身的并发写入——那件事由 std::call_once 负责）
     *
     * @warning 这里绝不能对 e.cachedOnce 做"为空则创建"的懒加载：resolve() 会在只持有 shared_lock
     *          （extensions()/instantiateAll()）、甚至完全不持锁（tryExtensions() 先拷贝快照、
     *          释放锁之后才调用 resolve()）的情况下被多线程并发调用；对同一个 mutable 成员指针
     *          做"检查为空再赋值"是未加保护的写，两个线程同时命中会产生数据竞争。之前这里确实
     *          有这个 bug，已经通过让 registerFactory()/registerExtension()/clearCache() 三处
     *          （都在 m_mutex 的写锁保护下）预先创建好 cachedOnce 来修复——resolve() 只管调用
     *          std::call_once，不再自己创建 once_flag。
     */
    ExtensionPtr resolve(const Entry& e) const
    {
        if (e.policy == extension::DeferredPolicy::Singleton) {
            //!!! 不要在这里这么做
            // if (!e.cachedOnce) {
            //     e.cachedOnce = std::make_unique<std::once_flag>();
            // }
            std::call_once(*e.cachedOnce, [&] {
                if (!e.cached && e.factory) {
                    e.cached = e.factory();
                }
            });
            return e.cached;
        }
        // Transient: 每次调用工厂
        return e.factory ? e.factory() : nullptr;
    }

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
