# SPDX-License-Identifier: MIT
TEMPLATE = app
TARGET = application_off_tests
CONFIG += console c++20 link_pkgconfig
CONFIG -= qt app_bundle
QT -= core gui
PKGCONFIG += Fcitx5Core Fcitx5Utils
DEFINES += RASKLAD_APPLICATION_OFF_TEST RASKLAD_CHROMIUM_LAB
SOURCES += $$PWD/application_off_tests.cpp $$PWD/../../src/applicationpolicy.cpp $$PWD/../../src/applicationpermissions.cpp $$PWD/../../src/compositiondisable.cpp
QMAKE_CXXFLAGS += -fsanitize=address -fno-omit-frame-pointer -g
QMAKE_LFLAGS += -fsanitize=address
