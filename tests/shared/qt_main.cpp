
#include <QTimer>

#if defined(USE_QT_GUI_APP)
#include <QApplication>
#elif defined(USE_QT_CORE_APP)
#include <QCoreApplication>
#endif

#include <gtest/gtest.h>

int main(int argc, char* argv[])
{
    ::testing::InitGoogleTest(&argc, argv);

#if defined(USE_QT_GUI_APP) || defined(USE_QT_CORE_APP)

#if defined(USE_QT_GUI_APP)

    // 有些环境下没有显示设备时如CI，可加：
#ifdef Q_OS_LINUX
    qputenv("QT_QPA_PLATFORM", "offscreen");
#endif

    QApplication app(argc, argv);
#else
    QCoreApplication app(argc, argv);
#endif

    int test_result = 0;

    // 核心技巧：利用单次定时器。当 app.exec() 启动、事件循环开始运转的第一个瞬间，
    // Qt 会立刻回调这个 Lambda 表达式来执行 Google Test，保证 processEvents / 焦点等行为正常。
    QTimer::singleShot(0, [&]() {
        test_result = RUN_ALL_TESTS();

        // 测试全部跑完后，必须手动退出事件循环，否则整个 CI 会卡死在 app.exec()
        QCoreApplication::exit(test_result);
    });

    // 启动全局事件循环，代码会在此处阻塞，直到上面调用了 exit()
    return app.exec();
#else
    // 普通非 Qt 测试
    return RUN_ALL_TESTS();
#endif
}
