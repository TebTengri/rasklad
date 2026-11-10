# Модульные тесты n-граммного scorer'а и helper'ов датасета эксперимента.
# Внешние словари не нужны: используются крошечные искусственные fixtures.
TEMPLATE = app
TARGET = detector_tests

QT = core testlib
CONFIG += console c++17 warn_on
CONFIG -= app_bundle

DEFINES += \
    QT_NO_CAST_FROM_ASCII \
    QT_NO_CAST_TO_ASCII \
    QT_NO_KEYWORDS

INCLUDEPATH += $$PWD/../../src $$PWD/../../tools
DEPENDPATH += $$PWD/../../src $$PWD/../../tools

LIBS += -L$$OUT_PWD/../../src -lrasklad_core
PRE_TARGETDEPS += $$OUT_PWD/../../src/librasklad_core.a

# Тот же helper, что использует CLI, — алгоритм разделения не дублируется.
HEADERS += $$PWD/../../tools/detectordataset.h $$PWD/../../tools/detectormanifest.h
SOURCES += detector_tests.cpp \
    $$PWD/../../tools/detectordataset.cpp \
    $$PWD/../../tools/detectormanifest.cpp
