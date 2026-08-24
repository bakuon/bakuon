#include <gtest/gtest.h>

#include <algorithm>
#include <memory>
#include <vector>

#include <QDebug>
#include <QString>

#include <bakuon/gui/IPlugin.h>
#include <bakuon/gui/PluginContext.h>

#include <gui/b_plugin.h>
#include <gui/b_pluginblock.h>
#include <gui/b_pluginsystem.h>

using namespace bakuon::gui;

namespace {

/// 内置插件测试替身：记录调用顺序，方便验证 PluginSystem 是否如实驱动了 Plugin 的生命周期。
class FakePlugin : public IPlugin
{
public:
    explicit FakePlugin(QString id, std::vector<std::string>* log = nullptr)
        : m_id(std::move(id))
        , m_log(log)
    {
    }

    [[nodiscard]] QString id() const override { return m_id; }
    [[nodiscard]] QString name() const override { return QStringLiteral("Fake-") + m_id; }
    [[nodiscard]] QString version() const override { return QStringLiteral("0.0.1"); }

    bool initialize(PluginContext& /*ctx*/) override
    {
        if (m_log) {
            m_log->push_back((m_id + ":initialize").toStdString());
        }
        return initializeResult;
    }
    void extensionsInitialized() override
    {
        if (m_log) {
            m_log->push_back((m_id + ":extensionsInitialized").toStdString());
        }
    }
    void shutdown() override
    {
        if (m_log) {
            m_log->push_back((m_id + ":shutdown").toStdString());
        }
    }

    bool initializeResult = true;

private:
    QString m_id;
    std::vector<std::string>* m_log;
};

std::shared_ptr<PluginBlock> makeBuiltIn(PluginSystem& system, const QString& id,
                                          std::vector<std::string>* log = nullptr)
{
    auto fake = std::make_shared<FakePlugin>(id, log);
    return PluginBlock::create(system.nextId(), std::static_pointer_cast<IPlugin>(fake));
}

} // namespace

TEST(PluginSystemTest, DiscoverBuiltInAndQuery)
{
    PluginSystem system;
    auto block = makeBuiltIn(system, QStringLiteral("test.a"));

    const size_t id = system.discoverBuiltIn(block);
    ASSERT_NE(id, 0u) << system.lastError().toStdString();

    EXPECT_TRUE(system.hasPlugin(id));
    EXPECT_TRUE(system.hasPlugin(QStringLiteral("test.a")));
    EXPECT_EQ(system.pluginCount(), 1u);
    EXPECT_EQ(system.plugin(id), block);
    EXPECT_EQ(system.plugin(QStringLiteral("test.a")), block);
    qDebug("[OK] discoverBuiltIn + hasPlugin/plugin(id/string) lookup\n");
}

TEST(PluginSystemTest, DuplicateStringIdRejected)
{
    PluginSystem system;
    auto block1 = makeBuiltIn(system, QStringLiteral("dup.id"));
    ASSERT_NE(system.discoverBuiltIn(block1), 0u);

    auto block2 = makeBuiltIn(system, QStringLiteral("dup.id"));
    const size_t id2 = system.discoverBuiltIn(block2);

    EXPECT_EQ(id2, 0u) << "重复的字符串 id 应该被拒绝注册";
    EXPECT_FALSE(system.lastError().isEmpty());
    EXPECT_EQ(system.pluginCount(), 1u);
    qDebug("[OK] duplicate IPlugin::id() rejected: %s\n", qPrintable(system.lastError()));
}

TEST(PluginSystemTest, FullLifecycleBuiltIn)
{
    std::vector<std::string> log;
    PluginSystem system;

    auto blockA = makeBuiltIn(system, QStringLiteral("life.a"), &log);
    auto blockB = makeBuiltIn(system, QStringLiteral("life.b"), &log);
    ASSERT_NE(system.discoverBuiltIn(blockA), 0u);
    ASSERT_NE(system.discoverBuiltIn(blockB), 0u);

    ASSERT_TRUE(system.startup()) << system.lastError().toStdString();
    EXPECT_TRUE(system.isAllInitialized());

    // startup() = loadAll() + initializeAll()；initializeAll() 里所有插件 initialize() 成功后
    // 才会统一进入 reactExtensions() 阶段，所以两个插件的 initialize 应该都排在
    // 任何一个 extensionsInitialized 之前。
    ASSERT_EQ(log.size(), 4u);
    EXPECT_NE(std::find(log.begin(), log.end(), "life.a:initialize"), log.end());
    EXPECT_NE(std::find(log.begin(), log.end(), "life.b:initialize"), log.end());
    const auto lastInitialize = std::max(std::find(log.begin(), log.end(), "life.a:initialize"),
                                          std::find(log.begin(), log.end(), "life.b:initialize"));
    const auto firstExtInit   = std::min(std::find(log.begin(), log.end(), "life.a:extensionsInitialized"),
                                          std::find(log.begin(), log.end(), "life.b:extensionsInitialized"));
    EXPECT_LT(lastInitialize, firstExtInit)
        << "所有插件都 initialize() 成功后才能进入 extensionsInitialized 阶段";

    system.shutdown(); // shutdownAll() + unloadAll()
    EXPECT_FALSE(system.isInitialized(system.plugin(QStringLiteral("life.a"))->id()));

    ASSERT_EQ(log.size(), 6u);
    EXPECT_EQ(log[4].substr(log[4].find(':') + 1), "shutdown");
    EXPECT_EQ(log[5].substr(log[5].find(':') + 1), "shutdown");
    qDebug("[OK] PluginSystem::startup()/shutdown() drive Plugin lifecycle end to end\n");
}

TEST(PluginSystemTest, InitializeFailureIsReportedAndDoesNotRunExtensions)
{
    std::vector<std::string> log;
    PluginSystem system;

    auto fake         = std::make_shared<FakePlugin>(QStringLiteral("fail.one"), &log);
    fake->initializeResult = false;
    auto block = PluginBlock::create(system.nextId(), std::static_pointer_cast<IPlugin>(fake));
    ASSERT_NE(system.discoverBuiltIn(block), 0u);

    EXPECT_FALSE(system.startup());
    EXPECT_FALSE(system.lastError().isEmpty());
    // initialize() 失败，不应该有任何 extensionsInitialized 被调用。
    EXPECT_EQ(log.size(), 1u);
    EXPECT_EQ(log[0], "fail.one:initialize");
    qDebug("[OK] initialize() failure surfaces via lastError(), extensionsInitialized skipped\n");
}

#ifdef BAKUON_TEST_EXAMPLE_PLUGIN_PATH
TEST(PluginSystemTest, DiscoverAndLoadRealDynamicLibraryPlugin)
{
    PluginSystem system;
    const QString path = QStringLiteral(BAKUON_TEST_EXAMPLE_PLUGIN_PATH);

    const size_t id = system.discoverPlugin(path);
    ASSERT_NE(id, 0u) << system.lastError().toStdString();
    EXPECT_TRUE(system.hasPlugin(QStringLiteral("com.bakuon.example")))
        << "example_plugin.json 里的 MetaData.Id 应该被正确解析为查询 key";

    ASSERT_TRUE(system.load(id)) << system.lastError().toStdString();
    ASSERT_TRUE(system.initializeOne(id)) << system.lastError().toStdString();
    EXPECT_TRUE(system.isInitialized(id));

    system.shutdownOne(id);
    EXPECT_TRUE(system.unload(id)) << system.lastError().toStdString();
    qDebug("[OK] discoverPlugin() + load()/initializeOne() against a real built .so\n");
}
#endif
