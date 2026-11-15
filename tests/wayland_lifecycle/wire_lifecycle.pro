# SPDX-License-Identifier: MIT
TEMPLATE = app
TARGET = wire_lifecycle
CONFIG += console c++20 link_pkgconfig warn_on
CONFIG -= qt app_bundle
QT -= core gui
PKGCONFIG += wayland-client wayland-server
INCLUDEPATH += $$OUT_PWD
SOURCES = $$PWD/wire_lifecycle.cpp $$PWD/wire_client.cpp $$OUT_PWD/wire-protocol.c
HEADERS = $$PWD/wire_common.h $$OUT_PWD/wire-client.h $$OUT_PWD/wire-server.h
