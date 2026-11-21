# SPDX-License-Identifier: MIT
TEMPLATE = lib
TARGET = server_route_fake
CONFIG += plugin c++20 link_pkgconfig warn_on
CONFIG -= qt app_bundle
QT -= core gui
PKGCONFIG += wayland-server
QMAKE_PREFIX_PLUGIN =
CONFIG += no_plugin_name_prefix
SOURCES = $$PWD/server_route_fake.cpp
HEADERS = $$PWD/server_route_fake.h
QMAKE_CXXFLAGS += -fvisibility=hidden
