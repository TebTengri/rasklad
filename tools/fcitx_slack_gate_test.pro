# SPDX-License-Identifier: MIT
# Offline gate-test artifact (never installed, never used with Slack): the
# Slack session gates against the owned Chromium fixture in closed Sway.
include(fcitx_slack_lab.pro)
TARGET = raskladslackgatetest
DEFINES += RASKLAD_SLACK_GATE_TEST_PROGRAM
