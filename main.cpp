#include <QApplication>
#include <iostream>
#ifdef _WIN32
#include <windows.h>
#endif
#include "mainwindow.h"
#include "multiplayer.h"

int main(int argc, char *argv[])
{
#ifdef _WIN32
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    if (!hOut || hOut == INVALID_HANDLE_VALUE || GetFileType(hOut) == FILE_TYPE_UNKNOWN) {
        if (AttachConsole(ATTACH_PARENT_PROCESS)) {
            freopen("CONOUT$", "w", stdout);
            freopen("CONOUT$", "w", stderr);
        }
    }
    std::ios::sync_with_stdio(true);
    std::cout.clear();
    std::cerr.clear();
#endif
    QApplication app(argc, argv);
    app.setApplicationName("Raster FPS");

    QStringList args = app.arguments();

    // Automated loopback network test
    if (args.contains("--test-net")) {
        std::cout << "\n[NET TEST] Running loopback multiplayer handshake test..." << std::endl;
        std::cout.flush();
        bool ok = NetworkManager::runNetworkTest();
        if (ok) {
            std::cout << "[NET TEST] SUCCESS: Client and Host connected, synchronized slots, and exchanged packets!\n" << std::endl;
            std::cout.flush();
            return 0;
        } else {
            std::cerr << "[NET TEST] FAILED: Handshake or slot synchronization timeout.\n" << std::endl;
            std::cerr.flush();
            return 1;
        }
    }

    // Migrate settings helper
    if (args.contains("--migrate-settings")) {
        MainWindow win;
        win.loadSettings();
        return 0;
    }

    MainWindow win;

    // Developer aid: "fps_game --snapshot <dir>" renders scripted scenes to PNGs
    int si = args.indexOf("--snapshot");
    if (si >= 0 && si + 1 < args.size()) return win.snapshot(args[si + 1]) ? 0 : 1;

    // Custom player name
    int ni = args.indexOf("--name");
    if (ni >= 0 && ni + 1 < args.size()) {
        win.m_mpPlayerName = args[ni + 1];
    }

    // Direct host CLI option: --host [port]
    int hi = args.indexOf("--host");
    if (hi >= 0) {
        quint16 port = 7777;
        if (hi + 1 < args.size() && !args[hi + 1].startsWith("-")) {
            port = quint16(args[hi + 1].toUShort());
            if (port == 0) port = 7777;
        }
        win.openMultiplayerLobby();
        win.hostMultiplayer();
    }

    // Direct connect CLI option: --connect <address:port>
    int ci = args.indexOf("--connect");
    if (ci >= 0 && ci + 1 < args.size()) {
        win.m_mpInputAddr = args[ci + 1];
        win.openMultiplayerLobby();
        win.joinMultiplayer();
    }

    win.loadSettings();
    win.enableAudio();
    win.setWindowTitle("Raster FPS  |  CG Lab  |  Sector War Multiplayer Active");
    win.show();

    return app.exec();
}

