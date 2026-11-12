# SYS-239: offscreen Qt Quick smoke test of Main.qml with the real controller,
# a fake in-memory word provider and fictional text. Drives the window through
# its controls (no real desktop, input devices or clipboard reads), checks the
# rejected-edit reset, EN/RU translation and the narrow layout, and saves
# screenshots with QQuickWindow::grabWindow into build/tests/ui/screenshots.
# Run with QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software (the test sets
# them itself if they are unset).
TEMPLATE = app
TARGET = ui_tests

QT = core gui qml quick quickcontrols2 testlib
CONFIG += console c++17 warn_on
CONFIG -= app_bundle

DEFINES += \
    QT_NO_CAST_FROM_ASCII \
    QT_NO_CAST_TO_ASCII \
    QT_NO_KEYWORDS

# Screenshots stay inside the ignored build tree.
DEFINES += RASKLAD_SCREENSHOT_DIR=\\\"$$clean_path($$OUT_PWD/screenshots)\\\"

include(../../src/rasklad_app.pri)
include(../../src/rasklad_ui.pri)

LIBS += -L$$OUT_PWD/../../src -lrasklad_core
PRE_TARGETDEPS += $$OUT_PWD/../../src/librasklad_core.a

SOURCES += ui_tests.cpp
