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
