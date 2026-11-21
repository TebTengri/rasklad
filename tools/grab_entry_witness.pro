# SPDX-License-Identifier: MIT
TEMPLATE = lib
TARGET = raskladcoreentry
CONFIG += plugin c++20 link_pkgconfig warn_on
CONFIG -= qt app_bundle
QT -= gui core
PKGCONFIG += Fcitx5Core Fcitx5Utils wayland-client
QMAKE_PREFIX_PLUGIN =
CONFIG += no_plugin_name_prefix
DEFINES += RASKLAD_GRAB_ENTRY_WITNESS_LAB
SOURCES += $$PWD/core_entry_witness.cpp
INCLUDEPATH += $$OUT_PWD/generated
GRAB_XML = $$PWD/../tests/input_lab/protocol/input-method-unstable-v2.xml
grab_header.input = GRAB_XML
grab_header.output = $$OUT_PWD/generated/input-method-v2-client-protocol.h
grab_header.commands = /usr/bin/python3 $$shell_quote($$PWD/generate_grab_header.py) $$shell_quote($$OUT_PWD/generated/input-method-v2-client-protocol.h)
grab_header.depends = $$PWD/generate_grab_header.py
grab_header.CONFIG += no_link target_predeps
QMAKE_EXTRA_COMPILERS += grab_header
QMAKE_CXXFLAGS += -fvisibility=hidden -fvisibility-inlines-hidden
QMAKE_LFLAGS += -Wl,--no-undefined -Wl,--version-script,$$PWD/grab_entry_witness.map
LIBS += -ldl -lpthread
