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
