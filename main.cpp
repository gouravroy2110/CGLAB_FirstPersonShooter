#include <QApplication>
#include "mainwindow.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName("Raster FPS");

    MainWindow win;

    // Developer aid: "fps_game --snapshot <dir>" renders scripted scenes to PNGs
    QStringList args = app.arguments();
    int si = args.indexOf("--snapshot");
    if (si >= 0 && si + 1 < args.size()) return win.snapshot(args[si + 1]) ? 0 : 1;

    win.loadSettings();
    win.enableAudio();
    win.setWindowTitle("Raster FPS  |  CG Lab  |  SCROLL=zoom  |  [/]=pixel blocks");
    win.show();

    return app.exec();
}
