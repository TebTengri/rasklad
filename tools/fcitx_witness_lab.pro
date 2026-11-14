# SPDX-License-Identifier: MIT
# Developer-only Space-delivery witness variant of the PROFILE_LAB Fcitx LAB
# addon (RASKLAD_WITNESS_LAB). Never installed; no UI/app/autostart.
# Adds one read-only GetStats D-Bus method, registered only inside the fixed
# private lab environment. Default and plain profile variants are unaffected.
# Build in its own build directory: TARGET is inherited unchanged so the
# runner-provided diagnostic addon conf (with the dbus dependency) can load it.

include(fcitx_profile_lab.pro)

PKGCONFIG += Fcitx5Module

DEFINES += RASKLAD_WITNESS_LAB

HEADERS += $$PWD/fcitxlabwitness.h
