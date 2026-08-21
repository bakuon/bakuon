#include "MainWindow.h"
#include "Constants.h"
#include "CustomizeMenuDialog.h"
#include "EditorSurface.h"

#include <QtWidgets/QStatusBar>
#include <QtWidgets/QTabWidget>

namespace bakuon::examples {

MainWindow::MainWindow()
    : QMainWindow(nullptr)
{
    setWindowTitle(QStringLiteral("上下文感知命令系统 - 菜单自定义布局示例"));
    resize(760, 520);

    registerCommands(); // 先建立完整的命令目录（存在性 + 行为），与菜单布局的加载顺序无关

    auto* tabs    = new QTabWidget(this);
    m_imageCanvas = new EditorSurface(QStringLiteral("图像编辑器"),
                                      kImageFocused,
                                      kImageObjectSelected,
                                      tabs);
    m_scene3d     = new EditorSurface(QStringLiteral("3D 编辑器"),
                                      kScene3dFocused,
                                      kScene3dObjectSelected,
                                      tabs);
    m_imageCanvas->setObjectName(QStringLiteral("Image Editor"));
    m_scene3d->setObjectName(QStringLiteral("3D Editor"));
    tabs->addTab(m_imageCanvas, QStringLiteral("图像编辑"));
    tabs->addTab(m_scene3d, QStringLiteral("3D 编辑"));
    setCentralWidget(tabs);

    buildDefaultMenuLayout(); // CommandManager -> CommandModel -> MenuBarBuilder -> QMenuBar
    buildToolBar();           // 工具栏本次不纳入自定义范围，仍直接摆放代理 QAction

    gui::CommandSystem::pushContext(kCtxGlobal, this);
    statusBar()->showMessage(
        QStringLiteral("提示：菜单(&V)视图 -> 自定义菜单布局…，可拖拽/重排/存盘"));
}

MainWindow::~MainWindow()
{
    gui::CommandSystem::releaseContext(this);
}

void MainWindow::registerCommands()
{
    auto& del = gui::CommandSystem::registerCommand(kCmdDelete, QStringLiteral("删除"));
    del.setShortcut(QKeySequence::Delete);
    del.setDefaultIcon(QIcon::fromTheme(QIcon::ThemeIcon::EditDelete));

    auto& dup = gui::CommandSystem::registerCommand(kCmdDuplicate, QStringLiteral("复制"));
    dup.setShortcut(QKeySequence(QStringLiteral("Ctrl+D")));
    dup.setAttribute(gui::Command::Attribute::HideWhenIdle, true);
    dup.setDefaultIcon(QIcon::fromTheme(QIcon::ThemeIcon::EditCopy));

    auto& save = gui::CommandSystem::registerCommand(kCmdSave, QStringLiteral("保存"));
    save.setShortcut(QKeySequence::Save);
    save.setDefaultIcon(QIcon::fromTheme(QIcon::ThemeIcon::DocumentSave));

    auto& customize = gui::CommandSystem::registerCommand(kCmdCustomize, QStringLiteral("自定义"));
    customize.setShortcut(QKeySequence(Qt::Key_O));

    m_saveRealAction = new QAction(QStringLiteral("保存"), this);
    connect(m_saveRealAction, &QAction::triggered, this, [this]() {
        statusBar()->showMessage(QStringLiteral("已保存"), 2000);
    });

    m_customizeAction = new QAction(QStringLiteral("自定义菜单布局…"), this);
    connect(m_customizeAction, &QAction::triggered, this, [this]() {
        CustomizeMenuDialog dlg(m_menuModel, this);
        dlg.exec();
    });

    save.addContextAction(m_saveRealAction, kCtxGlobal);
    customize.addContextAction(m_customizeAction, kCtxGlobal);
}

void MainWindow::buildDefaultMenuLayout()
{
    using Item             = gui::CommandLayout::Item;
    m_menuLayout           = new gui::CommandLayout;
    // TreeNode::insertChildAt 对越界下标的内置回退语义就是"追加到末尾"，
    // 用一个具名常量表达"追加"意图，比裸的 (size_t)-1 更清楚。
    constexpr auto kAppend = static_cast<std::size_t>(-1);

    Item* fileMenu = m_menuLayout->addMenu(nullptr, kAppend, QStringLiteral("文件(&F)"));
    m_menuLayout->addCommand(fileMenu, kAppend, kCmdSave);

    Item* editMenu = m_menuLayout->addMenu(nullptr, kAppend, QStringLiteral("编辑(&E)"));
    m_menuLayout->addCommand(editMenu, kAppend, kCmdDelete);
    m_menuLayout->addSeparator(editMenu, kAppend);
    m_menuLayout->addCommand(editMenu, kAppend, kCmdDuplicate);

    Item* viewMenu = m_menuLayout->addMenu(nullptr, kAppend, QStringLiteral("视图(&V)"));
    m_menuLayout->addCommand(viewMenu, kAppend, kCmdCustomize);

    // 默认布局搭好之后，才创建 CommandModel 包一层——供"自定义菜单布局"对话框使用。
    m_menuModel = new gui::CommandModel(m_menuLayout, this);

    // 模型任何结构性变化（拖拽/上移下移/删除/改名/整体 loadLayoutFromFile 重置）
    // 都重新整体渲染一次菜单栏，让"自定义菜单布局"对话框里的编辑实时生效。
    auto rebuild = [this]() { rebuildMenuBar(); };
    connect(m_menuModel, &QAbstractItemModel::dataChanged, this, rebuild);
    connect(m_menuModel, &QAbstractItemModel::rowsInserted, this, rebuild);
    connect(m_menuModel, &QAbstractItemModel::rowsRemoved, this, rebuild);
    connect(m_menuModel, &QAbstractItemModel::rowsMoved, this, rebuild);
    connect(m_menuModel, &QAbstractItemModel::modelReset, this, rebuild);

    rebuildMenuBar();
}

void MainWindow::rebuildMenuBar()
{
    gui::CommandSystem::renderMenuBar(m_menuLayout, menuBar());
}

void MainWindow::buildToolBar()
{
    auto* toolBar = addToolBar(QStringLiteral("常用操作"));
    toolBar->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    toolBar->addAction(gui::CommandSystem::command(kCmdDelete)->action());
    toolBar->addAction(gui::CommandSystem::command(kCmdDuplicate)->action());
}

} // namespace bakuon::examples
