#include <QApplication>
#include "mainwindow.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName("Raster FPS");

    MainWindow win;
    win.setWindowTitle("Raster FPS  |  CG Lab  |  SCROLL=zoom  |  [/]=pixel blocks");
    win.show();

    return app.exec();
}
