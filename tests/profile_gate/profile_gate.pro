# SPDX-License-Identifier: MIT
# SYS-256: the packaging gate. PKGBUILD check() runs it on the staged profile, so a
# profile the addon would refuse (old, unmarked or altered) never becomes a package.
# Same verifier and the same production pins as the addon; no Hunspell is loaded.
TEMPLATE = app
TARGET = profile_gate
QT = core
CONFIG += console c++20 warn_on
CONFIG -= app_bundle
DEFINES += QT_NO_CAST_FROM_ASCII QT_NO_CAST_TO_ASCII QT_NO_KEYWORDS
INCLUDEPATH += $$PWD/../../src $$PWD/../../tools
SOURCES += profile_gate.cpp \
    $$PWD/../../src/profileverify.cpp \
    $$PWD/../../src/ngramdetector.cpp \
    $$PWD/../../src/layoutconverter.cpp \
    $$PWD/../../tools/frontcodedlexicon.cpp
