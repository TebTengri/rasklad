TEMPLATE = lib
TARGET = rskcompose
QT = core
CONFIG += plugin c++20 link_pkgconfig warn_on
CONFIG -= app_bundle
DEFINES += QT_NO_KEYWORDS
PKGCONFIG += Fcitx5Core
INCLUDEPATH += ../../src
SOURCES += fcitx_engine.cpp composer.cpp
HEADERS += composer.h
LIBS += -L$$OUT_PWD/../src -lrasklad_core
include(../../src/rasklad_hardening.pri)
