# SPDX-License-Identifier: MIT
TEMPLATE = app
TARGET = fcitx-drain-server
CONFIG += console c++20 link_pkgconfig warn_on
CONFIG -= qt app_bundle
QT -= core gui
PKGCONFIG += wayland-server xkbcommon
INCLUDEPATH += $$OUT_PWD
SOURCES = $$PWD/server.cpp $$OUT_PWD/im-protocol.c $$OUT_PWD/vk-protocol.c
HEADERS = $$OUT_PWD/im-server.h $$OUT_PWD/vk-server.h
