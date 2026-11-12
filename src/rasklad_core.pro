# Чистое ядро без GUI, I/O и системного ввода.
TEMPLATE = lib
TARGET = rasklad_core

QT = core
CONFIG += staticlib c++17 warn_on
CONFIG -= app_bundle
include(rasklad_hardening.pri)

DEFINES += \
    QT_NO_CAST_FROM_ASCII \
    QT_NO_CAST_TO_ASCII \
    QT_NO_KEYWORDS

HEADERS += decisionrouter.h layoutconverter.h ngramdetector.h
SOURCES += decisionrouter.cpp layoutconverter.cpp ngramdetector.cpp
