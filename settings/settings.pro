# SPDX-License-Identifier: MIT
# SYS-227: rasklad-settings, the settings window for the rasklad Fcitx addon.
TEMPLATE = app
TARGET = rasklad-settings
QT = core gui qml quick quickcontrols2 dbus network
CONFIG += c++17 warn_on
CONFIG -= app_bundle
DEFINES += QT_NO_KEYWORDS RASKLAD_VERSION=\\\"0.5.21\\\"
INCLUDEPATH += $$PWD $$PWD/../src
include(../src/rasklad_hardening.pri)
HEADERS += licenses.h settingsbackend.h settingscore.h sources.h desktopindex.h appiconprovider.h \
    ../src/themewatcher.h ../src/jsontranslator.h ../src/terminalapps.h
SOURCES += main.cpp licenses.cpp settingsbackend.cpp settingscore.cpp sources.cpp desktopindex.cpp \
    ../src/themewatcher.cpp ../src/jsontranslator.cpp
RESOURCES += settings.qrc
