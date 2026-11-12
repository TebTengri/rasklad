# Standalone assertion client; NOT a runtime backend or a compositor.
TEMPLATE = app
TARGET = native_field_client
QT = core gui qml quick
CONFIG += c++17 console warn_on
CONFIG -= app_bundle
include(../../src/rasklad_hardening.pri)
SOURCES += main.cpp
RESOURCES += client.qrc
