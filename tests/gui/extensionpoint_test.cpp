/**
 * @file extension_point_variants_test.cpp
 * @brief GoogleTest 测试用例：ScopedExtensionPoint / DeferredExtensionPoint / AsyncExtensionPoint
 *
 * 编译要求：C++20，链接 pthread，建议开启 ASan+UBSan。
 *  CMake 已配置 ENABLE_SANITIZERS=ON 自动附加 -fsanitize=address,undefined。
 */

#include <gui/b_asyncextensionpoint.h>
#include <gui/b_defaultextensionpoint.h>
#include <gui/b_deferredextensionpoint.h>
#include <gui/b_extensionsystem.h>
#include <gui/b_scopedextensionpoint.h>

#include <atomic>
#include <chrono>
#include <string>
#include <thread>

#include <gtest/gtest.h>

using namespace bakuon::gui;

/* ============================================================
 *  测试接口定义
 * ============================================================ */

class IGreeter
{
public:
    virtual ~IGreeter()                                = default;
    virtual std::string greet(const std::string& name) = 0;
};
BAKUON_DECLARE_EXTENSION_IID(IGreeter, "com.test.IGreeter")

class HelloGreeter final : public IGreeter
{
public:
    std::string greet(const std::string& name) override { return "Hello, " + name; }
};

class HiGreeter final : public IGreeter
{
public:
    std::string greet(const std::string& name) override { return "Hi, " + name; }
};

class LazyCounter final : public IGreeter
{
public:
    explicit LazyCounter(std::atomic<int>* counter)
        : m_counter(counter)
    {
    }
    std::string greet(const std::string& name) override
    {
        ++(*m_counter);
        return "Lazy(" + name + ")";
    }
    std::atomic<int>* m_counter;
};

/* ============================================================
 *  1) ScopedExtensionPoint 测试
 * ============================================================ */

enum class ProjectKind { Cpp, Rust, Python };

class ScopedFilterTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        ep = std::make_shared<ScopedExtensionPoint<IGreeter, ProjectKind>>(
            "com.test.greeter.scoped", "Scoped greeter");
        hello = std::make_shared<HelloGreeter>();
        hi    = std::make_shared<HiGreeter>();
    }
    std::shared_ptr<ScopedExtensionPoint<IGreeter, ProjectKind>> ep;
    std::shared_ptr<IGreeter> hello;
    std::shared_ptr<IGreeter> hi;
};

TEST_F(ScopedFilterTest, RegisterGlobalVisibleToAllScopes)
{
    EXPECT_TRUE(ep->registerExtension(hello, 50));
    EXPECT_EQ(ep->count(), 1u);
    // 全局扩展在任何作用域都可见
    auto listCpp  = ep->extensionsIn(ProjectKind::Cpp);
    auto listRust = ep->extensionsIn(ProjectKind::Rust);
    EXPECT_EQ(listCpp.size(), 1u);
    EXPECT_EQ(listRust.size(), 1u);
}

TEST_F(ScopedFilterTest, RegisterScopedVisibility)
{
    EXPECT_TRUE(ep->registerExtension(hello, 100, ProjectKind::Cpp));
    EXPECT_TRUE(ep->registerExtension(hi, 50, ProjectKind::Rust));
    // 无全局扩展时
    EXPECT_EQ(ep->extensionsIn(ProjectKind::Cpp).size(), 1u);
    EXPECT_EQ(ep->extensionsIn(ProjectKind::Rust).size(), 1u);
    EXPECT_EQ(ep->extensionsIn(ProjectKind::Python).size(), 0u);
    // 全局查询返回全部
    EXPECT_EQ(ep->extensions().size(), 2u);
}

TEST_F(ScopedFilterTest, MultipleScopesInitializerList)
{
    EXPECT_TRUE(ep->registerExtension(hello, 100, {ProjectKind::Cpp, ProjectKind::Rust}));
    EXPECT_EQ(ep->extensionsIn(ProjectKind::Cpp).size(), 1u);
    EXPECT_EQ(ep->extensionsIn(ProjectKind::Rust).size(), 1u);
    EXPECT_EQ(ep->extensionsIn(ProjectKind::Python).size(), 0u);
}

TEST_F(ScopedFilterTest, ExtensionsInAll_AND_Semantics)
{
    EXPECT_TRUE(ep->registerExtension(hello, 100, {ProjectKind::Cpp, ProjectKind::Rust}));
    auto both = ep->extensionsInAll({ProjectKind::Cpp, ProjectKind::Rust});
    EXPECT_EQ(both.size(), 1u);
    auto cppOnly = ep->extensionsInAll({ProjectKind::Cpp, ProjectKind::Python});
    EXPECT_EQ(cppOnly.size(), 0u);
}

TEST_F(ScopedFilterTest, ExtensionsInAny_OR_Semantics)
{
    EXPECT_TRUE(ep->registerExtension(hello, 100, ProjectKind::Cpp));
    auto any = ep->extensionsInAny({ProjectKind::Cpp, ProjectKind::Python});
    EXPECT_EQ(any.size(), 1u);
    auto none = ep->extensionsInAny({ProjectKind::Python, ProjectKind::Rust});
    EXPECT_EQ(none.size(), 0u);
}

TEST_F(ScopedFilterTest, GlobalAlwaysVisibleInFilteredQueries)
{
    EXPECT_TRUE(ep->registerExtension(hello, 10)); // 全局
    EXPECT_TRUE(ep->registerExtension(hi, 100, ProjectKind::Cpp));
    auto list = ep->extensionsIn(ProjectKind::Cpp);
    EXPECT_EQ(list.size(), 2u);
    list = ep->extensionsIn(ProjectKind::Rust);
    EXPECT_EQ(list.size(), 1u);
}

TEST_F(ScopedFilterTest, PriorityOrdering)
{
    auto a = std::make_shared<HiGreeter>();
    auto b = std::make_shared<HelloGreeter>();
    ep->registerExtension(a, 50, ProjectKind::Cpp);
    ep->registerExtension(b, 100, ProjectKind::Cpp);
    auto list = ep->extensionsIn(ProjectKind::Cpp);
    ASSERT_EQ(list.size(), 2u);
    EXPECT_EQ(list[0], b); // 高优先级在前
    EXPECT_EQ(list[1], a);
}

TEST_F(ScopedFilterTest, TryExtensionsIn_Chain)
{
    // Cpp 作用域下 Hello 先返回结果
    ep->registerExtension(hi, 50, ProjectKind::Rust); // Rust 专用
    ep->registerExtension(hello, 100, ProjectKind::Cpp);
    std::string result = ep->tryExtensionsIn<std::string>(
        ProjectKind::Cpp, [&](const auto& ext) { return ext->greet("Bob"); }, std::string{});
    EXPECT_EQ(result, "Hello, Bob");

    // Rust 作用域下只有 Hi 可用
    result = ep->tryExtensionsIn<std::string>(
        ProjectKind::Rust, [&](const auto& ext) { return ext->greet("Bob"); }, std::string{});
    EXPECT_EQ(result, "Hi, Bob");

    // Python 下无扩展
    result = ep->tryExtensionsIn<std::string>(
        ProjectKind::Python, [&](const auto& ext) { return ext->greet("Bob"); }, std::string{});
    EXPECT_TRUE(result.empty());
}

TEST_F(ScopedFilterTest, UnregisterRemovesFromScopes)
{
    ep->registerExtension(hello, 100, ProjectKind::Cpp);
    EXPECT_EQ(ep->extensionsIn(ProjectKind::Cpp).size(), 1u);
    EXPECT_TRUE(ep->unregisterExtension(hello));
    EXPECT_EQ(ep->extensionsIn(ProjectKind::Cpp).size(), 0u);
    EXPECT_FALSE(ep->unregisterExtension(hello));
}

TEST_F(ScopedFilterTest, RejectDuplicate)
{
    EXPECT_TRUE(ep->registerExtension(hello, 100));
    EXPECT_FALSE(ep->registerExtension(hello, 100)); // 重复
    EXPECT_EQ(ep->count(), 1u);
}

TEST_F(ScopedFilterTest, StringScope)
{
    ScopedExtensionPoint<IGreeter, std::string> ep2("id", "string scoped");
    ep2.registerExtension(hello, 100, "admin");
    ep2.registerExtension(hi, 50); // 全局
    EXPECT_EQ(ep2.extensionsIn("admin").size(), 2u);
    EXPECT_EQ(ep2.extensionsIn("guest").size(), 1u);
}

/* ============================================================
 *  2) DeferredExtensionPoint 测试
 * ============================================================ */

class LazyFactoryTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        ep      = std::make_shared<DeferredExtensionPoint<IGreeter>>("com.test.greeter.lazy",
                                                                     "Lazy greeter");
        counter = 0;
    }
    std::shared_ptr<DeferredExtensionPoint<IGreeter>> ep;
    std::atomic<int> counter;
};

TEST_F(LazyFactoryTest, RegisterInstanceDirectly)
{
    auto h = std::make_shared<HelloGreeter>();
    EXPECT_TRUE(ep->registerExtension(h, 100));
    EXPECT_EQ(ep->instantiatedCount(), 1u); // 已实例化
    EXPECT_EQ(ep->count(), 1u);
}

TEST_F(LazyFactoryTest, SingletonFactory_InstantiatedOnce)
{
    ep->registerFactory([&] { return std::make_shared<LazyCounter>(&counter); },
                        100,
                        extension::DeferredPolicy::Singleton);
    EXPECT_EQ(ep->instantiatedCount(), 0u); // 尚未实例化
    // 注意：LazyCounter 构造本身不递增 counter，greet() 才递增
    auto list = ep->extensions();
    ASSERT_EQ(list.size(), 1u);
    EXPECT_EQ(ep->instantiatedCount(), 1u);
    list[0]->greet("a");
    EXPECT_EQ(counter.load(), 1); // greet 触发 +1

    // 再次调用 extensions 不再调用工厂（同一 shared_ptr）
    auto list2 = ep->extensions();
    EXPECT_EQ(list2[0], list[0]);
    list2[0]->greet("b");
    EXPECT_EQ(counter.load(), 2); // 只多了一次 greet，没有再构造
}

TEST_F(LazyFactoryTest, NewInstanceFactory_CreatesEachTime)
{
    ep->registerFactory([&] { return std::make_shared<LazyCounter>(&counter); },
                        100,
                        extension::DeferredPolicy::Transient);
    auto list1 = ep->extensions();
    list1[0]->greet("a");
    auto list2 = ep->extensions();
    list2[0]->greet("b");
    // 每次调用 extensions 都调用工厂，所以 cached 永远为 null
    EXPECT_EQ(ep->instantiatedCount(), 0u);
    EXPECT_NE(list1[0], list2[0]);
    EXPECT_EQ(counter.load(), 2); // 两次 greet
}

TEST_F(LazyFactoryTest, TryExtensions_LazyStopsOnFirstHit)
{
    std::atomic<int> aCount{0}, bCount{0};
    ep->registerFactory(
        [&] {
            ++aCount;
            return std::make_shared<HelloGreeter>();
        },
        100,
        extension::DeferredPolicy::Transient);
    ep->registerFactory(
        [&] {
            ++bCount;
            return std::make_shared<HiGreeter>();
        },
        50,
        extension::DeferredPolicy::Transient);
    // 第一个扩展已经能返回非空字符串，第二个不应被实例化
    std::string r = ep->tryExtensions<std::string>([](const auto& ext) { return ext->greet("X"); },
                                                   std::string{});
    EXPECT_EQ(r, "Hello, X");
    EXPECT_EQ(aCount.load(), 1);
    EXPECT_EQ(bCount.load(), 0);
}

TEST_F(LazyFactoryTest, TryExtensions_FallbackThrough)
{
    std::atomic<int> bCount{0};
    // 第一个扩展返回空字符串（默认值）→ 跳过
    ep->registerFactory(
        [] {
            class EmptyGreeter : public IGreeter
            {
            public:
                std::string greet(const std::string&) override { return {}; }
            };
            return std::make_shared<EmptyGreeter>();
        },
        100,
        extension::DeferredPolicy::Transient);
    ep->registerFactory(
        [&] {
            ++bCount;
            return std::make_shared<HiGreeter>();
        },
        50,
        extension::DeferredPolicy::Transient);
    std::string r = ep->tryExtensions<std::string>([](const auto& ext) { return ext->greet("Y"); },
                                                   std::string{});
    EXPECT_EQ(r, "Hi, Y");
    EXPECT_EQ(bCount.load(), 1);
}

TEST_F(LazyFactoryTest, ClearCache_ReconstructsSingleton)
{
    ep->registerFactory([&] { return std::make_shared<LazyCounter>(&counter); },
                        100,
                        extension::DeferredPolicy::Singleton);
    auto list1 = ep->extensions();
    list1[0]->greet("a");
    EXPECT_EQ(counter.load(), 1);
    ep->clearCache();
    auto list2 = ep->extensions();
    list2[0]->greet("b");
    EXPECT_EQ(counter.load(), 2);
    EXPECT_NE(list1[0], list2[0]);
}

TEST_F(LazyFactoryTest, InstantiateAll_Warmup)
{
    ep->registerFactory(
        [&] {
            ++counter;
            return std::make_shared<HelloGreeter>();
        },
        100,
        extension::DeferredPolicy::Singleton);
    ep->registerFactory(
        [&] {
            ++counter;
            return std::make_shared<HiGreeter>();
        },
        50,
        extension::DeferredPolicy::Singleton);
    EXPECT_EQ(counter.load(), 0);
    ep->instantiateAll();
    EXPECT_EQ(counter.load(), 2);
    EXPECT_EQ(ep->instantiatedCount(), 2u);
}

TEST_F(LazyFactoryTest, RejectNullFactory)
{
    EXPECT_FALSE(ep->registerFactory(nullptr, 100));
    EXPECT_EQ(ep->count(), 0u);
}

TEST_F(LazyFactoryTest, ClearRemovesAll)
{
    ep->registerFactory([] { return std::make_shared<HelloGreeter>(); }, 100);
    ep->clear();
    EXPECT_EQ(ep->count(), 0u);
    EXPECT_EQ(ep->instantiatedCount(), 0u);
}

TEST_F(LazyFactoryTest, PriorityOrder)
{
    ep->registerFactory([] { return std::make_shared<HelloGreeter>(); }, 50);
    ep->registerFactory([] { return std::make_shared<HiGreeter>(); }, 100);
    auto list = ep->extensions();
    ASSERT_EQ(list.size(), 2u);
    EXPECT_EQ(list[0]->greet("x"), "Hi, x"); // 高优先级在前
    EXPECT_EQ(list[1]->greet("x"), "Hello, x");
}

/* ============================================================
 *  3) AsyncExtensionPoint 测试
 * ============================================================ */

class AsyncExtensionPointTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        // 使用同步执行器便于确定性测试：直接在当前线程执行
        extension::Executor syncExec = [](const std::function<void()>& task) {
            if (task)
                task();
        };
        ep = std::make_shared<AsyncExtensionPoint<IGreeter>>("com.test.greeter.async",
                                                             "Async greeter",
                                                             syncExec);
    }
    std::shared_ptr<AsyncExtensionPoint<IGreeter>> ep;
};

TEST_F(AsyncExtensionPointTest, BroadcastAsync_AllCalled)
{
    std::atomic<int> callCount{0};
    class CountingGreeter : public IGreeter
    {
    public:
        explicit CountingGreeter(std::atomic<int>* c)
            : m_c(c)
        {
        }
        std::string greet(const std::string&) override
        {
            ++(*m_c);
            return "ok";
        }
        std::atomic<int>* m_c;
    };
    ep->registerExtension(std::make_shared<CountingGreeter>(&callCount), 100);
    ep->registerExtension(std::make_shared<CountingGreeter>(&callCount), 50);

    auto futures = ep->broadcastAsync([](const auto& ext) { return ext->greet("x"); });
    ASSERT_EQ(futures.size(), 2u);
    for (auto& f : futures) {
        auto r = f.get();
        EXPECT_EQ(r, "ok");
    }
    EXPECT_EQ(callCount.load(), 2);
}

TEST_F(AsyncExtensionPointTest, BroadcastAsync_VoidReturn)
{
    std::atomic<int> sum{0};
    class AddGreeter : public IGreeter
    {
    public:
        explicit AddGreeter(std::atomic<int>* s, int v)
            : m_s(s)
            , m_v(v)
        {
        }
        std::string greet(const std::string&) override
        {
            m_s->fetch_add(m_v);
            return {};
        }
        std::atomic<int>* m_s;
        int m_v;
    };
    ep->registerExtension(std::make_shared<AddGreeter>(&sum, 1), 100);
    ep->registerExtension(std::make_shared<AddGreeter>(&sum, 2), 50);
    auto futures = ep->broadcastAsync([](const auto& ext) { (void) ext->greet(""); });
    for (auto& f : futures)
        f.get();
    EXPECT_EQ(sum.load(), 3);
}

TEST_F(AsyncExtensionPointTest, TryAsync_FirstWins_SyncExecutor)
{
    ep->registerExtension(std::make_shared<HelloGreeter>(), 100);
    ep->registerExtension(std::make_shared<HiGreeter>(), 50);
    auto fut = ep->tryAsync<std::string>([](const auto& ext) { return ext->greet("World"); },
                                         std::string{},
                                         extension::RacePolicy::FirstWins);
    // 同步执行器下应立即得到结果
    EXPECT_EQ(fut.get(), "Hello, World");
}

TEST_F(AsyncExtensionPointTest, TryAsync_PriorityOrdered_Fallback)
{
    // 第一个返回空（等于默认值）→ 走第二个
    class EmptyGreeter : public IGreeter
    {
    public:
        std::string greet(const std::string&) override { return {}; }
    };
    ep->registerExtension(std::make_shared<EmptyGreeter>(), 200);
    ep->registerExtension(std::make_shared<HiGreeter>(), 100);
    auto fut = ep->tryAsync<std::string>([](const auto& ext) { return ext->greet("Z"); },
                                         std::string{},
                                         extension::RacePolicy::PriorityOrdered);
    EXPECT_EQ(fut.get(), "Hi, Z");
}

TEST_F(AsyncExtensionPointTest, TryAsync_AllFail_ReturnsDefault)
{
    class EmptyGreeter : public IGreeter
    {
    public:
        std::string greet(const std::string&) override { return {}; }
    };
    ep->registerExtension(std::make_shared<EmptyGreeter>(), 100);
    auto fut = ep->tryAsync<std::string>([](const auto& ext) { return ext->greet("Z"); },
                                         std::string{"NOT_FOUND"},
                                         extension::RacePolicy::PriorityOrdered);
    EXPECT_EQ(fut.get(), "NOT_FOUND");
}

TEST_F(AsyncExtensionPointTest, TryAsync_EmptyPoint_ReturnsDefault)
{
    auto ep2 = std::make_shared<AsyncExtensionPoint<IGreeter>>("id2",
                                                               "empty",
                                                               [](const std::function<void()>& t) {
                                                                   if (t)
                                                                       t();
                                                               });
    auto fut = ep2->tryAsync<std::string>([](const auto&) { return "x"; },
                                          std::string{"def"},
                                          extension::RacePolicy::PriorityOrdered);
    EXPECT_EQ(fut.get(), "def");
}

TEST_F(AsyncExtensionPointTest, SetExecutorReplaces)
{
    std::atomic<int> execCount{0};
    ep->setExecutor([&](const std::function<void()>& t) {
        ++execCount;
        if (t)
            t();
    });
    ep->registerExtension(std::make_shared<HelloGreeter>(), 100);
    auto fut = ep->tryAsync<std::string>([](const auto& ext) { return ext->greet("x"); },
                                         std::string{});
    fut.get();
    EXPECT_GE(execCount.load(), 1);
}

TEST_F(AsyncExtensionPointTest, ConcurrentWithThreadPool)
{
    // 使用 std::async 默认线程池执行器（并行调度）
    extension::Executor poolExec = [](std::function<void()> task) {
        if (!task)
            return;
        std::thread([t = std::move(task)] { t(); }).detach();
    };
    auto ep2 = std::make_shared<AsyncExtensionPoint<IGreeter>>("tpool", "pool", poolExec);

    std::atomic<int> callCount{0};
    class SafeGreeter : public IGreeter
    {
    public:
        explicit SafeGreeter(std::atomic<int>* c)
            : m_c(c)
        {
        }
        std::string greet(const std::string&) override
        {
            using namespace std::chrono_literals;
            std::this_thread::sleep_for(5ms);
            ++(*m_c);
            return "done";
        }
        std::atomic<int>* m_c;
    };
    for (int i = 0; i < 8; ++i) {
        ep2->registerExtension(std::make_shared<SafeGreeter>(&callCount), 100 - i);
    }

    auto futures = ep2->broadcastAsync([](const auto& ext) { return ext->greet(""); });
    for (auto& f : futures) {
        EXPECT_EQ(f.get(), "done");
    }
    EXPECT_EQ(callCount.load(), 8);
}

TEST_F(AsyncExtensionPointTest, ExceptionInHandler_DoesNotCrash)
{
    class ThrowingGreeter : public IGreeter
    {
    public:
        std::string greet(const std::string&) override { throw std::runtime_error("boom"); }
    };
    ep->registerExtension(std::make_shared<ThrowingGreeter>(), 100);
    ep->registerExtension(std::make_shared<HiGreeter>(), 50);
    // PriorityOrdered 下：第一个抛异常应被吞掉并继续第二个
    auto fut = ep->tryAsync<std::string>([](const auto& ext) { return ext->greet("E"); },
                                         std::string{},
                                         extension::RacePolicy::PriorityOrdered);
    EXPECT_EQ(fut.get(), "Hi, E");
}

/* ============================================================
 *  4) 开闭原则与多态兼容性测试
 * ============================================================ */

TEST(PolymorphismTest, AllVariantsCanRegisterViaBasePointer)
{
    ExtensionSystem::instance().clear();
    auto p1 = std::make_shared<DefaultExtensionPoint<IGreeter>>("d", "default");
    auto p2 = std::make_shared<ScopedExtensionPoint<IGreeter, std::string>>("s", "scoped");
    auto p3 = std::make_shared<DeferredExtensionPoint<IGreeter>>("l", "lazy");
    auto p4 = std::make_shared<AsyncExtensionPoint<IGreeter>>("a", "async");

    EXPECT_TRUE(ExtensionSystem::instance().registerExtensionPoint(p1));
    EXPECT_TRUE(ExtensionSystem::instance().registerExtensionPoint(p2));
    EXPECT_TRUE(ExtensionSystem::instance().registerExtensionPoint(p3));
    EXPECT_TRUE(ExtensionSystem::instance().registerExtensionPoint(p4));

    EXPECT_EQ(ExtensionSystem::instance().extensionPointIds().size(), 4u);

    // 可通过基类指针安全取回
    auto got = ExtensionSystem::instance().IExtensionSystem::template extensionPoint<IGreeter>("d");
    EXPECT_NE(got, nullptr);
    got = ExtensionSystem::instance().IExtensionSystem::template extensionPoint<IGreeter>("a");
    EXPECT_NE(got, nullptr);

    ExtensionSystem::instance().clear();
}
