#include <QtWidgets/QApplication>

#include <gui/b_contextfocusrouter.h>

#include "MainWindow.h"

using namespace bakuon::examples;

int main(int argc, char** argv)
{
    QApplication app(argc, argv);

    auto* router = new bakuon::gui::ContextFocusRouter(&app);
    router->install();

    MainWindow window;
    window.show();

    return app.exec();
}