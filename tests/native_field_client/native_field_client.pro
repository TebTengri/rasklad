# Standalone assertion client; NOT a runtime backend or a compositor.
TEMPLATE = app
TARGET = native_field_client
QT = core gui qml quick
CONFIG += c++17 console warn_on
CONFIG -= app_bundle
include(../../src/rasklad_hardening.pri)
SOURCES += main.cpp
RESOURCES += client.qrc

HEADERS += timingobserver.h

HEADERS += lifecycleactions.h

INCLUDEPATH += ../../src ../client_adapter_lab
HEADERS += ../client_adapter_lab/clientadapter.h
LIBS += -L$$OUT_PWD/../src -lrasklad_core
