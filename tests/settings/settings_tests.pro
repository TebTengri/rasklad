# SPDX-License-Identifier: MIT
TEMPLATE = app
TARGET = settings_tests
QT = core testlib
CONFIG += console c++17 testcase
CONFIG -= app_bundle
DEFINES += QT_NO_KEYWORDS
INCLUDEPATH += $$PWD/../../settings $$PWD/../../src
HEADERS += $$PWD/../../settings/settingsbackend.h $$PWD/../../src/seenprograms.h
SOURCES += settings_tests.cpp \
    $$PWD/../../settings/settingscore.cpp \
    $$PWD/../../settings/settingsbackend.cpp
