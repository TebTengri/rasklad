# SPDX-License-Identifier: MIT
# SYS-255: the shared configuration rules and the word rules of automatic
# correction (pure headers), plus the policy's once-per-revision registry check.
TEMPLATE = app
TARGET = product_core_tests
CONFIG += console c++20 warn_on
CONFIG -= qt app_bundle
QT -= core gui
INCLUDEPATH += $$PWD/../../src
SOURCES += product_core_tests.cpp $$PWD/../../src/applicationpolicy.cpp
HEADERS += $$PWD/../../src/configrules.h $$PWD/../../src/wordrules.h $$PWD/../../src/wipe.h $$PWD/../../src/applicationpolicy.h
