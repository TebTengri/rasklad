# SPDX-License-Identifier: MIT
# Lab build recipe only. Upstream sources remain LGPL-2.1-or-later.
TEMPLATE = lib
TARGET = waylandim
CONFIG += plugin c++20 link_pkgconfig warn_on
CONFIG -= qt app_bundle
QT -= core gui
QMAKE_PREFIX_PLUGIN = lib
PKGCONFIG += Fcitx5Core Fcitx5Utils Fcitx5Config wayland-client xkbcommon
SOURCE = /lab/source
# Installed matching SDK first; source fallback is for private *_p headers only.
INCLUDEPATH += /usr/include/Fcitx5/Core /usr/include/Fcitx5/Utils /usr/include/Fcitx5/Config
INCLUDEPATH += /usr/include/Fcitx5/Module/fcitx-module/wayland /usr/include/Fcitx5/Module/fcitx-module/dbus
INCLUDEPATH += $$OUT_PWD $$OUT_PWD/protocols $$SOURCE/src/lib
INCLUDEPATH += $$SOURCE/src/frontend/waylandim $$SOURCE/src/frontend/virtualinputcontext
WAYLAND_DIRS = core input-method input-method-v2 plasma-window-management wlr-foreign-toplevel-management-unstable-v1 ext-foreign-toplevel-list cosmic-toplevel-info-unstable-v1
for(part, WAYLAND_DIRS) {
 INCLUDEPATH += $$SOURCE/src/lib/fcitx-wayland/$$part
 SOURCES += $$files($$SOURCE/src/lib/fcitx-wayland/$$part/*.cpp)
}
INCLUDEPATH += $$SOURCE/src/lib/fcitx-wayland
SOURCES += $$files($$SOURCE/src/frontend/waylandim/*.cpp)
SOURCES += $$files($$SOURCE/src/frontend/virtualinputcontext/*.cpp)
SOURCES += $$files($$OUT_PWD/protocols/*-protocol.c)
DEFINES += _GNU_SOURCE HAVE_PIPE2=1
QMAKE_CXXFLAGS += -fvisibility=hidden -fvisibility-inlines-hidden
QMAKE_LFLAGS += -Wl,--no-undefined
