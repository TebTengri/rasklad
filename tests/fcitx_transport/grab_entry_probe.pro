# SPDX-License-Identifier: MIT
TEMPLATE = app
TARGET = grab_entry_probe
CONFIG += console c++20 link_pkgconfig warn_on
CONFIG -= qt app_bundle
QT -= core gui
PKGCONFIG += Fcitx5Core Fcitx5Utils wayland-client
INCLUDEPATH += $$PWD/../../build/chromium-ime/grab-entry-shim/generated
SOURCES = $$PWD/grab_entry_probe.cpp
HEADERS = $$PWD/grab_test_api.h
# The shim resolves the true Fcitx entry but the probe never invokes it.
QMAKE_LFLAGS += -Wl,--no-as-needed
LIBS += -ldl -lpthread
