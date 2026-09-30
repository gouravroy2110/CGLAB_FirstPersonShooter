#include <QApplication>
#include "mainwindow.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName("FPS Raster Game");

    MainWindow win;
    win.setWindowTitle("FPS Raycaster — Doom Style  |  Week 1");
    win.setFixedSize(W, H);          // lock to our framebuffer size
    win.show();

    return app.exec();
}
