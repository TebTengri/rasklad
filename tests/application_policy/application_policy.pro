# SPDX-License-Identifier: MIT
TEMPLATE = app
TARGET = application_policy_tests
CONFIG += console c++17 warn_on
CONFIG -= qt app_bundle
QT -= core gui
INCLUDEPATH += $$PWD/../../src
SOURCES += $$PWD/application_policy_tests.cpp $$PWD/../../src/applicationpolicy.cpp
HEADERS += $$PWD/../../src/applicationpolicy.h
