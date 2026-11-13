TEMPLATE = app
TARGET = composition_tests
QT = core testlib
CONFIG += console c++17 warn_on
CONFIG -= app_bundle
include(../../src/rasklad_hardening.pri)
INCLUDEPATH += ../../src
SOURCES += composition_tests.cpp composer.cpp
HEADERS += composer.h
LIBS += -L$$OUT_PWD/../src -lrasklad_core
