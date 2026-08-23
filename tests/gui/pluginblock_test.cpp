#include <gtest/gtest.h>

#include <memory>
#include <vector>

#include <QDebug>
#include <QString>

#include <bakuon/gui/IPlugin.h>
#include <bakuon/gui/PluginContext.h>

#include <gui/b_plugin.h>
#include <gui/b_pluginblock.h>

using namespace bakuon::gui;

namespace {

/// 记录 FakePlugin 各回调的调用顺序，用于验证 Plugin 生命周期方法是否如实转发给 IPlugin。
class LifecycleRecorder
{
public:
    static std::vector<std::string>& events()
    {
        static std::vector<std::string> e;
        return e;
    }
    static void reset() { events().clear(); }
};

/// 最小可用的 IPlugin 测试替身：不依赖任何动态库，直接以“内置插件”的方式构造 Plugin。
class FakePlugin : public IPlugin
{
public:
    ~FakePlugin() override { LifecycleRecorder::events().emplace_back("~FakePlugin"); }

    [[nodiscard]] QString id() const override { return QStringLiteral("test.fake"); }
    [[nodiscard]] QString name() const override { return QStringLiteral("Fake"); }
    [[nodiscard]] QString version() const override { return QStringLiteral("0.0.1"); }

    bool initialize(PluginContext& /*ctx*/) override
    {
        LifecycleRecorder::events().emplace_back("initialize");
        return initializeResult;
    }
    void extensionsInitialized() override
    {
        LifecycleRecorder::events().emplace_back("extensionsInitialized");
    }
    void shutdown() override { LifecycleRecorder::events().emplace_back("shutdown"); }

    bool initializeResult = true;
};

} // namespace

TEST(PluginBlockTest, CreateBuiltinAndAccess)
{
    auto fake  = std::make_shared<FakePlugin>();
    auto block = PluginBlock::create(42, std::static_pointer_cast<IPlugin>(fake));

    ASSERT_NE(block, nullptr) << "PluginBlock::create() 组合分配失败";
    EXPECT_EQ(block->id(), 42u);
    EXPECT_EQ(block->plugin()->id(), 42u) << "PluginBlock::get() 应该能正确定位到紧随其后的 Plugin";
    EXPECT_TRUE(block->plugin()->isLoaded()) << "内置插件构造完成后应视为已加载";
    qDebug("[OK] PluginBlock::create() builtin: id=%zu\n", block->id());
}

TEST(PluginBlockTest, PluginOfSharesRefcount)
{
    auto fake   = std::make_shared<FakePlugin>();
    auto block  = PluginBlock::create(1, std::static_pointer_cast<IPlugin>(fake));
    auto plugin = PluginBlock::pluginOf(block);

    ASSERT_NE(plugin, nullptr);
    EXPECT_EQ(plugin->id(), 1u);
    // aliasing 构造出来的 shared_ptr<Plugin> 应该和 shared_ptr<PluginBlock> 共享同一份引用计数，
    // 而不是各自独立计数（后者会导致其中一个提前把内存释放掉）。
    EXPECT_EQ(block.use_count(), plugin.use_count());
    EXPECT_TRUE(block == plugin.get());
    EXPECT_TRUE(plugin.get() == block);
    qDebug("[OK] PluginBlock::pluginOf() aliasing: use_count=%ld\n",
           static_cast<long>(block.use_count()));
}

TEST(PluginBlockTest, LifecycleCallsAreForwardedInOrder)
{
    LifecycleRecorder::reset();

    auto fake      = std::make_shared<FakePlugin>();
    auto block     = PluginBlock::create(2, std::static_pointer_cast<IPlugin>(fake));
    Plugin* plugin = block->plugin();

    EXPECT_TRUE(plugin->load()) << "内置插件的 load() 应该是幂等的 no-op 成功";
    EXPECT_TRUE(plugin->initialize()) << "initialize() 应该如实转发 IPlugin::initialize() 的返回值";
    plugin->reactExtensions();
    plugin->quit();

    // Plugin::unload() 只是释放了 Plugin 自己持有的引用；测试用例这里还握着 fake 的另一份引用，
    // 所以要主动 reset() 才能真正触发 ~FakePlugin()，验证组合分配 + 自定义删除器路径确实工作。
    fake.reset();

    const std::vector<std::string> expected = {
        "initialize",
        "extensionsInitialized",
        "shutdown",
        "~FakePlugin", // fake.reset() 释放了测试用例自己持有的最后一份引用，触发析构
    };
    EXPECT_EQ(LifecycleRecorder::events(), expected);
    qDebug("[OK] Plugin lifecycle order verified (load->initialize->reactExtensions->quit)\n");
}

TEST(PluginBlockTest, InitializeFailurePropagates)
{
    auto fake              = std::make_shared<FakePlugin>();
    fake->initializeResult = false;
    auto block             = PluginBlock::create(3, std::static_pointer_cast<IPlugin>(fake));

    EXPECT_TRUE(block->plugin()->load());
    EXPECT_FALSE(block->plugin()->initialize())
        << "IPlugin::initialize() 返回 false 时，Plugin::initialize() 必须如实转发，不能吞掉失败";
    qDebug("[OK] Plugin::initialize() propagates IPlugin failure\n");
}

TEST(PluginBlockTest, DestructionFreesCombinedAllocation)
{
    LifecycleRecorder::reset();
    auto fake = std::make_shared<FakePlugin>();
    {
        auto block = PluginBlock::create(4, std::static_pointer_cast<IPlugin>(fake));
        EXPECT_EQ(block.use_count(), 1);
    } // block 在此销毁：应该依次析构 Plugin 子对象、PluginBlock 自身，再释放整块对齐内存

    // Plugin 析构时若 m_instance 仍非空会兜底调用 unload()（见 Plugin::~Plugin() 的安全网逻辑），
    // 但这里已经在上面的作用域里正常 quit 过？—— 没有，这个用例故意不调用 load()/quit()，
    // 只验证“shared_ptr<PluginBlock> 生命周期结束 = 组合内存被正确释放”，不关心业务生命周期。
    EXPECT_EQ(fake.use_count(), 1)
        << "block 销毁后，Plugin 内部持有的 shared_ptr<IPlugin> 引用应被释放";
    qDebug("[OK] combined allocation released, no leak in fake refcount\n");
}
