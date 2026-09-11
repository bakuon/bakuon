#pragma once

#include <QtWidgets/QMainWindow>

#include <gui/b_commandmodel.h>
#include <gui/b_commandsystem.h>

namespace bakuon::examples {

class EditorSurface;

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow();

    ~MainWindow() override;

private:
    void registerCommands();
    // 用代码搭建一份默认菜单布局（相当于"出厂设置"）
    void buildDefaultMenuLayout();
    void rebuildMenuBar();
    void buildToolBar();

private:
    EditorSurface* m_imageCanvas     = nullptr;
    EditorSurface* m_scene3d         = nullptr;
    gui::CommandLayout* m_menuLayout = nullptr; // 纯数据层：菜单布局的真正持有者
    gui::CommandModel* m_menuModel = nullptr; // Qt 视图适配层：包装 m_menuLayout，供自定义对话框使用
    QAction* m_saveRealAction  = nullptr;
    QAction* m_customizeAction = nullptr;
};

} // namespace bakuon::examples
