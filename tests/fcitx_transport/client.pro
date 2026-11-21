# SPDX-License-Identifier: MIT
TEMPLATE = app
TARGET = rasklad-ime-lab
QT = core gui widgets dbus gui-private
CONFIG += console c++17
CONFIG -= app_bundle
SOURCES = $$PWD/client.cpp
