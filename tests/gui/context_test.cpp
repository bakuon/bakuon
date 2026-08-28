#include <gtest/gtest.h>

#include <QApplication>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

#include <gui/b_commandsystem.h>
#include <gui/b_contextfocusrouter.h>

using namespace bakuon::gui;

TEST(ContextTest, StringCaseInsensitive)
{
    // StrongId 大小写不敏感 interning：不同大小写的同一字符串应该得到相同的 rawId()
    ContextId a{"Editor.Image.Focused"};
    ContextId b{"editor.image.focused"};
    EXPECT_TRUE(a == b) << "不同大小写得到了不同的上下文";
    EXPECT_TRUE(a.rawId() == b.rawId()) << "不同大小写字符串得到不一致的 Id";
    EXPECT_TRUE(a.rawId() != 0) << "无效的上下文";
    qDebug("[OK] StrongId case-insensitive interning: rawId=%u, value=%s\n",
           a.rawId(),
           qPrintable(a.name()));
}

// collision detection
TEST(ContextTest, CollisionDetection)
{
    const ContextId ctx{"test.collision"};
    auto r1 = CommandSystem::registerContext(ctx, "moduleA", "first registration");
    auto r2 = CommandSystem::registerContext(ctx, "moduleA", "duplicate owner call"); // 幂等
    auto r3 = CommandSystem::registerContext(ctx, "moduleB", "conflicting owner");    // 冲突
    EXPECT_TRUE(r1) << "上下文发生冲突";
    EXPECT_TRUE(r2) << "上下文发生冲突";
    EXPECT_TRUE(r3) << "上下文冲突应该被拒绝"; // 应该被拒绝（下面会有一条 qWarning 输出，属预期行为）
    qDebug("[OK] registerContext collision detection: r1=%s r2=%s r3=%s\n",
           qPrintable(r1->id().name()),
           qPrintable(r2->id().name()),
           qPrintable(r3->id().name()));
}

TEST(ContextTest, UnregisterContext)
{
    //pushContext 对未注册上下文的警告（下面会输出一条 qWarning，属预期行为）
    CommandSystem::pushContext(ContextId{"never.registered"}, QApplication::instance());
    CommandSystem::popContext(ContextId{"never.registered"}, QApplication::instance());
    qDebug("[OK] pushContext on unregistered context warns but does not crash\n");
}

TEST(ContextTest, ContextFocusRouter)
{
    // 焦点切换驱动上下文集合的 push/pop
    const ContextId ctxA      = CommandSystem::declareContext("test.widgetA", "widget A", "test");
    const ContextId ctxB      = CommandSystem::declareContext("test.widgetB", "widget B", "test");
    const ContextId ctxShared = CommandSystem::declareContext("test.shared",
                                                              "shared by A and B",
                                                              "test");

    auto* router = new ContextFocusRouter(QApplication::instance());
    router->install();

    auto* window  = new QWidget;
    auto* layout  = new QVBoxLayout(window);
    auto* widgetA = new QWidget(window);
    auto* widgetB = new QWidget(window);
    widgetA->setFocusPolicy(Qt::StrongFocus);
    widgetB->setFocusPolicy(Qt::StrongFocus);
    layout->addWidget(widgetA);
    layout->addWidget(widgetB);

    CommandSystem::setProviderContexts(widgetA, Context{ctxA, ctxShared});
    CommandSystem::setProviderContexts(widgetB, Context{ctxB, ctxShared});

    window->show();

    widgetA->setFocus();
    qApp->processEvents();
    EXPECT_TRUE(CommandSystem::isActiveContext(ctxA));
    EXPECT_TRUE(CommandSystem::isActiveContext(ctxShared));
    EXPECT_TRUE(!CommandSystem::isActiveContext(ctxB));
    qDebug("[OK] focusing widgetA activates {ctxA, ctxShared}\n");

    widgetB->setFocus();
    qApp->processEvents();
    EXPECT_TRUE(!CommandSystem::isActiveContext(ctxA));
    EXPECT_TRUE(CommandSystem::isActiveContext(ctxShared)); // 共享上下文全程未失活
    EXPECT_TRUE(CommandSystem::isActiveContext(ctxB));
    qDebug("[OK] switching focus to widgetB: ctxA deactivated, ctxShared stayed active "
           "throughout, ctxB activated\n");

    delete window;
}

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);

    ::testing::InitGoogleTest(&argc, argv);

    QTimer::singleShot(0, []() {
        int gtest_result = RUN_ALL_TESTS();
        // 测试完成后，带着 gtest 的返回码退出 Qt 事件循环
        QCoreApplication::exit(gtest_result);
    });

    return app.exec();
}
