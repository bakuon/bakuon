#pragma once

#include <atomic>
#include <future>
#include <mutex>
#include <thread>
#include <type_traits>

#include "gui/b_defaultextensionpoint.h"

namespace bakuon::gui {

/* ============================================================
 *  3) AsyncExtensionPoint<T>
 *     异步扩展点（支持广播与异步责任链）
 * ============================================================ */

namespace extension {

/**
 * @brief 异步责任链的竞速策略
 */
enum class RacePolicy {
    FirstWins,       // 并发调用所有扩展，第一个返回非默认值的作为结果（高吞吐，不严格按优先级）
    PriorityOrdered, // 按优先级顺序串行调用（在前一个失败后才调用下一个），严格优先级
};

/**
 * @brief Executor 抽象：投递一个无参可调用对象到目标执行上下文
 *
 * 默认实现使用 std::thread 分离线程；使用者可替换为：
 *  - QThreadPool::start
 *  - asio::thread_pool::executor
 *  - 项目自定义的主线程/worker 线程投递器
 */
using Executor = std::function<void(std::function<void()>)>;

} // namespace extension

/**
 * @brief 异步扩展点：扩展调用通过 extension::Executor 异步执行，返回 std::future
 * @tparam T 扩展接口类型
 *
 * 典型用法：
 *  - 耗时操作（索引、编译、网络请求、代码分析）通过广播或责任链并发触发；
 *  - UI 线程注册扩展点，后台线程池执行，future.then() 或 co_await 回到 UI 更新。
 *
 * 继承关系：
 *  - 本类继承 DefaultExtensionPoint<T>，注册/注销/查询/优先级/排序/计数/清空等
 *    同步管理 API 全部复用基类实现（读写锁、稳定排序、快照语义一致），避免代码重复；
 *  - 仅在基类之上增量提供异步广播（broadcastAsync）与异步责任链（tryAsync）能力；
 *  - extension::Executor 的切换使用独立的轻量级锁（m_execMutex），与基类存储锁相互独立，
 *    避免锁层级耦合导致死锁。
 *
 * 线程安全：
 *  - 异步调用期间，即使原扩展被注销（shared_ptr 引用计数仍被 future 持有），
 *    已在执行的 handler 仍可安全访问扩展对象；
 *  - 单个扩展抛异常会被 promise 捕获并 set_exception，不影响其他扩展继续尝试。
 */
template<typename T>
class AsyncExtensionPoint final : public DefaultExtensionPoint<T>
{
public:
    using Base = DefaultExtensionPoint<T>;
    using typename Base::ExtensionList;
    using typename Base::ExtensionPtr;
    using typename Base::FilterFunc;

    /**
     * @brief 构造异步扩展点
     * @param id          IID
     * @param description 描述
     * @param executor    任务执行器；为空则使用默认 std::thread 分离线程实现
     */
    explicit AsyncExtensionPoint(std::string id, std::string description = {},
                                 extension::Executor executor = {})
        : Base(std::move(id), std::move(description))
        , m_executor(executor ? std::move(executor) : defaultExecutor())
    {
    }

    /**
     * @brief 更换执行器（线程安全）
     */
    void setExecutor(extension::Executor executor)
    {
        if (!executor)
            return;
        std::lock_guard<std::mutex> lock(m_execMutex);
        m_executor = std::move(executor);
    }

    /**
     * @brief 默认执行器：使用 std::thread 分离线程并发执行任务
     */
    static extension::Executor defaultExecutor()
    {
        return [](std::function<void()> task) {
            if (!task)
                return;
            // 投递到独立后台线程；调用方通过持有的 std::future 等待/获取结果。
            // 注：broadcastAsync / tryAsync 返回的 future 必须由调用方持有，
            // 否则任务虽已投递但结果无法回收。
            std::thread([t = std::move(task)] { t(); }).detach();
        };
    }

    /* ---------- 异步 API ---------- */

    /**
     * @brief 对所有扩展并发执行 handler（广播），返回每个调用的 future 集合
     * @tparam Fn 可调用对象：void(const std::shared_ptr<T>&) 或返回任意类型
     * @param handler 对每个扩展执行的回调
     * @return std::vector<std::future<R>>，按优先级顺序排列；调用方持有 future 以等待完成
     *
     * @note 该接口立即返回，不会阻塞调用线程；每个扩展通过 extension::Executor 异步执行。
     */
    template<typename Fn>
    auto broadcastAsync(Fn&& handler) const
        -> std::vector<std::future<std::invoke_result_t<Fn, const ExtensionPtr&>>>
    {
        using R                      = std::invoke_result_t<Fn, const ExtensionPtr&>;
        // 复用基类 extensions() 的快照语义（基类已加 shared_lock，返回安全副本）
        const ExtensionList snapshot = this->extensions();
        std::vector<std::future<R>> futures;
        futures.reserve(snapshot.size());
        extension::Executor exec = currentExecutor();
        auto sharedHandler       = std::make_shared<std::decay_t<Fn>>(std::forward<Fn>(handler));
        for (const auto& ext : snapshot) {
            std::promise<R> p;
            futures.push_back(p.get_future());
            auto sharedPromise = std::make_shared<std::promise<R>>(std::move(p));
            exec([ext, sharedHandler, sharedPromise]() mutable {
                try {
                    if constexpr (std::is_void_v<R>) {
                        std::invoke(*sharedHandler, ext);
                        sharedPromise->set_value();
                    } else {
                        sharedPromise->set_value(std::invoke(*sharedHandler, ext));
                    }
                } catch (...) {
                    try {
                        sharedPromise->set_exception(std::current_exception());
                    } catch (...) {
                    }
                }
            });
        }
        return futures;
    }

    /**
     * @brief 异步责任链：按策略寻找第一个返回非默认值的扩展
     * @tparam Result 返回类型
     * @param handler      处理回调，返回 Result
     * @param defaultValue 默认值
     * @param policy       竞速策略
     * @return std::future<Result>；set_value 时即为最终结果
     */
    template<typename Result, typename Handler>
    std::future<Result> tryAsync(Handler&& handler, Result defaultValue = {},
                                 extension::RacePolicy policy = extension::RacePolicy::FirstWins) const
    {
        // 复用基类 extensions() 的快照语义
        const auto snapshot = this->extensions();
        auto sharedPromise  = std::make_shared<std::promise<Result>>();
        auto fut            = sharedPromise->get_future();

        if (snapshot.empty()) {
            sharedPromise->set_value(defaultValue);
            return fut;
        }

        extension::Executor exec = currentExecutor();

        if (policy == extension::RacePolicy::FirstWins) {
            // 并发竞速：第一个返回非默认值的 set_value
            auto done          = std::make_shared<std::atomic<bool>>(false);
            auto pending       = std::make_shared<std::atomic<std::size_t>>(snapshot.size());
            auto sharedHandler = std::make_shared<std::decay_t<Handler>>(
                std::forward<Handler>(handler));
            for (const auto& ext : snapshot) {
                exec([ext, sharedHandler, done, pending, sharedPromise, defaultValue]() mutable {
                    try {
                        Result r = std::invoke(*sharedHandler, ext);
                        // 注意：这里必须和 Result{} 比较（"这个扩展有没有给出答案"），
                        // 不能和 defaultValue 比较——defaultValue 是"全部扩展都没有答案时，
                        // 返回给调用方的兜底值"，调用方完全可以把它设成跟 Result{} 不一样的值
                        // （比如这里的 "NOT_FOUND"），两者是两个不同的概念，混在一起比较会导致
                        // "扩展明明没给出答案（返回了 Result{}），却被误判成命中"。
                        if (r != Result{}) {
                            bool expected = false;
                            if (done->compare_exchange_strong(expected, true)) {
                                try {
                                    sharedPromise->set_value(std::move(r));
                                } catch (...) {
                                }
                            }
                        }
                    } catch (...) {
                        // 单个扩展抛异常不影响其他扩展尝试
                    }
                    if (pending->fetch_sub(1, std::memory_order_acq_rel) == 1) {
                        // 所有扩展均尝试过且无命中
                        bool expected = false;
                        if (done->compare_exchange_strong(expected, true)) {
                            try {
                                sharedPromise->set_value(defaultValue);
                            } catch (...) {
                            }
                        }
                    }
                }); // exec
            } // for
        } else {
            // PriorityOrdered：串行链式投递，前一个失败再投下一个
            auto sharedHandler = std::make_shared<std::function<Result(const ExtensionPtr&)>>(
                [h = std::forward<Handler>(handler)](const ExtensionPtr& ext) -> Result {
                    return std::invoke(h, ext);
                });
            chainInvoke<Result>(snapshot, 0, sharedHandler, defaultValue, sharedPromise, exec);
        }
        return fut;
    }

private:
    /**
     * @brief 线程安全地读取当前 executor（拷贝一份快照，避免持锁回调）
     */
    [[nodiscard]] extension::Executor currentExecutor() const
    {
        std::lock_guard<std::mutex> lock(m_execMutex);
        return m_executor;
    }

    /**
     * @brief PriorityOrdered 模式下的链式投递：在 executor 中调用第 i 个扩展，
     *        未命中则继续投递 i+1；命中或全部失败则 set_value
     */
    template<typename Result>
    void chainInvoke(const ExtensionList& snapshot, std::size_t idx,
                     std::shared_ptr<std::function<Result(const ExtensionPtr&)>> sharedHandler,
                     const Result& defaultValue, std::shared_ptr<std::promise<Result>> promise,
                     const extension::Executor& exec) const
    {
        if (idx >= snapshot.size()) {
            try {
                promise->set_value(defaultValue);
            } catch (...) {
            }
            return;
        }
        auto ext = snapshot[idx];
        exec([this, snapshot, idx, ext, sharedHandler, defaultValue, promise, exec]() mutable {
            try {
                Result r = (*sharedHandler)(ext);
                // 和 Result{} 比较，不能和 defaultValue 比较，见 tryAsync() FirstWins 分支的注释。
                if (r != Result{}) {
                    try {
                        promise->set_value(std::move(r));
                    } catch (...) {
                    }
                    return;
                }
            } catch (...) {
                // 单个扩展失败继续下一个
            }
            chainInvoke<Result>(snapshot, idx + 1, sharedHandler, defaultValue, promise, exec);
        });
    }

private:
    extension::Executor m_executor; /**< 当前使用的任务执行器 */
    mutable std::mutex m_execMutex; /**< 保护 m_executor 切换的独立轻量锁 */
};

} // namespace bakuon::gui
