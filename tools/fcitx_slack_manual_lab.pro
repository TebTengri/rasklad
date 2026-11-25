# SPDX-License-Identifier: MIT
# SYS-225 supervised-Slack-session LAB artifact with the manual double-Shift gesture
# (no automatic correction). Same arm pipe, receipts and commit gate as fcitx_slack_lab.pro.
include(fcitx_slack_lab.pro)
TARGET = raskladslackmanual
DEFINES += RASKLAD_MANUAL_LAB
HEADERS += $$PWD/../src/manualgesture.h $$PWD/../src/wipe.h
