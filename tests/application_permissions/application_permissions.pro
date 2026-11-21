# SPDX-License-Identifier: MIT
TEMPLATE = app
TARGET = application_permissions_tests
CONFIG += console c++17 warn_on
CONFIG -= qt app_bundle
QT -= core gui
INCLUDEPATH += $$PWD/../../src
SOURCES += $$PWD/../application_policy/permissions_tests.cpp $$PWD/../../src/applicationpermissions.cpp $$PWD/../../src/applicationpolicy.cpp
HEADERS += $$PWD/../../src/applicationpermissions.h
