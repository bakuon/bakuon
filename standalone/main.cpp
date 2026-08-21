#pragma once

#include <QtCore/QTimer>
#include <QtWidgets/QApplication>

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);

    QTimer::singleShot(3000, &app, SLOT(quit()));

    return app.exec();
}
