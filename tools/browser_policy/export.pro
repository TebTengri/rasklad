# Build-time evaluator of the existing native router; not a runtime service.
TEMPLATE = app
TARGET = browser_policy_export
QT = core
CONFIG += console c++17 warn_on
CONFIG -= app_bundle
include(../../src/rasklad_hardening.pri)
INCLUDEPATH += ../../src
SOURCES += synthetic.cpp export.cpp ../../src/decisionrouter.cpp ../../src/layoutconverter.cpp ../../src/ngramdetector.cpp
HEADERS += synthetic.h ../../src/decisionrouter.h ../../src/layoutconverter.h ../../src/ngramdetector.h
