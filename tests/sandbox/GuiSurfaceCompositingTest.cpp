#include <gtest/gtest.h>

#include <QEventLoop>
#include <QImage>
#include <QTimer>

#include <sandbox/b_guisurfaceevents.h>
#include <sandbox/b_tabsandboxmanager.h>

using namespace bakuon::sandbox;

namespace {

template<typename Predicate>
bool waitUntil(Predicate predicate, int timeoutMs = 5000)
{
    QEventLoop loop;
    QTimer timeoutTimer;
    timeoutTimer.setSingleShot(true);
    QObject::connect(&timeoutTimer, &QTimer::timeout, &loop, &QEventLoop::quit);
    timeoutTimer.start(timeoutMs);

    QTimer pollTimer;
    QObject::connect(&pollTimer, &QTimer::timeout, [&] {
        if (predicate()) {
            loop.quit();
        }
    });
    pollTimer.start(10);

    if (predicate()) {
        return true;
    }
    loop.exec();
    return predicate();
}

/// 单纯把事件循环晾一段时间，不等待任何成功条件——用于"验证在这段时间里
/// 什么都没有发生"这类断言（变化检测：画面静止时不应该再收到新帧）。
void spinWithoutExpectation(int ms)
{
    QEventLoop loop;
    QTimer::singleShot(ms, &loop, &QEventLoop::quit);
    loop.exec();
}

QString sandboxRuntimePath()
{
    return QString::fromLatin1(BAKUON_TEST_SANDBOX_RUNTIME_PATH);
}

QString sandboxedExamplePluginPath()
{
    return QString::fromLatin1(BAKUON_TEST_SANDBOXED_EXAMPLE_PLUGIN_PATH);
}

} // namespace

/**
 * @brief 端到端验证跨进程 GUI 合成链路：渲染方向 + 输入方向都要真正打通，
 *        不是分别单独验证两条半截的链路。
 *
 * 验证思路：
 *  1. 打开一个 Tab，等到收到第一帧（证明渲染方向：sandbox_runtime 里
 *     ClickCounterWidget 的初始画面确实通过共享内存传回了 Host）。
 *  2. 记录首帧内容，dispatchInputEvent() 发送一次鼠标点击。
 *  3. 等待收到一帧内容和首帧不同的新帧（证明输入方向：点击事件真的送达了
 *     沙箱进程里的 widget、触发了它的 mousePressEvent()、改变了点击计数、
 *     进而改变了下一次抓帧的画面内容）。
 *
 * 不做像素级 OCR 识别数字——"画面内容变了"已经是输入事件被正确处理的
 * 强信号（ClickCounterWidget 唯一会变化外观的原因就是点击计数递增），
 * 用像素比较而不是识别具体数字，是刻意让测试保持简单、不脆弱。
 */
TEST(GuiSurfaceCompositingTest, RenderAndInputRoundTripThroughSandbox)
{
    TabSandboxManager manager(sandboxRuntimePath());

    QImage firstFrame;
    QImage latestFrame;
    int frameCount = 0;
    QObject::connect(&manager, &TabSandboxManager::tabFrameReady, &manager,
                      [&](uint64_t, const QImage &image, const QRect &) {
                          if (firstFrame.isNull()) {
                              firstFrame = image;
                          }
                          latestFrame = image;
                          ++frameCount;
                      });

    const uint64_t tabId = manager.openTab(sandboxedExamplePluginPath());
    ASSERT_NE(tabId, 0u);

    ASSERT_TRUE(waitUntil([&] { return manager.tabState(tabId) == TabState::Running; }))
        << "等待 Tab 进入 Running 超时";
    ASSERT_TRUE(waitUntil([&] { return !firstFrame.isNull(); }))
        << "渲染方向没有打通——一直没收到任何一帧画面";
    EXPECT_EQ(firstFrame.size(), QSize(480, 360));
    EXPECT_FALSE(firstFrame.allGray()) << "首帧应该是有颜色的背景（蓝色调），不应该是灰阶/空白";

    // show()（为了让 update() 真正生效，见 SandboxRuntime 里 WA_DontShowOnScreen 的说明）
    // 自己会触发一次"自然"的首次真实重绘，和我们手动调用一次 captureAndSendFrame()
    // 几乎同时发生，但走的是异步的事件循环调度，不一定卡在同一个时间点——这里先让
    // 这个启动瞬态过去，再开始统计"画面静止时是否还在发新帧"，避免把这个良性的
    // 一次性事件误判成变化检测没生效。
    spinWithoutExpectation(300);

    // 变化检测验证：画面完全静止的这段时间里，不应该再收到任何新的一帧——
    // 这是本轮"优化方向"里分量最重的一项，光靠上面"收到过帧"不足以证明它生效，
    // 必须反过来证明"没有变化就真的不再发送"。
    const int idleFrameCount = frameCount;
    spinWithoutExpectation(500);
    EXPECT_EQ(frameCount, idleFrameCount)
        << "画面静止期间不应该再收到新帧——变化检测没有生效，还在无脑轮询发送";

    // 发一次鼠标点击（在 widget 中心），validate 输入方向。
    ASSERT_TRUE(manager.dispatchInputEvent(tabId, static_cast<int>(GuiInputEventType::MousePress),
                                          QPoint(240, 180),
                                          static_cast<int>(GuiMouseButton::Left), 0, 0));
    ASSERT_TRUE(manager.dispatchInputEvent(
        tabId, static_cast<int>(GuiInputEventType::MouseRelease), QPoint(240, 180),
        static_cast<int>(GuiMouseButton::Left), 0, 0));

    const int framesBeforeClick = frameCount;
    ASSERT_TRUE(waitUntil(
        [&] { return frameCount > framesBeforeClick && latestFrame != firstFrame; }, 3000))
        << "输入方向没有打通——点击之后画面内容一直没有变化";

    manager.closeAll();
    ASSERT_TRUE(waitUntil([&] { return manager.count() == 0; })) << "等待 closeAll 收尾超时";
}
