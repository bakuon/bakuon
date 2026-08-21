#include <QtWidgets/QApplication>

#include "FocusContextWatcher.h"
#include "MainWindow.h"

using namespace bakuon::examples;

int main(int argc, char** argv)
{
    QApplication app(argc, argv);

    auto* watcher = new FocusContextWatcher(&app);
    app.installEventFilter(watcher);

    MainWindow window;
    window.show();

    return app.exec();
}