# Original synthetic full/control gold, no research dictionaries or model.
TEMPLATE = app
TARGET = native_spell_fixture
QT = core
CONFIG += console c++17 warn_on link_pkgconfig
CONFIG -= app_bundle
PKGCONFIG += hunspell
include(../../src/rasklad_hardening.pri)
INCLUDEPATH += ../../src ../../tools
SOURCES += native.cpp ../../src/profileverify.cpp ../../src/ngramdetector.cpp ../../src/layoutconverter.cpp ../frontcodedlexicon.cpp
HEADERS += ../../src/profileverify.h ../../src/ngramdetector.h ../../src/layoutconverter.h ../frontcodedlexicon.h
