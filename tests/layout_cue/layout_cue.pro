# SPDX-License-Identifier: MIT
TEMPLATE = app
TARGET = layout_cue_tests
CONFIG += console c++20 link_pkgconfig
CONFIG -= qt app_bundle
PKGCONFIG += alsa
INCLUDEPATH += $$PWD/../../src
SOURCES += layout_cue_tests.cpp $$PWD/../../src/layoutcue.cpp
