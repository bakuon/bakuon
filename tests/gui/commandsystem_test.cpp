#include <QApplication>
#include <QDebug>
#include <QDir>
#include <QFocusEvent>
#include <QJsonObject>
#include <QMenu>
#include <QMenuBar>
#include <QTimer>
#include <QWidget>

#include <gtest/gtest.h>
#include <gui/b_commandmodel.h>
#include <gui/b_commandsystem.h>

// #include "../shared/focuscontextwatcher.h"

using namespace bakuon::gui;

namespace {
const CommandId kCmdA{"test.a"};
const CommandId kCmdB{"test.b"};
const CommandId kCmdC{"test.c"};
} // namespace

TEST(CommandSystem, CommandLayout)
{
    CommandManager mgr;
    mgr.registerCommand(kCmdA, QStringLiteral("命令A"));
    mgr.registerCommand(kCmdB, QStringLiteral("命令B"));
    mgr.registerCommand(kCmdC, QStringLiteral("命令C"));

    CommandLayout layout;
    using Item = CommandLayout::Item;

    Item* fileMenu = layout.addMenu(nullptr, 0, QStringLiteral("文件"));
    layout.addCommand(fileMenu, 0, kCmdA);
    Item* editMenu = layout.addMenu(nullptr, 1, QStringLiteral("编辑"));
    layout.addCommand(editMenu, 0, kCmdB);
    layout.addSeparator(editMenu, 1);
    layout.addCommand(editMenu, 2, kCmdC);

    const size_t num = layout.root()->childCount();
    EXPECT_TRUE(num == 2) << "FAIL: " << "根节点下期望有 2 个菜单，结果是 " << num;
    EXPECT_TRUE(editMenu->childCount() == 3) << "FAIL: " << "编辑菜单下有 3 个子节点（B/分隔线/C）";

    // 菜单栏顶层不允许分隔线
    EXPECT_TRUE(layout.addSeparator(nullptr, 0) == nullptr)
        << "FAIL: " << "根节点添加分隔线没有被拒绝";

    // 移动：把编辑菜单下第 2 项（分隔线，下标1）移到最前面——用句柄而非下标定位
    auto* sepItem = editMenu->childAt(1);
    EXPECT_TRUE(sepItem->data().type == CommandLayoutData::Type::Separator)
        << "取到的第1项不是分隔线";
    EXPECT_TRUE(layout.moveItem(editMenu, 1, editMenu, 0)) << "分隔线移动到编辑菜单最前面失败";
    EXPECT_TRUE(editMenu->childAt(0) == sepItem) << "移动后第0项不是刚才那个分隔线节点";

    // TreeNode 自带的 paths()/pathNode() 往返一致性
    const std::vector<std::size_t> path = sepItem->paths();
    EXPECT_TRUE(layout.root()->pathNode(path) == sepItem)
        << "paths()/pathNode() 往返定位到不是同一节点";

    // JSON 往返序列化
    const QJsonObject json = layout.serialize();
    CommandLayout layout2;
    layout2.deserialize(json);
    EXPECT_TRUE(layout2.root()->childCount() == layout.root()->childCount())
        << "JSON 往返后根节点行数不一致";
    EXPECT_TRUE(layout2.root()->childAt(1)->childCount() == editMenu->childCount())
        << "JSON 往返后编辑菜单子节点数不一致";

    // 存盘/读盘
    const QString path2 = QDir::temp().filePath(QStringLiteral("bakuon_test_commandlayout.json"));
    EXPECT_TRUE(layout.save(path2)) << "保存到磁盘失败";
    CommandLayout layout3;
    EXPECT_TRUE(layout3.load(path2)) << "从磁盘读取失败";
    EXPECT_TRUE(layout3.root()->childCount() == 2) << "读盘后根节点行数不正确";

    // 引用了未注册命令的场景：不应崩溃，只应在渲染时被跳过
    const CommandId unknownId{"test.unknown"};
    layout3.addCommand(layout3.root()->childAt(0), 1, unknownId);
    // 渲染到真实 QMenuBar，验证渲染（现在直接消费 CommandLayout）不崩溃、
    // 且能正确跳过未知命令
    QMenuBar menuBar;
    mgr.renderMenuBar(&layout3, &menuBar);
    EXPECT_TRUE(menuBar.actions().size() == 2) << "期望渲染出 2 个顶层菜单";
    QMenu* fileMenuWidget = menuBar.actions().first()->menu();
    EXPECT_TRUE(fileMenuWidget != nullptr) << "文件菜单没有被正确创建为 QMenu";
    EXPECT_TRUE(fileMenuWidget->actions().size() == 1)
        << "文件菜单期望只渲染出 1 个 QAction（未注册的 unknownId 被未能安全跳过）";

    mgr.renderMenuBar(&layout3, &menuBar);
    mgr.renderMenuBar(&layout3, &menuBar);
    const auto submenus = menuBar.findChildren<QMenu*>(QString(), Qt::FindDirectChildrenOnly);
    EXPECT_TRUE(submenus.size() == 2) << "反复 render 产生了残留孤儿 QMenu";
}

TEST(CommandSystem, CommandModel)
{
    CommandLayout layout;
    using Item = CommandLayout::Item;

    Item* fileMenu = layout.addMenu(nullptr, 0, QStringLiteral("文件"));
    layout.addCommand(fileMenu, 0, kCmdA);
    Item* editMenu = layout.addMenu(nullptr, 1, QStringLiteral("编辑"));
    layout.addCommand(editMenu, 0, kCmdB);
    layout.addSeparator(editMenu, 1);
    layout.addCommand(editMenu, 2, kCmdC);

    CommandModel model(&layout);
    EXPECT_TRUE(model.rowCount({}) == 2) << "CommandModel 读到的根节点行数与 CommandLayout 不一致";

    const QModelIndex editMenuIdx = model.index(1, 0, {});
    EXPECT_TRUE(model.data(editMenuIdx).toString() == QStringLiteral("编辑"))
        << "CommandModel::data 未能正确读取菜单标题";

    const QModelIndex newCmdIdx = model.addCommand(editMenuIdx, 0, kCmdA);
    EXPECT_TRUE(newCmdIdx.isValid()) << "通过 CommandModel::addCommand 插入新命令节点失败";
    EXPECT_TRUE(editMenu->childCount() == 4) << "CommandModel 的编辑未能如实写回底层 CommandLayout";

    EXPECT_TRUE(model.removeRows(0, 1, editMenuIdx))
        << "通过 CommandModel::removeRows 删除节点失败";
    EXPECT_TRUE(editMenu->childCount() == 3) << "删除后底层 CommandLayout 子节点数未能同步减少";

    // moveRows：把编辑菜单下第0项移动到第2位之后（往下移一位），验证 Qt moveRows 语义正确对接
    const int beforeRow0Type
        = model.data(model.index(0, 0, editMenuIdx), CommandModel::CommandTypeRole).toInt();
    EXPECT_TRUE(model.moveRows(editMenuIdx, 0, 1, editMenuIdx, 2))
        << "CommandModel::moveRows 执行失败";
    EXPECT_TRUE(model.data(model.index(1, 0, editMenuIdx), CommandModel::CommandTypeRole).toInt()
                == beforeRow0Type)
        << "moveRows 后节点未能落在预期的新位置";
}

TEST(CommandSystem, CommandContext)
{
    CommandManager mgr;

    // addContextAction 在上下文"已经激活"之后才调用，
    // 且 action() 是"之后才第一次被创建"——这个时序曾经导致转发连接
    // 从未建立（代理点了没反应），根因是用"权威下标是否变化"判断要不要重连。
    {
        const ContextId ctxAlreadyActive{"regression.alreadyActive"};
        const void* regressionSource = &ctxAlreadyActive; // 任意一个稳定地址即可

        mgr.pushContext(ctxAlreadyActive, regressionSource);
        EXPECT_TRUE(mgr.isActiveContext(ctxAlreadyActive)) << "上下文已提前激活";

        auto& regressionCmd = mgr.registerCommand(CommandId{"regression.cmd1"},
                                                  QStringLiteral("命令1"));
        auto* realAction    = new QAction(QStringLiteral("真实动作"), QApplication::instance());
        bool realTriggered  = false;
        QObject::connect(realAction,
                         &QAction::triggered,
                         QApplication::instance(),
                         [&realTriggered]() { realTriggered = true; });

        // 关键：先注册绑定（此时上下文已激活，但代理还不存在）
        regressionCmd.addContextAction(realAction, ctxAlreadyActive);
        // ……过一会儿才第一次创建代理（模拟"这条命令一直没被拖进任何菜单，直到
        // 自定义对话框第一次读取它的显示文本才触发 proxyAction() 创建"）
        QAction* proxy = regressionCmd.action();

        EXPECT_TRUE(proxy->isEnabled()) << "代理创建后未能正确镜像出已激活上下文的 enabled 状态";
        proxy->trigger(); // 模拟用户点击代理
        EXPECT_TRUE(realTriggered) << "点击代理未能如实转发触发真实动作";

        mgr.popContext(ctxAlreadyActive, regressionSource);
    }

    // 验证同时绑定新旧两个上下文的 Command，在旧上下文尚未失活前就已经正确切换到
    // 新上下文对应的 realAction，不会经过"无权威源"的中间态。
    {
        const ContextId ctxOld{"regression.focusOld"};
        const ContextId ctxNew{"regression.focusNew"};
        const void* widgetOld = &ctxOld;
        const void* widgetNew = &ctxNew;

        auto& regressionCmd2 = mgr.registerCommand(CommandId{"regression.cmd2"},
                                                   QStringLiteral("命令2"));
        auto* realOld = new QAction(QStringLiteral("旧编辑器动作"), QApplication::instance());
        auto* realNew = new QAction(QStringLiteral("新编辑器动作"), QApplication::instance());
        regressionCmd2.addContextAction(realOld, ctxOld);
        regressionCmd2.addContextAction(realNew, ctxNew);

        mgr.pushContext(ctxOld, widgetOld); // 模拟旧编辑器已获得焦点
        QAction* proxy2 = regressionCmd2.action();
        EXPECT_TRUE(proxy2->text() == realOld->text()) << "初始状态代理未能正常镜像旧编辑器的动作";

        // 先 push 新的……
        mgr.pushContext(ctxNew, widgetNew);
        EXPECT_TRUE(proxy2->text() == realNew->text())
            << "push 新上下文后（旧的还未 pop），代理未能切换到新编辑器的动作";
        // ……再 pop 旧的
        mgr.popContext(ctxOld, widgetOld);
        EXPECT_TRUE(proxy2->text() == realNew->text())
            << "pop 旧上下文后代理未能正确停留在新编辑器的动作";
        EXPECT_TRUE(proxy2->isEnabled()) << "全程代理都应保持 enabled（不应出现中间的无权威源态）";

        mgr.popContext(ctxNew, widgetNew);
    }
}

inline constexpr char kContextIdPropertyName[] = "bakuon_contextId";

inline void setWidgetContext(QObject* widget, const ContextId& context)
{
    Q_ASSERT(widget != nullptr);
    widget->setProperty(kContextIdPropertyName, context.value());
}

inline ContextId widgetContext(const QObject* widget)
{
    const QVariant v = widget->property(kContextIdPropertyName);
    return v.isValid() ? ContextId{v.toString()} : ContextId{};
}

class FocusContextWatcher final : public QObject
{
public:
    explicit FocusContextWatcher(CommandManager& manager, QObject* parent = nullptr)
        : QObject(parent)
        , m_cmdManager(manager)
    {
    }

protected:
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (event->type() != QEvent::FocusIn) {
            return QObject::eventFilter(watched, event);
        }

        auto* widget = qobject_cast<QWidget*>(watched);
        if (!widget || widget->focusPolicy() == Qt::NoFocus) {
            return QObject::eventFilter(watched, event);
        }

        QWidget* providerWidget = nullptr;
        ContextId foundContext;
        while (widget) {
            ContextId ctx = widgetContext(widget);
            if (ctx.isValid()) {
                providerWidget = widget;
                foundContext   = ctx;
                break;
            }
            widget = widget->parentWidget();
        }

        if (providerWidget == m_focusedProviderWidget.data()) {
            return QObject::eventFilter(watched,
                                        event); // 焦点仍在同一个已标记部件内部转移，无需变更
        }

        QWidget* const oldWidget   = m_focusedProviderWidget.data();
        const ContextId oldContext = m_focusedContext;

        if (providerWidget) {
            m_cmdManager.pushContext(foundContext, providerWidget);
        }
        if (oldWidget) {
            m_cmdManager.popContext(oldContext, oldWidget);
        }

        m_focusedProviderWidget = providerWidget; // 为 nullptr 表示焦点完全离开了所有已标记部件
        m_focusedContext        = providerWidget ? foundContext : ContextId{};

        return QObject::eventFilter(watched, event);
    }

private:
    CommandManager& m_cmdManager;
    // 当前因焦点而处于激活状态的 (widget, context)，焦点转移时用于精确 pop 掉旧的引用
    QPointer<QWidget> m_focusedProviderWidget;
    ContextId m_focusedContext;
};

TEST(CommandSystem, ContextFocus)
{
    // 验证 FocusContextWatcher 能正确过滤掉两类噪声 FocusIn，
    // 不会把它们误判成"焦点离开了所有已标记部件"而错误地 pop 掉当前上下文：
    //   (1) 非 QWidget 的 QObject（第一个 check 本身就会经过这类事件——
    //       在 offscreen 平台下 container->show()/setActiveWindow() 期间
    //       Qt 会先向内部样式对象、QWidgetWindow 等发送 FocusIn，这曾经
    //       是实际压垮过这个修复第一版的场景）；
    //   (2) focusPolicy() == Qt::NoFocus 的 QWidget（典型如 QMainWindow 自身），
    //       用下面的合成事件直接、确定性地复现。
    CommandManager mgr;

    const ContextId ctxProvider{"regression.nofocusguard.provider"};

    auto* watcher = new FocusContextWatcher(mgr, QApplication::instance());
    QApplication::instance()->installEventFilter(watcher);

    // 用一个顶层容器窗口承载两个子部件——在 offscreen 平台下，孤立的顶层
    // QWidget 不一定能可靠地成为"激活窗口"，作为同一个窗口的子部件之间
    // 切换焦点则没有这个问题。
    auto* container      = new QWidget();
    auto* providerWidget = new QWidget(container);
    providerWidget->setFocusPolicy(Qt::StrongFocus);
    setWidgetContext(providerWidget, ctxProvider);

    auto* adminWidget = new QWidget(container); // 模拟 QMainWindow 这类默认 NoFocus 的部件
    adminWidget->setFocusPolicy(Qt::NoFocus);

    container->show();
    container->activateWindow(); // QApplication::setActiveWindow(container);
    QCoreApplication::processEvents();

    providerWidget->setFocus(Qt::MouseFocusReason); // 真实的焦点转移：应当 push ctxProvider
    QCoreApplication::processEvents();
    EXPECT_TRUE(mgr.isActiveContext(ctxProvider)) << "获得焦点后上下文未能正确激活";

    // 直接向 adminWidget 发送一个合成的 FocusIn——模拟 Qt 内部补发的噪声事件，
    // 不经过真正的 setFocus() 流程（那样对一个 NoFocus 部件通常也不会生效），
    // 但仍然会被安装在 qApp 上的全局事件过滤器接收到。
    QFocusEvent noiseEvent(QEvent::FocusIn, Qt::ActiveWindowFocusReason);
    QCoreApplication::sendEvent(adminWidget, &noiseEvent);

    EXPECT_TRUE(mgr.isActiveContext(ctxProvider))
        << "NoFocus 部件的噪声 FocusIn 不应把刚激活的上下文 pop 掉";

    QApplication::instance()->removeEventFilter(watcher);
    mgr.releaseContext(providerWidget);
    delete container;
    delete watcher;
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
