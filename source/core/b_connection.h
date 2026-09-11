#pragma once

#include <functional>
#include <utility>

namespace bakuon::core {

/**
 * @brief RAII 信号连接守卫。
 *
 * @details 与 gui::ContextGuard（source/gui/b_commandsystem.h）同样的设计模式：
 * 构造时持有一份"如何断开这次连接"的回调，析构时自动调用；支持提前 dismiss()
 * 放弃自动断开、支持移动、禁止拷贝——同一份连接只能有一个所有者。
 *
 * Registry::onConstruct()/onUpdate()/onDestroy() 返回的就是本类型：调用方拿到
 * 一个 Connection 对象即可管理这次观察者注册的生命周期，不需要认识任何
 * entt::sink/entt::sigh 类型（那些是 core 内部实现细节，不应该泄漏给消费者）。
 *
 * @code
 *   bakuon::core::Registry registry;
 *   {
 *       auto conn = registry.onConstruct<Position>([](Registry&, EntityId id) {
 *           // ...
 *       });
 *       // conn 存活期间，Position 组件被 emplace 时会调用上面的回调
 *   } // 离开作用域，conn 析构，自动断开连接
 * @endcode
 *
 * ## disconnector 与 onDismiss 为什么是两个独立的回调
 * 第一版实现只有一个"断开回调"，dismiss() 简单地把它设为空——这在回调本身
 * 只是"从某个容器里摘除一条记录"时是安全的，但当断开回调背后还挂着一份
 * 堆上分配的资源时（典型如 Registry::bindSink() 里为每次订阅分配的 Slot<T>，
 * entt::sink 内部保存的是指向它的裸指针，不是 shared_ptr），"忘记"断开回调
 * 会连带丢弃该回调闭包里捕获的最后一份 shared_ptr 引用——资源立刻被释放，
 * 而 entt 那边浑然不知，下一次事件触发就是一次悬空访问（这不是理论上的
 * 担忧，是在给 dismiss() 补单元测试时被 AddressSanitizer 实测抓到的一次
 * 真实 heap-use-after-free，修复过程见提交历史）。
 *
 * 现在的设计把"断开"和"放弃自动断开"拆成两条独立的路径：
 *  - disconnector：真正执行断开动作（比如 entt::connection::release()），
 *    执行完毕后它捕获的资源自然、安全地跟着一起释放；
 *  - onDismiss：dismiss() 时执行，职责仅仅是"把背后资源的所有权转移到一个
 *    活得比这个 Connection 更久的地方"（Registry::bindSink() 会把它转交给
 *    Registry 自己持有，见该函数实现），不释放任何东西。
 * 两者互斥：无论 Connection 最终是被断开、还是被 dismiss()，都只会有其中
 * 一条路径真正执行，另一条会被清空、不再触发。
 */
class Connection
{
public:
    Connection() = default;

    /**
     * @param disconnector 断开连接时执行的回调；执行后视为"这次订阅彻底结束"，
     *                      其闭包捕获的任何资源都应该在此刻被安全释放。
     * @param onDismiss    可选。dismiss() 时执行的回调，职责只是"移交资源所有权
     *                     给别处"，不应该释放任何东西——省略（默认空）适用于
     *                     背后没有任何需要单独续命的堆资源的连接。
     */
    explicit Connection(std::function<void()> disconnector,
                        std::function<void()> onDismiss = {}) noexcept
        : m_disconnector(std::move(disconnector))
        , m_onDismiss(std::move(onDismiss))
    {
    }

    Connection(const Connection&)            = delete;
    Connection& operator=(const Connection&) = delete;

    Connection(Connection&& other) noexcept
        : m_disconnector(std::move(other.m_disconnector))
        , m_onDismiss(std::move(other.m_onDismiss))
    {
        other.m_disconnector = nullptr;
        other.m_onDismiss    = nullptr;
    }

    Connection& operator=(Connection&& other) noexcept
    {
        if (this != &other) {
            disconnect();
            m_disconnector       = std::move(other.m_disconnector);
            m_onDismiss          = std::move(other.m_onDismiss);
            other.m_disconnector = nullptr;
            other.m_onDismiss    = nullptr;
        }
        return *this;
    }

    ~Connection() { disconnect(); }

    /// 主动断开连接；幂等，重复调用/对一个从未连接过的对象调用都是安全的空操作。
    void disconnect()
    {
        if (m_disconnector) {
            m_disconnector();
            m_disconnector = nullptr;
        }
        // 已经真正断开：onDismiss 存在的意义只是"万一将来 dismiss() 该把资源
        // 移交去哪"，既然已经走了断开路径、资源已经安全释放，这份"移交指引"
        // 也不再有意义，一并清空，避免调用方之后误调 dismiss() 又把（可能是
        // 别的连接复用的）资源重复移交一次。
        m_onDismiss = nullptr;
    }

    /**
     * @brief 放弃自动断开：析构时不再调用断开回调，转而调用 onDismiss()（若提供）
     * 把背后资源的所有权移交出去，然后彻底放弃对这次连接的一切控制权——调用方
     * 之后无法再通过这个 Connection 对象重新断开它，这是有意的：dismiss() 的
     * 语义就是"我确定这个订阅应该和它的宿主活得一样久，不需要再被单独管理"。
     */
    void dismiss() noexcept
    {
        if (m_onDismiss) {
            m_onDismiss();
        }
        m_disconnector = nullptr;
        m_onDismiss    = nullptr;
    }

    [[nodiscard]] bool isConnected() const noexcept { return static_cast<bool>(m_disconnector); }

private:
    std::function<void()> m_disconnector;
    std::function<void()> m_onDismiss;
};

} // namespace bakuon::core
