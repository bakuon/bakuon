#include <any>
#include <atomic>
#include <condition_variable>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <queue>
#include <shared_mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

/// 精简版纯本地 Actor 骨架实现： 仿 CAF 核心类源码设计与实现原理

using actor_id = uint64_t;

struct Message
{
    std::any content;
};

class AbstractActor;
class Scheduler;

// 全局调度器单例指针（方便演示）
extern Scheduler* g_scheduler;

// ==========================================
// 1. 线程池调度器 (Scheduler)
// ==========================================
class Scheduler
{
private:
    std::vector<std::thread> workers;
    std::queue<std::shared_ptr<AbstractActor>> ready_queue; // 存放准备好运行的 Actor 强引用
    std::mutex queue_mtx;
    std::condition_variable cv;
    std::atomic<bool> stop{false};

    void worker_thread();

public:
    explicit Scheduler(size_t threads)
    {
        for (size_t i = 0; i < threads; ++i) {
            workers.emplace_back(&Scheduler::worker_thread, this);
        }
    }

    ~Scheduler()
    {
        stop = true;
        cv.notify_all();
        for (std::thread& worker : workers) {
            if (worker.joinable())
                worker.join();
        }
    }

    // 调度一个 Actor 去执行（由于传入强引用，只要在队列里，Actor 就绝不会析构）
    void schedule(std::shared_ptr<AbstractActor> actor)
    {
        std::lock_guard<std::mutex> lock(queue_mtx);
        ready_queue.push(std::move(actor));
        cv.notify_one();
    }
};

Scheduler* g_scheduler = nullptr;

// ==========================================
// 2. 控制块与句柄
// ==========================================
struct ActorControlBlock
{
    actor_id id;
    std::weak_ptr<AbstractActor> actor_impl;

    explicit ActorControlBlock(actor_id aid)
        : id(aid)
    {
    }
    bool enqueue(Message msg);
};

class ActorHandle
{
public:
    std::shared_ptr<ActorControlBlock> cb;
    ActorHandle() = default;
    explicit ActorHandle(std::shared_ptr<ActorControlBlock> control_block)
        : cb(std::move(control_block))
    {
    }
    ActorControlBlock* operator->() const { return cb.get(); }
    explicit operator bool() const { return cb != nullptr; }
};

// ==========================================
// 3. 抽象 Actor 基类（包含生命周期修复）
// ==========================================
class AbstractActor : public std::enable_shared_from_this<AbstractActor>
{
private:
    std::queue<Message> mailbox;
    std::mutex mailbox_mtx;
    std::atomic<bool> is_scheduled{false}; // 防止同一个 Actor 被重复推入调度队列

protected:
    // 留给子类实现的核心业务逻辑
    virtual void on_receive(const Message& msg) = 0;

public:
    actor_id id;
    std::shared_ptr<ActorControlBlock> ctrl_block;

    // 【核心修复】：用一个强引用自身指针来“锁住”生命周期
    // 类似于 CAF 内部的初始强引用计数，确保 spawn 完不会立刻死掉
    std::shared_ptr<AbstractActor> keep_alive;

    explicit AbstractActor(actor_id aid)
        : id(aid)
    {
        ctrl_block = std::make_shared<ActorControlBlock>(aid);
    }

    virtual ~AbstractActor() { std::cout << "[System] Actor #" << id << " has been destructed.\n"; }

    void init()
    {
        ctrl_block->actor_impl = shared_from_this();
        // 激活生命周期保护，让对象自己持有一个自己的强引用
        keep_alive             = shared_from_this();
    }

    // 显式退出函数：解除自引用，允许 Actor 被析构
    void quit()
    {
        std::cout << "[Actor #" << id << "] Explicitly quitting...\n";
        keep_alive.reset();
    }

    // 接收消息入队
    void receive(Message msg)
    {
        std::lock_guard<std::mutex> lock(mailbox_mtx);
        mailbox.push(std::move(msg));

        // 如果当前没有被调度，则通知调度器投递到线程池
        if (!is_scheduled.exchange(true)) {
            if (g_scheduler) {
                g_scheduler->schedule(shared_from_this());
            }
        }
    }

    // 由调度线程池中的某个线程调用，批量消耗邮箱消息
    void consume_messages()
    {
        std::queue<Message> local_queue;
        {
            std::lock_guard<std::mutex> lock(mailbox_mtx);
            std::swap(mailbox, local_queue);
            is_scheduled.store(false); // 重置调度标记
        }

        // 在无锁临界区保护的情况下，安全地处理消息
        while (!local_queue.empty()) {
            on_receive(local_queue.front());
            local_queue.pop();
        }
    }
};

void Scheduler::worker_thread()
{
    while (!stop) {
        std::shared_ptr<AbstractActor> actor = nullptr;
        {
            std::unique_lock<std::mutex> lock(queue_mtx);
            cv.wait(lock, [this] { return !ready_queue.empty() || stop; });
            if (stop && ready_queue.empty())
                return;
            actor = std::move(ready_queue.front());
            ready_queue.pop();
        }
        if (actor) {
            // 执行 Actor 的消息处理逻辑
            actor->consume_messages();
        }
    }
}

// 完善控制块投递逻辑
bool ActorControlBlock::enqueue(Message msg)
{
    if (auto ptr = actor_impl.lock()) {
        ptr->receive(std::move(msg));
        return true;
    }
    return false;
}

// ==========================================
// 4. 全局注册表
// ==========================================
class ActorRegistry
{
private:
    std::unordered_map<actor_id, std::shared_ptr<ActorControlBlock>> registry;
    std::shared_mutex mtx;
    std::atomic<actor_id> id_counter{1};

public:
    template<typename T, typename... Args>
    ActorHandle spawn(Args&&... args)
    {
        actor_id new_id = id_counter.fetch_add(1);
        auto actor_ptr  = std::make_shared<T>(new_id, std::forward<Args>(args)...);
        actor_ptr->init();

        std::unique_lock<std::shared_mutex> lock(mtx);
        registry[new_id] = actor_ptr->ctrl_block;

        return ActorHandle(actor_ptr->ctrl_block);
    } // <--- 【修复生效】：即使 actor_ptr 局部变量在这里销毁，因为 actor_ptr->keep_alive 内部存了强引用，Actor 实体依然稳稳地活在堆内存中！

    ActorHandle get(actor_id id)
    {
        std::shared_lock<std::shared_mutex> lock(mtx);
        auto it = registry.find(id);
        if (it != registry.end())
            return ActorHandle(it->second);
        return ActorHandle();
    }

    void erase(actor_id id)
    {
        std::unique_lock<std::shared_mutex> lock(mtx);
        registry.erase(id);
    }
};

// ==========================================
// 5. 业务具体实现：实现自定义的 MyWorker
// ==========================================
class MyWorker : public AbstractActor
{
public:
    using AbstractActor::AbstractActor;

protected:
    void on_receive(const Message& msg) override
    {
        if (msg.content.type() == typeid(std::string)) {
            std::string text = std::any_cast<std::string>(msg.content);
            std::cout << "[MyWorker线程 " << std::this_thread::get_id() << "] 处理 Actor #" << id
                      << " 的消息: " << text << "\n";

            if (text == "SHUTDOWN") {
                this->quit(); // 触发退出
            }
        }
    }
};

// ==========================================
// 6. 测试运行
// ==========================================
int main()
{
    // 初始化一个拥有 2 个工作线程的调度器
    Scheduler scheduler(2);
    g_scheduler = &scheduler;

    ActorRegistry registry;

    std::cout << "--- 1. 创建 Actor (此时 spawn 结束后不会立刻析构了) ---\n";
    ActorHandle worker = registry.spawn<MyWorker>();
    actor_id saved_id  = worker->id;

    std::cout << "\n--- 2. 异步投递消息 ---\n";
    worker->enqueue(Message{std::string("Hello, Actor Framework!")});
    worker->enqueue(Message{std::string("Processing second heavy task...")});

    // 主线程稍微等一下，让后台工作线程有时间打印
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    std::cout << "\n--- 3. 模拟注销寻址，并发送关闭信号 ---\n";
    registry.erase(saved_id); // 仅仅从注册表移除，Actor 实体依然由于 keep_alive 活着

    worker->enqueue(Message{std::string("SHUTDOWN")}); // 发送退出命令

    // 等待 Actor 真正析构完成
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    std::cout << "\n--- Main 线程结束 ---\n";
    return 0;
}
