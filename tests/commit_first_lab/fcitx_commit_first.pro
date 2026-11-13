TEMPLATE = lib
TARGET = rskcommit
QT = core
CONFIG += plugin c++20 link_pkgconfig warn_on
CONFIG -= app_bundle
DEFINES += QT_NO_KEYWORDS
PKGCONFIG += Fcitx5Core
INCLUDEPATH += ../../src
SOURCES += fcitx_commit_first.cpp
LIBS += -L$$OUT_PWD/../src -lrasklad_core
include(../../src/rasklad_hardening.pri)
