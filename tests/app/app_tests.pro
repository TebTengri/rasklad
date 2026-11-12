# SYS-239: manual-preview logic without a window — owned document controller
# (snapshots, apply/undo, stale invalidation, oversize rejection, Unicode),
# the single-owner analysis worker with fake providers (latest-request queue,
# stale results, shutdown join), verified-profile gates on synthetic fixtures,
# Omarchy theme/language parsing, CLI options and RU translation coverage.
# No generated research data is required. Hunspell, if available, is linked
# exactly as in the app but not called by these tests.
TEMPLATE = app
TARGET = app_tests

QT = core gui testlib
CONFIG += console c++17 warn_on
CONFIG -= app_bundle

DEFINES += \
    QT_NO_CAST_FROM_ASCII \
    QT_NO_CAST_TO_ASCII \
    QT_NO_KEYWORDS

# Public repository files only (Main.qml and ru.json for translation coverage).
DEFINES += RASKLAD_SOURCE_ROOT=\\\"$$clean_path($$PWD/../..)\\\"

include(../../src/rasklad_app.pri)
INCLUDEPATH += $$PWD/../common

LIBS += -L$$OUT_PWD/../../src -lrasklad_core
PRE_TARGETDEPS += $$OUT_PWD/../../src/librasklad_core.a

HEADERS += $$PWD/../common/profilefixture.h
SOURCES += app_tests.cpp
