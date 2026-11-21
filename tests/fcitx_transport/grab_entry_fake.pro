# SPDX-License-Identifier: MIT
TEMPLATE = lib
TARGET = grab_entry_fake
CONFIG += plugin c++20 link_pkgconfig warn_on
CONFIG -= qt app_bundle
QT -= core gui
PKGCONFIG += wayland-client
QMAKE_PREFIX_PLUGIN =
CONFIG += no_plugin_name_prefix
INCLUDEPATH += $$PWD/../../build/chromium-ime/grab-entry-shim/generated
SOURCES = $$PWD/grab_entry_fake.cpp
HEADERS = $$PWD/grab_test_api.h
QMAKE_CXXFLAGS += -fvisibility=hidden
