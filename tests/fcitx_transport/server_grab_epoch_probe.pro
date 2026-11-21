# SPDX-License-Identifier: MIT
TEMPLATE = app
TARGET = server_grab_epoch_probe
CONFIG += console c++20 link_pkgconfig warn_on
CONFIG -= qt app_bundle
QT -= core gui
PKGCONFIG += wayland-server
QMAKE_LFLAGS += -Wl,--no-as-needed
SOURCES = $$PWD/server_grab_epoch_probe.cpp
HEADERS = $$PWD/server_route_fake.h
LIBS += -ldl -lpthread
