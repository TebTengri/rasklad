# SPDX-License-Identifier: MIT
# Separate supervised-Slack-session LAB artifact (never installed): program
# "slack", supervisor arm pipe (RASKLAD_SESSION_ARM_FD), commit text gate.
include(fcitx_lab.pro)
DEFINES += RASKLAD_SLACK_SESSION_LAB
HEADERS += $$PWD/../src/sessionarm.h
