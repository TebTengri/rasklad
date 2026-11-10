# Модульные тесты чистого ядра. Собирается из корневого rasklad.pro,
# который сначала собирает src/rasklad_core.pro.
TEMPLATE = app
TARGET = rasklad_tests

QT = core testlib
CONFIG += console c++17 warn_on
CONFIG -= app_bundle

DEFINES += \
    QT_NO_CAST_FROM_ASCII \
    QT_NO_CAST_TO_ASCII \
    QT_NO_KEYWORDS

INCLUDEPATH += $$PWD/../src
DEPENDPATH += $$PWD/../src

LIBS += -L$$OUT_PWD/../src -lrasklad_core
PRE_TARGETDEPS += $$OUT_PWD/../src/librasklad_core.a

SOURCES += rasklad_tests.cpp
