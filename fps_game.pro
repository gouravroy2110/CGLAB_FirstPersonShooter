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
    multiplayer.cpp

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
    weapons.h \
    gunbuild.h \
    maps.h \
    pixelfont.h \
    audio.h \
    multiplayer.h

FORMS +=

# Suppress deprecation warnings from Qt internals
DEFINES += QT_DEPRECATED_WARNINGS

# Default rules for deployment
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target
