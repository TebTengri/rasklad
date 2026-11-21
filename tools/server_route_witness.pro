# SPDX-License-Identifier: MIT
TEMPLATE = lib
TARGET = raskladserverroute
CONFIG += plugin c++20 link_pkgconfig warn_on no_plugin_name_prefix
CONFIG -= qt app_bundle
QT -= core gui
PKGCONFIG += wayland-server
QMAKE_PREFIX_PLUGIN =
SOURCES = $$PWD/server_route_witness.cpp
QMAKE_CXXFLAGS += -fvisibility=hidden -fvisibility-inlines-hidden
QMAKE_LFLAGS += -Wl,--no-undefined -Wl,--version-script,$$PWD/server_route_witness.map
LIBS += -ldl -lpthread
