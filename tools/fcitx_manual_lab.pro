# SPDX-License-Identifier: MIT
# SYS-225 slice 1: separate optional lab artifact (Chromium fixture identity,
# no automatic correction, double-Shift preedit toggle). Not product code.
include(fcitx_chromium_lab.pro)
DEFINES += RASKLAD_MANUAL_LAB
HEADERS += $$PWD/../src/manualgesture.h $$PWD/../src/wipe.h
