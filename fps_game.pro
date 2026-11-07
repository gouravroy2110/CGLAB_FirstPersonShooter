QT       += core gui multimedia concurrent network

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

CONFIG += c++17

# Software rasterizer: always optimise
QMAKE_CXXFLAGS_RELEASE += -O3
QMAKE_CXXFLAGS_DEBUG += -O2

SOURCES += \
    main.cpp \
    mainwindow.cpp \
    assets.cpp \
    render_world.cpp \
    render_weapon.cpp \
    render_hud.cpp \
    world.cpp \
    sim.cpp \
    render_actors.cpp \
    actors.cpp \
    weapons.cpp \
    gunbuild.cpp \
    combat.cpp \
    upgrades.cpp \
    render_fx.cpp \
    menus.cpp \
    killcam.cpp \
    environment.cpp \
    editor.cpp \
    campaign.cpp \
    enemies.cpp \
    bosses.cpp \
    audio.cpp \
    multiplayer.cpp \
    stats.cpp \
    commander.cpp \
    cvinput.cpp \
    cvcontrols.cpp \
    touch.cpp \
    rogue_ai.cpp

HEADERS += \
    mainwindow.h \
    missions.h \
    game.h \
    raster.h \
    assets.h \
    gunmodels.h \
    parallel.h \
    world.h \
    sim.h \
    raster3d.h \
    soldier3d.h \
    radiolines.h \
    weapons.h \
    gunbuild.h \
    maps.h \
    pixelfont.h \
    audio.h \
    multiplayer.h \
    cvinput.h \
    rogue_brain.h

FORMS +=

# Radio voices shipped with the game (tools/bundle_voices.py writes voices/ and this file)
RESOURCES += voices.qrc
CONFIG += resources_big

# Suppress deprecation warnings from Qt internals
DEFINES += QT_DEPRECATED_WARNINGS
# CV MODE finds tools/cv_control.py through this when the game runs from a build folder elsewhere
# (a generated header: the path may contain spaces, which a -D define on the command line can't)
RASTER_SRCDIR_LINE = "$${LITERAL_HASH}define RASTER_SRC_DIR \"$$PWD\""
write_file($$OUT_PWD/raster_srcdir.h, RASTER_SRCDIR_LINE)
INCLUDEPATH += $$OUT_PWD

# --- Phones ---------------------------------------------------------------------------
# Android (Qt for Android kit): landscape, full screen, network for multiplayer
android {
    ANDROID_PACKAGE_SOURCE_DIR = $$PWD/android
    DISTFILES += android/AndroidManifest.xml
}
# iOS (Qt for iOS kit, on a Mac with Xcode): landscape, full screen, local network for multiplayer
ios {
    QMAKE_INFO_PLIST = $$PWD/ios/Info.plist
    QMAKE_TARGET_BUNDLE_PREFIX = com.cglab
    QMAKE_BUNDLE = rasterfps
}

# Default rules for deployment
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target
