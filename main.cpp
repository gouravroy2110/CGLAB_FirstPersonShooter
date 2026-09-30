#include <QApplication>
#include "mainwindow.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName("FPS Raster Game");

    MainWindow win;
    win.setWindowTitle("FPS Raster  |  Doom Style  |  SCROLL to zoom  |  [ ] pixel size");
    win.show();

    return app.exec();
}
