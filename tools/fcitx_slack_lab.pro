# SPDX-License-Identifier: MIT
# Separate supervised-Slack-session LAB artifact (never installed): program
# "slack", supervisor arm pipe (RASKLAD_SESSION_ARM_FD), commit text gate.
# Distinct library name so it can never be swapped with the default lab addon.
include(fcitx_lab.pro)
TARGET = raskladslacklab
DEFINES += RASKLAD_SLACK_SESSION_LAB
HEADERS += $$PWD/../src/sessionarm.h
