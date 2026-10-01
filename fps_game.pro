QT       += core gui multimedia

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
    world.cpp

HEADERS += \
    mainwindow.h \
    game.h \
    raster.h \
    assets.h \
    gunmodels.h \
    maps.h \
    pixelfont.h

FORMS +=

# Suppress deprecation warnings from Qt internals
DEFINES += QT_DEPRECATED_WARNINGS

# Default rules for deployment
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target
