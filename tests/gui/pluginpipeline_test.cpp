#include <gtest/gtest.h>

#include <memory>
#include <vector>

#include <QDebug>
#include <QString>

#include <bakuon/gui/IPlugin.h>
#include <bakuon/gui/PluginContext.h>

#include <gui/b_pluginpipeline.h>

using namespace bakuon::gui;

namespace {

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
        if (m_log)
            m_log->push_back((m_id + ":initialize").toStdString());
        return initializeResult;
    }
    void extensionsInitialized() override
    {
        if (m_log)
            m_log->push_back((m_id + ":extensionsInitialized").toStdString());
    }
    void shutdown() override
    {
        if (m_log)
            m_log->push_back((m_id + ":shutdown").toStdString());
    }

    bool initializeResult = true;

private:
    QString m_id;
    std::vector<std::string>* m_log;
};

} // namespace

TEST(PluginPipelineTest, BuiltInStartsAtValidatedAndCarriesMetadata)
{
    auto fake = std::make_shared<FakePlugin>(QStringLiteral("test.builtin"));
    PluginPipeline pipeline(1, std::static_pointer_cast<IPlugin>(fake));

    EXPECT_EQ(pipeline.state(), PluginState::Validated) << "内置插件构造后应直接跳到 Validated";
    EXPECT_EQ(pipeline.metadata().id, QStringLiteral("test.builtin"));
    qDebug("[OK] builtin pipeline starts at Validated with metadata populated\n");
}

TEST(PluginPipelineTest, BuiltInFullLifecycle)
{
    std::vector<std::string> log;
    auto fake = std::make_shared<FakePlugin>(QStringLiteral("life.one"), &log);
    PluginPipeline pipeline(1, std::static_pointer_cast<IPlugin>(fake));

    ASSERT_TRUE(pipeline.launch()) << pipeline.lastError().toStdString();
    EXPECT_EQ(pipeline.state(), PluginState::Initialized)
        << "launch() 应该停在 Initialized，不自动进入 Running";

    ASSERT_TRUE(pipeline.run()) << pipeline.lastError().toStdString();
    EXPECT_EQ(pipeline.state(), PluginState::Running);

    ASSERT_TRUE(pipeline.stop()) << pipeline.lastError().toStdString();
    EXPECT_EQ(pipeline.state(), PluginState::Stopped);

    ASSERT_TRUE(pipeline.unload()) << pipeline.lastError().toStdString();
    EXPECT_EQ(pipeline.state(), PluginState::Unloaded);

    const std::vector<std::string> expected = {
        "life.one:initialize",
        "life.one:extensionsInitialized",
        "life.one:shutdown",
    };
    EXPECT_EQ(log, expected);
    qDebug("[OK] full lifecycle: launch->run->stop->unloadNow drives IPlugin correctly\n");
}

TEST(PluginPipelineTest, IllegalTransitionRejectedWithoutChangingState)
{
    auto fake = std::make_shared<FakePlugin>(QStringLiteral("test.illegal"));
    PluginPipeline pipeline(1, std::static_pointer_cast<IPlugin>(fake));

    // 此时处于 Validated，直接尝试 StartRun（跳过 Resolve/Load/Initialize）应该被拒绝。
    EXPECT_FALSE(pipeline.handle(PluginEvent::StartRun));
    EXPECT_EQ(pipeline.state(), PluginState::Validated) << "非法转换不应该改变当前状态";
    EXPECT_FALSE(pipeline.lastError().isEmpty());
    qDebug("[OK] illegal transition rejected: %s\n", qPrintable(pipeline.lastError()));
}

TEST(PluginPipelineTest, InitializeFailureThenRetrySucceeds)
{
    auto fake              = std::make_shared<FakePlugin>(QStringLiteral("test.retry"));
    fake->initializeResult = false;
    PluginPipeline pipeline(1, std::static_pointer_cast<IPlugin>(fake));

    EXPECT_FALSE(pipeline.launch());
    EXPECT_EQ(pipeline.state(), PluginState::InitializeFailed);
    EXPECT_TRUE(pipeline.isFailed());

    // 重试：不需要重建对象，也不需要重新走 Resolve/Load，直接从失败态重新投递 StartInitialize。
    fake->initializeResult = true;
    EXPECT_TRUE(pipeline.handle(PluginEvent::StartInitialize));
    EXPECT_EQ(pipeline.state(), PluginState::Initialized);
    EXPECT_FALSE(pipeline.isFailed());
    qDebug("[OK] InitializeFailed -> retry via StartInitialize -> Initialized\n");
}

#ifdef BAKUON_TEST_EXAMPLE_PLUGIN_PATH
TEST(PluginPipelineTest, RealDynamicLibraryPluginFullChain)
{
    const QString path = QStringLiteral(BAKUON_TEST_EXAMPLE_PLUGIN_PATH);
    PluginPipeline pipeline(1, path);

    EXPECT_EQ(pipeline.state(), PluginState::Idle);
    ASSERT_TRUE(pipeline.launch()) << pipeline.lastError().toStdString();
    EXPECT_EQ(pipeline.state(), PluginState::Initialized);
    EXPECT_EQ(pipeline.metadata().id, QStringLiteral("com.bakuon.example"));

    ASSERT_TRUE(pipeline.run());
    ASSERT_TRUE(pipeline.stop());
    ASSERT_TRUE(pipeline.unload());
    EXPECT_EQ(pipeline.state(), PluginState::Unloaded);
    qDebug("[OK] real .so: Idle -> ... -> Initialized -> Running -> Stopped -> Unloaded\n");
}
#endif
