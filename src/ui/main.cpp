#include "ui/MainWindow.h"

#include <QApplication>
#include <QIcon>

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    app.setStyle("Fusion");
    app.setWindowIcon(QIcon(":/assets/icons/logo.svg"));
    blockforge::MainWindow w;
    w.resize(1200, 720);
    w.show();
    return app.exec();
}
