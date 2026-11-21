# SPDX-License-Identifier: MIT
TEMPLATE = app
TARGET = composition_disable_tests
CONFIG += console c++17 warn_on
CONFIG -= qt app_bundle
QT -= core gui
INCLUDEPATH += $$PWD/../../src
SOURCES += $$PWD/../application_policy/disable_tests.cpp $$PWD/../../src/compositiondisable.cpp $$PWD/../../src/applicationpolicy.cpp
HEADERS += $$PWD/../../src/compositiondisable.h $$PWD/../../src/applicationpolicy.h
