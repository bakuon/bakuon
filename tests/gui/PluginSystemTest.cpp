#include <gtest/gtest.h>

#include <algorithm>
#include <memory>
#include <vector>

#include <QDebug>
#include <QString>

#include <bakuon/gui/IPlugin.h>
#include <bakuon/gui/PluginContext.h>

#include <gui/b_pluginpipeline.h>
#include <gui/b_pluginsystem.h>

using namespace bakuon::gui;

namespace {

class FakePlugin : public IPlugin
{
public:
    explicit FakePlugin(QString id, QStringList deps = {}, std::vector<std::string>* log = nullptr)
        : m_id(std::move(id))
        , m_deps(std::move(deps))
        , m_log(log)
    {
    }

    [[nodiscard]] QString id() const override { return m_id; }
    [[nodiscard]] QString name() const override { return QStringLiteral("Fake-") + m_id; }
    [[nodiscard]] QString version() const override { return QStringLiteral("0.0.1"); }
    [[nodiscard]] QStringList dependencies() const override { return m_deps; }

    bool initialize(PluginContext& /*ctx*/) override
    {
        if (m_log) m_log->push_back((m_id + ":initialize").toStdString());
        return initializeResult;
    }
    void extensionsInitialized() override
    {
        if (m_log) m_log->push_back((m_id + ":extensionsInitialized").toStdString());
    }
    void shutdown() override
    {
        if (m_log) m_log->push_back((m_id + ":shutdown").toStdString());
    }

    bool initializeResult = true;

private:
    QString m_id;
    QStringList m_deps;
    std::vector<std::string>* m_log;
};

std::shared_ptr<FakePlugin> makeFake(QString id, QStringList deps = {}, std::vector<std::string>* log = nullptr)
{
    return std::make_shared<FakePlugin>(std::move(id), std::move(deps), log);
}

} // namespace

TEST(PluginSystemTest, RegisterBuiltInAndQuery)
{
    PluginSystem system;
    const size_t id = system.registerBuiltIn(makeFake(QStringLiteral("test.a")));

    ASSERT_NE(id, 0u);
    EXPECT_TRUE(system.hasPlugin(id));
    EXPECT_TRUE(system.hasPlugin(QStringLiteral("test.a")));
    EXPECT_EQ(system.pluginCount(), 1u);
    EXPECT_EQ(system.pipeline(id), system.pipeline(QStringLiteral("test.a")));
    qDebug("[OK] registerBuiltIn + hasPlugin/pipeline(id/string) lookup\n");
}

TEST(PluginSystemTest, StartupDrivesAllPluginsInitializeBeforeAnyRun)
{
    std::vector<std::string> log;
    PluginSystem system;
    system.registerBuiltIn(makeFake(QStringLiteral("life.a"), {}, &log));
    system.registerBuiltIn(makeFake(QStringLiteral("life.b"), {}, &log));

    ASSERT_TRUE(system.startup()) << system.lastError().toStdString();

    ASSERT_EQ(log.size(), 4u);
    const auto lastInit  = std::max(std::find(log.begin(), log.end(), "life.a:initialize"),
                                     std::find(log.begin(), log.end(), "life.b:initialize"));
    const auto firstExt   = std::min(std::find(log.begin(), log.end(), "life.a:extensionsInitialized"),
                                      std::find(log.begin(), log.end(), "life.b:extensionsInitialized"));
    EXPECT_LT(lastInit, firstExt) << "所有插件都 initialize() 成功后才能进入 extensionsInitialized 阶段";

    system.shutdown();
    ASSERT_EQ(log.size(), 6u);
    qDebug("[OK] startup() batches initialize before run across all plugins\n");
}

TEST(PluginSystemTest, DependencyOrderIndependentOfRegistrationOrder)
{
    // A 依赖 B，但 A 先注册、B 后注册——验证 launchAll() 的重试机制能纠正这种顺序问题。
    PluginSystem system;
    const size_t idA = system.registerBuiltIn(makeFake(QStringLiteral("dep.a"), {QStringLiteral("dep.b")}));
    const size_t idB = system.registerBuiltIn(makeFake(QStringLiteral("dep.b")));

    ASSERT_TRUE(system.launchAll()) << system.lastError().toStdString();
    EXPECT_EQ(system.pipeline(idA)->state(), PluginState::Initialized);
    EXPECT_EQ(system.pipeline(idB)->state(), PluginState::Initialized);
    qDebug("[OK] dependency resolved correctly despite registration order\n");
}

TEST(PluginSystemTest, MissingDependencyStaysFailed)
{
    PluginSystem system;
    const size_t id = system.registerBuiltIn(
        makeFake(QStringLiteral("dep.orphan"), {QStringLiteral("dep.nonexistent")}));

    EXPECT_FALSE(system.launchAll());
    EXPECT_EQ(system.pipeline(id)->state(), PluginState::ResolveFailed);
    EXPECT_FALSE(system.pipeline(id)->lastError().isEmpty());
    qDebug("[OK] genuinely missing dependency stays ResolveFailed: %s\n",
           qPrintable(system.pipeline(id)->lastError()));
}

TEST(PluginSystemTest, CircularDependencyDetected)
{
    PluginSystem system;
    system.registerBuiltIn(makeFake(QStringLiteral("cycle.a"), {QStringLiteral("cycle.b")}));
    system.registerBuiltIn(makeFake(QStringLiteral("cycle.b"), {QStringLiteral("cycle.a")}));

    EXPECT_FALSE(system.launchAll());
    EXPECT_EQ(system.pipeline(QStringLiteral("cycle.a"))->state(), PluginState::ResolveFailed);
    EXPECT_EQ(system.pipeline(QStringLiteral("cycle.b"))->state(), PluginState::ResolveFailed);
    qDebug("[OK] circular dependency detected: %s\n",
           qPrintable(system.pipeline(QStringLiteral("cycle.a"))->lastError()));
}

TEST(PluginSystemTest, OneFailedPluginDoesNotBlockOthersFromRunning)
{
    std::vector<std::string> log;
    PluginSystem system;
    auto badFake             = makeFake(QStringLiteral("mix.bad"), {}, &log);
    badFake->initializeResult = false;
    system.registerBuiltIn(badFake);
    system.registerBuiltIn(makeFake(QStringLiteral("mix.good"), {}, &log));

    system.launchAll(); // 预期整体返回 false（有失败），但不应该影响另一个插件
    EXPECT_EQ(system.pipeline(QStringLiteral("mix.bad"))->state(), PluginState::InitializeFailed);
    EXPECT_EQ(system.pipeline(QStringLiteral("mix.good"))->state(), PluginState::Initialized);

    EXPECT_TRUE(system.runAll());
    EXPECT_EQ(system.pipeline(QStringLiteral("mix.good"))->state(), PluginState::Running);
    EXPECT_EQ(system.pipeline(QStringLiteral("mix.bad"))->state(), PluginState::InitializeFailed)
        << "失败的插件不应该被意外推进状态";

    const auto it = std::find(log.begin(), log.end(), "mix.bad:extensionsInitialized");
    EXPECT_EQ(it, log.end()) << "失败的插件不应该收到 extensionsInitialized() 调用";
    qDebug("[OK] one InitializeFailed plugin does not block healthy plugins from Running\n");
}

TEST(PluginSystemTest, RunOrderDoesNotNeedToRespectDependencies)
{
    // 更正：extensionsInitialized() 的调用顺序不需要跟着依赖图走。IPlugin 的契约是扩展在
    // initialize() 阶段就注册完成了，runAll() 只会在全体插件都到达 Initialized 之后才被调用，
    // 也就是说无论 run.b 的 extensionsInitialized() 是在 run.a 之前还是之后执行，run.a 需要
    // 访问的、run.b 注册的扩展早就已经就绪了——所以这里不对调用顺序做任何断言，只验证
    // 两个都能正常进入 Running，顺序是任意的（这正是 runAll() 用 idSnapshot() 而不是
    // topologicalOrder() 的原因）。
    PluginSystem system;
    system.registerBuiltIn(makeFake(QStringLiteral("run.a"), {QStringLiteral("run.b")}));
    system.registerBuiltIn(makeFake(QStringLiteral("run.b")));

    ASSERT_TRUE(system.startup()) << system.lastError().toStdString();
    EXPECT_EQ(system.pipeline(QStringLiteral("run.a"))->state(), PluginState::Running);
    EXPECT_EQ(system.pipeline(QStringLiteral("run.b"))->state(), PluginState::Running);
    qDebug("[OK] runAll() succeeds regardless of extensionsInitialized() call order\n");
}

TEST(PluginSystemTest, StopAndUnloadOrderRespectsDependenciesInReverse)
{
    // A 依赖 B：停止/卸载时 A 必须先于 B——A 的 shutdown() 可能还要访问 B 的扩展。
    std::vector<std::string> log;
    PluginSystem system;
    system.registerBuiltIn(makeFake(QStringLiteral("stop.a"), {QStringLiteral("stop.b")}, &log));
    system.registerBuiltIn(makeFake(QStringLiteral("stop.b"), {}, &log));

    ASSERT_TRUE(system.startup());
    system.shutdown();

    const auto aShutdown = std::find(log.begin(), log.end(), "stop.a:shutdown");
    const auto bShutdown = std::find(log.begin(), log.end(), "stop.b:shutdown");
    ASSERT_NE(aShutdown, log.end());
    ASSERT_NE(bShutdown, log.end());
    EXPECT_LT(aShutdown, bShutdown) << "依赖 B 的插件 A 必须先于 B 停止，B 停止时 A 已经不会再用它了";

    EXPECT_EQ(system.pipeline(QStringLiteral("stop.a"))->state(), PluginState::Unloaded);
    EXPECT_EQ(system.pipeline(QStringLiteral("stop.b"))->state(), PluginState::Unloaded);
    qDebug("[OK] stopAll()/unloadAll() stop dependents before dependencies (reverse topological order)\n");
}

TEST(PluginSystemTest, StopOrderRespectsChainOfThree)
{
    // A -> B -> C（A 依赖 B，B 依赖 C）。停止顺序必须是 A, B, C。
    std::vector<std::string> log;
    PluginSystem system;
    system.registerBuiltIn(makeFake(QStringLiteral("chain.a"), {QStringLiteral("chain.b")}, &log));
    system.registerBuiltIn(makeFake(QStringLiteral("chain.b"), {QStringLiteral("chain.c")}, &log));
    system.registerBuiltIn(makeFake(QStringLiteral("chain.c"), {}, &log));

    ASSERT_TRUE(system.startup()) << system.lastError().toStdString();
    system.shutdown();

    const auto posA = std::find(log.begin(), log.end(), "chain.a:shutdown");
    const auto posB = std::find(log.begin(), log.end(), "chain.b:shutdown");
    const auto posC = std::find(log.begin(), log.end(), "chain.c:shutdown");
    ASSERT_NE(posA, log.end());
    ASSERT_NE(posB, log.end());
    ASSERT_NE(posC, log.end());
    EXPECT_LT(posA, posB);
    EXPECT_LT(posB, posC);
    qDebug("[OK] three-level dependency chain stops in exact A -> B -> C order\n");
}

TEST(PluginSystemTest, CommandLineArgumentsScopedAndGlobalBroadcast)
{
    QStringList argsSeenByA;
    QStringList argsSeenByB;

    class ArgCapturePlugin : public IPlugin
    {
    public:
        ArgCapturePlugin(QString id, QStringList* sink)
            : m_id(std::move(id))
            , m_sink(sink)
        {
        }
        [[nodiscard]] QString id() const override { return m_id; }
        [[nodiscard]] QString name() const override { return m_id; }
        [[nodiscard]] QString version() const override { return QStringLiteral("1.0"); }
        bool initialize(PluginContext& ctx) override
        {
            *m_sink = ctx.arguments();
            return true;
        }
        void extensionsInitialized() override {}
        void shutdown() override {}

    private:
        QString m_id;
        QStringList* m_sink;
    };

    PluginSystem system;
    system.setCommandLineArguments({
        QStringLiteral("--verbose"),                  // 全局：应该被 A 和 B 都收到
        QStringLiteral("--plugin:arg.a.theme=dark"),   // 只应该给 arg.a（还原成 --theme=dark）
        QStringLiteral("--plugin:arg.b.level=3"),      // 只应该给 arg.b（还原成 --level=3）
    });

    system.registerBuiltIn(std::make_shared<ArgCapturePlugin>(QStringLiteral("arg.a"), &argsSeenByA));
    system.registerBuiltIn(std::make_shared<ArgCapturePlugin>(QStringLiteral("arg.b"), &argsSeenByB));

    ASSERT_TRUE(system.launchAll()) << system.lastError().toStdString();

    EXPECT_TRUE(argsSeenByA.contains(QStringLiteral("--verbose")));
    EXPECT_TRUE(argsSeenByA.contains(QStringLiteral("--theme=dark")));
    EXPECT_FALSE(argsSeenByA.contains(QStringLiteral("--level=3")))
        << "不属于 arg.a 的限定参数不应该泄漏给它";

    EXPECT_TRUE(argsSeenByB.contains(QStringLiteral("--verbose")));
    EXPECT_TRUE(argsSeenByB.contains(QStringLiteral("--level=3")));
    EXPECT_FALSE(argsSeenByB.contains(QStringLiteral("--theme=dark")));

    qDebug("[OK] global args broadcast to all, --plugin:<id>. args scoped correctly\n");
}


#ifdef BAKUON_TEST_EXAMPLE_PLUGIN_PATH
TEST(PluginSystemTest, RegisterFileAndFullOrchestration)
{
    PluginSystem system;
    const QString path = QStringLiteral(BAKUON_TEST_EXAMPLE_PLUGIN_PATH);

    const size_t id = system.registerFile(path);
    ASSERT_NE(id, 0u);

    ASSERT_TRUE(system.startup()) << system.lastError().toStdString();
    EXPECT_TRUE(system.hasPlugin(QStringLiteral("com.bakuon.example")));
    EXPECT_EQ(system.pipeline(id)->state(), PluginState::Running);

    system.shutdown();
    EXPECT_EQ(system.pipeline(id)->state(), PluginState::Unloaded);
    qDebug("[OK] registerFile() + startup()/shutdown() against a real built .so\n");
}
#endif
