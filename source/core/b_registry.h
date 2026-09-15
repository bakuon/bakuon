#pragma once

#include <algorithm>
#include <cstddef>
#include <functional>
#include <memory>
#include <utility>
#include <vector>

#include <entt/entity/entity.hpp>
#include <entt/entity/registry.hpp>
#include <entt/signal/dispatcher.hpp>

#include "core/b_connection.h"
#include "core/b_handle.h"

namespace bakuon::core {

/**
 * @brief GUI 无关的实体-组件状态容器 —— 对 entt::registry 的一层薄封装。
 *
 * @details 面向"复杂 GUI 状态的底层数据驱动"这个定位设计：文档/场景里的每一个
 * "对象"（图层、节点、场景实体、工具面板条目……）对应一个 Handle，具体属性
 * 以组件（Component，一个普通的 C++ 结构体，无需继承任何基类）挂载/摘除，
 * 而不是让每个业务对象各自演化出一份"哪些字段变了要通知谁"的手写逻辑。
 *
 * 为什么选择 ECS（准确地说是 EnTT 的"稀疏集存储"模型）而不是继续用传统的
 * "每个业务对象一个 C++ 类 + QObject 信号"：
 *  - 数据和行为分离：Position/Visibility/Selected/Name 这类属性本身只是数据，
 *    不需要每种组合都单独定义一个类；新增一种属性组合不需要新增类型体系。
 *  - 变化可观察、可批量处理：onConstruct()/onUpdate()/onDestroy() 提供统一的
 *    "某类组件发生了变化"通知点，GUI 层（gui::CommandSystem/ContextArbiter，
 *    或具体的 QAbstractItemModel 适配器）在这一个地方决定如何映射成界面刷新，
 *    不需要在业务对象内部散落大量 QObject::connect。
 *  - 与具体 GUI 技术栈解耦：core 完全不知道 Qt 的存在（这是本模块存在的前提），
 *    使得同一套状态模型未来可以被非 GUI 场景复用（命令行批处理、单元测试、
 *    未来可能的非 Qt 前端），gui 层只是众多消费者之一。
 *
 * ## 使用方式（典型场景）
 * @code
 *   bakuon::core::Registry registry;
 *
 *   struct Position { float x = 0, y = 0; };
 *   struct Selected {}; // 空结构体作为"标签组件"（tag component），只表达"有/无"
 *
 *   const auto node = registry.create();
 *   registry.emplace<Position>(node, 10.f, 20.f);
 *
 *   // GUI 层订阅：任何实体被打上 Selected 标签时，刷新对应的界面高亮
 *   auto conn = registry.onConstruct<Selected>([](Registry&, Handle handle) {
 *       // ... 通知 gui 层 handle 对应的界面元素需要高亮
 *   });
 *
 *   registry.emplace<Selected>(node); // 触发上面的回调
 * @endcode
 *
 * ## 线程模型
 * 与 entt::registry 本身完全一致：不是线程安全的。本类只应该在单一线程（通常是
 * GUI 主线程）里使用；跨线程访问需要调用方自己做同步，本类内部不加任何锁。
 *
 * ## 与 include/bakuon/core/ 门面的关系
 * 本类是内部实现（b_ 前缀），但由于 Registry 的公开接口全是模板（EnTT 本身就是
 * header-only 模板库），没有跨动态库边界的符号导出问题——core 目前也仍然是
 * STATIC 库，不像 gui 那样需要 BAKUON_GUI_EXPORT。include/bakuon/core/Registry.h
 * 只是原样转发本文件，不再另外包一层适配代码，这一点在该文件顶部也有说明。
 */
class Registry
{
public:
    Registry()  = default;
    ~Registry() = default;

    // 不可拷贝、不可移动：
    //  - 不可拷贝的原因见下方注释（entt::registry 拷贝语义代价高昂且容易被误用）；
    //  - 不可移动是因为 bindSink() 里每个 Slot<T> 在构造时会捕获 `this`（见
    //    Slot::owner），一旦 Registry 被移动，所有已经存在的订阅（包括尚未触发过
    //    的、以及被 dismiss() 长期挂靠在 m_pinnedSlots 里的）里保存的 owner 指针
    //    都会指向移动前的旧地址，变成悬空指针——与 gui::CommandWorkspace 出于类似
    //    理由禁止移动是同一类考量（见 source/gui/b_commandsystem.h）。需要"延迟
    //    构造 Registry"的场景请用 std::unique_ptr<Registry> 或 std::optional<Registry>
    //    间接持有，而不是依赖 Registry 自身的移动语义。
    Registry(const Registry&)            = delete;
    Registry& operator=(const Registry&) = delete;
    Registry(Registry&&)                 = delete;
    Registry& operator=(Registry&&)      = delete;

    /// 创建一个新的空实体（不携带任何组件）。
    [[nodiscard]] Handle create() { return Handle{m_registry.create()}; }

    /// 销毁一个实体及其携带的全部组件；对已经无效的 handle 是安全的空操作。
    void destroy(Handle handle)
    {
        if (valid(handle)) {
            m_registry.destroy(handle.native());
        }
    }

    /// 该 handle 当前是否指向一个存活的实体（默认构造的 Handle 恒返回 false）。
    [[nodiscard]] bool valid(Handle handle) const noexcept
    {
        return handle.isValid() && m_registry.valid(handle.native());
    }

    /**
     * @brief 为实体挂载一个组件。
     * @warning 若该实体已经携带同类型组件，行为未定义（与 entt 语义一致）；
     *          不确定是否已存在时请用 emplaceOrReplace()。
     * @note 返回类型用 decltype(auto) 而非固定的 T&：EnTT 对"空组件"（tag component，
     *       如本文件顶部 Registry 类文档里的 Selected 例子）做了专门优化，这类组件
     *       不占用任何存储空间，emplace() 对它们返回 void 而不是 T&——固定写
     *       T& 会导致挂载空结构体标签组件时编译失败。
     */
    template<typename T, typename... Args>
    decltype(auto) emplace(Handle handle, Args&&... args)
    {
        return m_registry.emplace<T>(handle.native(), std::forward<Args>(args)...);
    }

    /// 挂载或替换：组件不存在则创建，已存在则整体替换为新构造的值。
    /// 返回类型同样是 decltype(auto)，原因见 emplace() 的说明。
    template<typename T, typename... Args>
    decltype(auto) emplaceOrReplace(Handle handle, Args&&... args)
    {
        return m_registry.emplace_or_replace<T>(handle.native(), std::forward<Args>(args)...);
    }

    /// 该实体是否携带指定组件类型。
    template<typename... Ts>
    [[nodiscard]] bool has(Handle handle) const
    {
        return m_registry.template all_of<Ts...>(handle.native());
    }

    template<typename... Ts>
    [[nodiscard]] bool hasAny(Handle handle) const
    {
        return m_registry.template any_of<Ts...>(handle);
    }

    template<typename T, typename... Args>
    [[nodiscard]] decltype(auto) getOrEmplace(Handle handle, Args&&... args)
    {
        return m_registry.get_or_emplace<T>(handle.native(), std::forward<Args>(args)...);
    }

    template<typename... Ts>
    [[nodiscard]] decltype(auto) get(Handle handle)
    {
        return m_registry.template get<Ts...>(handle.native());
    }

    template<typename... Ts>
    [[nodiscard]] decltype(auto) get(Handle handle) const
    {
        return m_registry.template get<Ts...>(handle.native());
    }

    /// 取组件指针；不存在（或 handle 本身无效）时返回 nullptr，不抛异常、不断言。
    template<typename... Ts>
    [[nodiscard]] auto tryGet(Handle handle)
    {
        return m_registry.template try_get<Ts...>(handle.native());
    }
    template<typename... Ts>
    [[nodiscard]] auto tryGet(Handle handle) const
    {
        return m_registry.template try_get<Ts...>(handle.native());
    }

    /**
     * @brief 原地修改一个已存在的组件，并触发 onUpdate() 订阅者。
     * @param func 签名为 void(T&)，在锁保护范围之外直接修改组件本身。
     * @warning 若该实体当前不携带这个组件，行为未定义（与 entt::patch 语义一致）；
     *          不确定是否已存在时，请先用 has<T>()/tryGet<T>() 判断。
     *
     * 这是"属性编辑器改了一个字段，界面需要联动刷新"这类场景的推荐入口：
     * 相比 emplaceOrReplace()（整体替换成一个新构造的值），patch() 允许只改
     * 组件的某一个字段，同时仍然会触发同一套 onUpdate() 通知。
     */
    template<typename T, typename Func>
    void patch(Handle handle, Func&& func)
    {
        m_registry.patch<T>(handle.native(), std::forward<Func>(func));
    }

    /// 摘除组件；组件本就不存在时是安全的空操作（不同于 erase()，这里刻意选择
    /// 更宽松的语义，避免 GUI 层调用方需要每次都先 has() 判断一遍）。
    template<typename T>
    void remove(Handle handle)
    {
        m_registry.remove<T>(handle.native());
    }

    /**
     * @brief 遍历所有同时携带 Components... 的实体，回调固定接收 Handle 作为第一个参数：
     *        Func 签名为 void(Handle, Components&...)（空结构体标签组件除外，
     *        见下方说明）。
     *
     * @note 这里没有直接把调用方的 Func 转发给 entt::basic_view::each()，是刻意的：
     *       entt 内部通过"Func 能否用裸 entt::entity 调用"来判断要不要把实体传给
     *       回调（is_applicable_v 探测），而 Handle 的转换构造函数是 explicit 的
     *       （见 b_entity.h，故意不允许从 entt::entity 隐式转换），这个探测永远会
     *       失败，entt 会静默地把实体参数从调用里去掉，导致调用方按"第一个参数是
     *       Handle"写的回调在运行期实参个数对不上。这里改为自己包一层只接受裸
     *       entt::entity 的胶水 lambda（满足 entt 的探测条件），拿到实体后才转换成
     *       Handle 再转发给调用方的 Func——调用方因此完全不需要知道 entt 这层探测
     *       逻辑的存在，contract 始终是"Handle 必须作为第一个参数"。
     * @note 空结构体"标签组件"（如 Selected，见类文档顶部示例）不占用存储空间，
     *       entt 不会为它在参数列表里传出引用——用 each<Position, Selected>()
     *       筛选"同时携带两者"的实体时，回调签名仍然只需要写 (Handle, Position&)，
     *       不需要（也不能）再多写一个 Selected& 形参。
     */
    template<typename... Components, typename Func,
             typename = std::enable_if_t<(sizeof...(Components) > 0)>>
    void each(Func&& func)
    {
        m_registry.view<Components...>().each([&func](entt::entity entity, auto&&... comps) {
            std::invoke(std::forward<Func>(func),
                        Handle{entity},
                        std::forward<decltype(comps)>(comps)...);
        });
    }

    template<typename Func>
    void each(Func&& func)
    {
        m_registry.view<entt::entity>().each(
            [&func](const entt::entity entity) { std::invoke(func, Handle{entity}); });
    }

    template<typename... Components, typename Func,
             typename = std::enable_if_t<(sizeof...(Components) > 0)>>
    void each(Func&& func) const
    {
        // 关键点：将传入的 Components... 映射为 const Components...
        // 这样可以确保即使外部传入 each<Position>，在 const 注册表下也能正确生成 view<const Position>
        m_registry.view<std::add_const_t<Components>...>().each(
            [&func](const entt::entity entity, auto&&... comps) {
                std::invoke(std::forward<Func>(func),
                            Handle{entity},
                            std::forward<decltype(comps)>(comps)...);
            });
    }

    template<typename Func>
    void each(Func&& func) const
    {
        m_registry.view<entt::entity>().each(
            [&func](const entt::entity entity) { std::invoke(func, Handle{entity}); });
    }

    /**
     * @brief 订阅"T 类型组件被 emplace 到某实体上"这一事件。
     * @param handler 签名为 void(Registry&, Handle)；回调触发时组件已经挂载完毕，
     *                可以在回调里安全地 tryGet<T>() 读取它的值。
     * @return RAII 连接守卫；守卫存活期间订阅持续有效，析构/dismiss() 见 Connection。
     */
    template<typename T, typename Handler>
    [[nodiscard]] Connection onConstruct(Handler&& handler)
    {
        return bindSink<T>(m_registry.template on_construct<T>(), std::forward<Handler>(handler));
    }

    /// 订阅"T 类型组件被原地修改（registry.patch<T>()/replace<T>()）"事件，语义同上。
    template<typename T, typename Handler>
    [[nodiscard]] Connection onUpdate(Handler&& handler)
    {
        return bindSink<T>(m_registry.template on_update<T>(), std::forward<Handler>(handler));
    }

    /// 订阅"T 类型组件即将从某实体移除"事件（回调触发时组件仍然存在，可读取最后一次的值）。
    template<typename T, typename Handler>
    [[nodiscard]] Connection onDestroy(Handler&& handler)
    {
        return bindSink<T>(m_registry.template on_destroy<T>(), std::forward<Handler>(handler));
    }

    /// 前置声明；完整定义在本文件末尾（class Registry::BatchGuard），因为它
    /// 需要调用 beginBatch()/endBatch()，必须等 Registry 的完整定义之后才能写。
    class BatchGuard;

    /**
     * @brief 开启一次批次：批次期间，同一个 (订阅, 实体) 组合无论触发多少次
     * onConstruct/onUpdate/onDestroy，都只会在批次结束（配对的 endBatch()
     * 让批次深度归零）时最多回调一次——用于"一次用户操作改了好几个组件"
     * 这类场景（拖拽同时改 X/Y/Rotation），避免观察者收到成串的中间态通知、
     * GUI 抖动式地反复刷新。
     *
     * 支持嵌套调用（beginBatch() 计数、endBatch() 递减，只有回到 0 才真正
     * flush），方便"高层批次里嵌套低层批次"的组合场景——比如一次"拖拽结束"
     * 的大批次内部，每一帧鼠标移动各自也走一次小批次，只有最外层结束时才
     * 真正通知观察者。
     *
     * @warning 当前实现按"信号类型独立去重"：onConstruct/onUpdate/onDestroy
     * 各自的订阅分别去重合并，不做跨信号类型的净效应折叠——如果同一个实体的
     * 同一个组件在同一批次内先 emplace() 又被 remove()，onConstruct 和
     * onDestroy 两边的订阅仍然都会各自触发一次（尽管净效果其实什么也没变），
     * 这是刻意简化的 v1 行为边界，不是遗漏。
     * @note 推荐配合 BatchGuard 使用而不是直接调用 beginBatch()/endBatch()，
     * 后者要求调用方自己保证异常安全下的严格配对。
     */
    void beginBatch() noexcept { ++m_batchDepth; }

    /// 结束一次批次；对没有匹配 beginBatch() 的调用是安全的空操作。批次深度
    /// 归零时才真正把本批次内积攒的通知一次性 flush 给各自的订阅者。
    void endBatch()
    {
        if (m_batchDepth == 0) {
            return;
        }
        if (--m_batchDepth == 0) {
            flushPendingNotifications();
        }
    }

    [[nodiscard]] bool isBatching() const noexcept { return m_batchDepth > 0; }

    /// 逃生舱口：极少数需要直接使用 entt 原生 API（比如 entt::organizer、
    /// 自定义 view 组合）的场景可以拿到底层 entt::registry；日常业务代码应优先
    /// 使用本类已封装的接口，不要绕开它直接操作 native()——那样会让"core 不依赖
    /// Qt、不泄漏 entt 类型"的边界形同虚设。
    [[nodiscard]] entt::registry& native() noexcept { return m_registry; }
    [[nodiscard]] const entt::registry& native() const noexcept { return m_registry; }

private:
    // entt 的 sink::connect<&Fn>(instance) 要求一个具名成员函数 + 一个具体实例地址；
    // 我们希望消费者能传任意可调用对象（lambda/std::function），因此需要一个稳定的
    // "跳板"：把消费者的 handler 存进一个堆上分配的 Slot，用 Slot::invoke 去满足
    // entt 的连接签名，Slot 的生命周期由返回的 Connection（内部用 shared_ptr 延长）
    // 接管——这样消费者完全不需要知道这层间接。
    //
    // Slot 同时也是批次（beginBatch()/endBatch()）去重的落点：批次期间 invoke()
    // 不立即触发 callback，而是把 Handle 记进 pending（去重），并且只在"本 Slot
    // 本批次第一次"产生 pending 项时，把自己的 flush() 登记进 Registry 的
    // 待冲洗列表——避免每次事件都重复登记同一个 Slot。
    template<typename T>
    struct Slot : std::enable_shared_from_this<Slot<T>>
    {
        std::function<void(Registry&, Handle)> callback;
        Registry* owner = nullptr;
        std::vector<Handle> pending;  // 当前批次内已经去重的待通知列表
        bool flushRegistered = false; // 本批次内是否已经把 flush() 登记给了 owner

        void invoke(entt::registry&, entt::entity entity)
        {
            if (!owner) {
                return;
            }
            const Handle id{entity};

            if (!owner->isBatching()) {
                if (callback) {
                    callback(*owner, id);
                }
                return;
            }

            if (std::find(pending.begin(), pending.end(), id) == pending.end()) {
                pending.push_back(id);
            }
            if (!flushRegistered) {
                flushRegistered = true;
                // 捕获 weak_ptr 而不是裸 this：如果调用方在本批次结束之前就
                // 显式 disconnect() 了这次订阅，Connection::disconnect() 会
                // 同时释放 disconnector/onDismiss 两个闭包各自持有的
                // shared_ptr<Slot> 副本——一旦这是最后一份引用，Slot 会在
                // disconnect() 内部同步被销毁，而这里登记进 Registry 的 flush
                // 回调却要等到 endBatch() 才会被调用，中间这段时间窗口如果
                // 只捕获裸指针，flush() 执行时就是一次悬空访问。weak_ptr +
                // lock() 让这种情况下的 flush 安全地变成空操作，而不是崩溃。
                owner->registerPendingFlush([weak = this->weak_from_this()]() {
                    if (auto locked = weak.lock()) {
                        locked->flush();
                    }
                });
            }
        }

        void flush()
        {
            flushRegistered = false;
            std::vector<Handle> toNotify;
            toNotify.swap(pending);
            if (!callback) {
                return;
            }
            for (Handle id : toNotify) {
                callback(*owner, id);
            }
        }
    };

    template<typename T, typename Sink, typename Handler>
    [[nodiscard]] Connection bindSink(Sink sink, Handler&& handler)
    {
        auto slot      = std::make_shared<Slot<T>>();
        slot->callback = std::forward<Handler>(handler);
        slot->owner    = this;

        entt::connection raw = sink.template connect<&Slot<T>::invoke>(*slot);
        // 正常断开路径：raw.release() 先让 entt 忘掉这次连接，随后这个 lambda
        // 自己持有的 shared_ptr 副本析构，slot 被安全释放——顺序上必须先断开
        // 再释放，否则 entt 内部仍持有的裸指针会变成悬空指针。
        auto disconnector    = [raw, slot]() mutable { raw.release(); };
        // dismiss 路径：不断开、也不释放，而是把 slot 的所有权交给 Registry
        // 自己保管（m_pinnedSlots），让它至少活到 Registry 析构为止——
        // 具体动机和"为什么不能简单地什么都不做"见 Connection 类文档。
        auto onDismiss       = [this, slot]() mutable { m_pinnedSlots.push_back(std::move(slot)); };
        return Connection(std::move(disconnector), std::move(onDismiss));
    }

    /// bindSink()/Slot<T>::invoke() 共用：把"本 Slot 需要 flush"登记进批次待办列表。
    void registerPendingFlush(std::function<void()> flush)
    {
        m_pendingFlushes.push_back(std::move(flush));
    }

    /// endBatch() 批次深度归零时调用：把本批次内积攒的全部 flush 回调依次执行。
    void flushPendingNotifications()
    {
        // 先整体搬空再执行：flush() 内部触发的用户回调完全可能反过来又调用
        // beginBatch()/endBatch()，或者产生新的组件变更（进而在 isBatching()
        // 为 false 的这一刻——注意 flush 期间 m_batchDepth 已经是 0——直接同步
        // 触发别的 Slot::invoke()，走的是"立即回调"分支，不会再次污染本次
        // 正在遍历的 pending 列表）。即便如此，先搬空再遍历仍然是防御性的
        // 最佳实践，与仓库别处 PluginSystem::idSnapshot() 等既有模式一致。
        std::vector<std::function<void()>> pending;
        pending.swap(m_pendingFlushes);
        for (auto& flush : pending) {
            flush();
        }
    }

private:
    entt::registry m_registry;
    // entt::dispatcher m_dispatcher;

    // 被 dismiss() 的连接背后的 Slot<T> 在此长期挂靠，见 bindSink() 的说明；
    // 类型擦除成 shared_ptr<void> 是因为不同 T 对应不同的 Slot<T> 特化，
    // 这里不需要、也不应该关心具体是哪一种。
    std::vector<std::shared_ptr<void>> m_pinnedSlots;
    std::size_t m_batchDepth = 0;
    std::vector<std::function<void()>> m_pendingFlushes;
};

/**
 * @brief beginBatch()/endBatch() 的 RAII 包装：构造时开启批次，析构时结束批次
 * （无论是正常离开作用域还是异常展开都会执行），避免调用方自己配对
 * beginBatch()/endBatch() 时因为提前 return/抛异常而忘记 endBatch()。
 *
 * @code
 *   {
 *       Registry::BatchGuard batch(registry);
 *       registry.patch<Position>(node, [](Position& p) { p.x += dx; });
 *       registry.patch<Position>(node, [](Position& p) { p.y += dy; });
 *       registry.patch<Rotation>(node, [](Rotation& r) { r.angle += dr; });
 *   } // 离开作用域：批次结束，onUpdate<Position>/onUpdate<Rotation> 各自最多回调一次
 * @endcode
 */
class Registry::BatchGuard
{
public:
    explicit BatchGuard(Registry& registry) noexcept
        : m_registry(&registry)
    {
        m_registry->beginBatch();
    }

    ~BatchGuard() { dismiss_and_end(); }

    BatchGuard(const BatchGuard&)            = delete;
    BatchGuard& operator=(const BatchGuard&) = delete;

    BatchGuard(BatchGuard&& other) noexcept
        : m_registry(other.m_registry)
    {
        other.m_registry = nullptr;
    }
    BatchGuard& operator=(BatchGuard&& other) noexcept
    {
        if (this != &other) {
            dismiss_and_end();
            m_registry       = other.m_registry;
            other.m_registry = nullptr;
        }
        return *this;
    }

private:
    void dismiss_and_end()
    {
        if (m_registry) {
            m_registry->endBatch();
            m_registry = nullptr;
        }
    }

    Registry* m_registry;
};

} // namespace bakuon::core
