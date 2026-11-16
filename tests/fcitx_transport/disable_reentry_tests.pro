# SPDX-License-Identifier: MIT
TEMPLATE = app
TARGET = disable_reentry_tests
CONFIG += console c++20 link_pkgconfig
CONFIG -= qt app_bundle
QT -= core gui
PKGCONFIG += Fcitx5Core Fcitx5Utils
DEFINES += RASKLAD_DISABLE_REENTRY_TEST
SOURCES += $$PWD/disable_reentry_tests.cpp $$PWD/../../src/applicationpolicy.cpp $$PWD/../../src/compositiondisable.cpp
QMAKE_CXXFLAGS += -fsanitize=address -fno-omit-frame-pointer -g
QMAKE_LFLAGS += -fsanitize=address
