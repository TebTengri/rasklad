# SPDX-License-Identifier: MIT
TEMPLATE = lib
TARGET = raskladserverroute
CONFIG += plugin c++20 link_pkgconfig warn_on no_plugin_name_prefix
CONFIG -= qt app_bundle
QT -= core gui
PKGCONFIG += wayland-server
QMAKE_PREFIX_PLUGIN =
DEFINES += RASKLAD_GRAB_EPOCH_WITNESS_LAB
SOURCES = $$PWD/server_route_witness.cpp
HEADERS = $$PWD/server_grab_epoch.h
QMAKE_CXXFLAGS += -fvisibility=hidden -fvisibility-inlines-hidden
QMAKE_LFLAGS += -Wl,--no-undefined -Wl,--version-script,$$PWD/server_grab_epoch_witness.map
LIBS += -ldl -lpthread
