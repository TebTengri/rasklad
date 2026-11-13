TEMPLATE = app
TARGET = client_adapter_tests
QT = core gui qml quick testlib
CONFIG += console c++17 warn_on
CONFIG -= app_bundle
include(../../src/rasklad_hardening.pri)
INCLUDEPATH += ../../src
SOURCES += client_adapter_tests.cpp
HEADERS += clientadapter.h
LIBS += -L$$OUT_PWD/../src -lrasklad_core
