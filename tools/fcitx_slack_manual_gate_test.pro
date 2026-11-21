# SPDX-License-Identifier: MIT
# Offline closed-lab twin of fcitx_slack_manual_lab.pro (fixture program identity).
include(fcitx_slack_gate_test.pro)
TARGET = raskladslackmanualgatetest
DEFINES += RASKLAD_MANUAL_LAB
HEADERS += $$PWD/../src/manualgesture.h
