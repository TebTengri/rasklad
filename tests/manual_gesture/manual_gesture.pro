# SPDX-License-Identifier: MIT
# SYS-225 slice 1: double-Shift detector and us->ru table (pure logic).
TEMPLATE = app
TARGET = manual_gesture_tests
CONFIG += console c++20
CONFIG -= app_bundle
QT = core
INCLUDEPATH += $$PWD/../../src
SOURCES += gesture_tests.cpp $$PWD/../../src/layoutconverter.cpp
HEADERS += $$PWD/../../src/manualgesture.h $$PWD/../../src/layoutconverter.h $$PWD/../../src/wipe.h
