# SPDX-License-Identifier: MIT
TEMPLATE = app
TARGET = desktopindex_tests
QT = core testlib
CONFIG += console c++17 testcase
CONFIG -= app_bundle
DEFINES += QT_NO_KEYWORDS
DEFINES += FIXTURES_DIR=\\\"$$PWD/fixtures\\\"
INCLUDEPATH += $$PWD/../../settings $$PWD/../../src
HEADERS += $$PWD/../../settings/desktopindex.h
SOURCES += desktopindex_tests.cpp \
    $$PWD/../../settings/desktopindex.cpp
