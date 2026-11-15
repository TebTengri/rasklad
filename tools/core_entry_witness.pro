# SPDX-License-Identifier: MIT
# Project-local diagnostic shim; loaded ONLY by the private Fcitx child.
TEMPLATE = lib
TARGET = raskladcoreentry
CONFIG += plugin c++20 link_pkgconfig warn_on
CONFIG -= qt app_bundle
QT -= gui core
PKGCONFIG += Fcitx5Core Fcitx5Utils
DESTDIR = $$OUT_PWD
QMAKE_PREFIX_PLUGIN =
CONFIG += no_plugin_name_prefix
SOURCES += $$PWD/core_entry_witness.cpp
QMAKE_CXXFLAGS += -fvisibility=hidden -fvisibility-inlines-hidden
QMAKE_LFLAGS += -Wl,--no-undefined -Wl,--version-script,$$PWD/core_entry_witness.map
LIBS += -ldl -lpthread
