# Модульные тесты router-политик и парсеров опыта 006 (SYS-238): протокол 006
# по точному SHA-256, аргументы с --shard, паспорт с шардами, общие ворота,
# таблицы истинности, сдвиг контекста single/burst, неизменность helper'ов
# 005 и формат вывода. Только синтетические fixtures в памяти;
# сгенерированные признаки, labels, корпус, словари и модель не читаются. Без
# Hunspell: собирается всегда.
TEMPLATE = app
TARGET = router_tests

QT = core testlib
CONFIG += console c++17 warn_on
CONFIG -= app_bundle

DEFINES += \
    QT_NO_CAST_FROM_ASCII \
    QT_NO_CAST_TO_ASCII \
    QT_NO_KEYWORDS

# Корень исходников: тест сверяет замороженные протоколы 005 и 006 из репозитория.
DEFINES += RASKLAD_SOURCE_ROOT=\\\"$$clean_path($$PWD/../..)\\\"

INCLUDEPATH += $$PWD/../../src $$PWD/../../tools
DEPENDPATH += $$PWD/../../src $$PWD/../../tools

LIBS += -L$$OUT_PWD/../../src -lrasklad_core
PRE_TARGETDEPS += $$OUT_PWD/../../src/librasklad_core.a

HEADERS += \
    $$PWD/../../tools/contextpolicy.h \
    $$PWD/../../tools/detectormanifest.h \
    $$PWD/../../tools/routerpolicy.h
SOURCES += router_tests.cpp \
    $$PWD/../../tools/contextpolicy.cpp \
    $$PWD/../../tools/detectormanifest.cpp \
    $$PWD/../../tools/routerpolicy.cpp
