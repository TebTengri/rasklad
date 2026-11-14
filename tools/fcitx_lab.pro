# SPDX-License-Identifier: MIT
# SYS-244 Fcitx5 transport LAB addon. Developer prototype; never installed by
# the build. Produces $$OUT_PWD/raskladlab.so (matches Library=raskladlab
# in tests/fcitx_transport/raskladlab.conf). No Qt dependency.

TEMPLATE = lib
TARGET = raskladlab
CONFIG += plugin c++20 link_pkgconfig warn_on
CONFIG -= qt app_bundle
QT -= gui core
PKGCONFIG += Fcitx5Core Fcitx5Utils

DESTDIR = $$OUT_PWD

SOURCES += $$PWD/../src/fcitxlab.cpp

# Only the factory symbol (FCITXCORE_EXPORT) is exported; catch missing links.
QMAKE_CXXFLAGS += -fvisibility=hidden -fvisibility-inlines-hidden
QMAKE_LFLAGS += -Wl,--no-undefined


QMAKE_PREFIX_PLUGIN =
CONFIG += no_plugin_name_prefix
