# rasklad manual-preview window (SYS-239). Built in build/src next to the
# core library, with its own Makefile (Makefile.rasklad_app, set by the root
# project) and object directory. Not installed: run it with bin/run.
TEMPLATE = app
TARGET = rasklad

QT = core gui qml quick quickcontrols2
CONFIG += c++17 warn_on
CONFIG -= app_bundle

DEFINES += \
    QT_NO_CAST_FROM_ASCII \
    QT_NO_CAST_TO_ASCII \
    QT_NO_KEYWORDS

OBJECTS_DIR = rasklad_app_obj
MOC_DIR = rasklad_app_obj
RCC_DIR = rasklad_app_obj

include(rasklad_app.pri)
include(rasklad_ui.pri)

LIBS += -L$$OUT_PWD -lrasklad_core
PRE_TARGETDEPS += $$OUT_PWD/librasklad_core.a

SOURCES += main.cpp

# No install target: the binary stays in the build directory.

# Audio output only; the player is initialized on explicit demand.
QT += multimedia
HEADERS += retrosoundplayer.h
SOURCES += retrosoundplayer.cpp
