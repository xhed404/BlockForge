#include "ui/MainWindow.h"

#include <QApplication>

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    blockforge::MainWindow w;
    w.resize(1200, 720);
    w.show();
    return app.exec();
}

