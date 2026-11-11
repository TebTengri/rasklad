# Модульные тесты политик и парсеров опыта 005 (SYS-237): протокол 005 по
# точному SHA-256, аргументы, паспорт оценки, строгий TSV признаков, голос
# контекста, шесть политик и формат вывода. Только синтетические fixtures в
# памяти; сгенерированные признаки, labels, корпус, словари и модель не
# читаются. Без Hunspell: собирается всегда.
TEMPLATE = app
TARGET = context_tests

QT = core testlib
CONFIG += console c++17 warn_on
CONFIG -= app_bundle

DEFINES += \
    QT_NO_CAST_FROM_ASCII \
    QT_NO_CAST_TO_ASCII \
    QT_NO_KEYWORDS

# Корень исходников: тест сверяет замороженный протокол 005 из репозитория.
DEFINES += RASKLAD_SOURCE_ROOT=\\\"$$clean_path($$PWD/../..)\\\"

INCLUDEPATH += $$PWD/../../src $$PWD/../../tools
DEPENDPATH += $$PWD/../../src $$PWD/../../tools

LIBS += -L$$OUT_PWD/../../src -lrasklad_core
PRE_TARGETDEPS += $$OUT_PWD/../../src/librasklad_core.a

HEADERS += \
    $$PWD/../../tools/contextpolicy.h \
    $$PWD/../../tools/detectormanifest.h
SOURCES += context_tests.cpp \
    $$PWD/../../tools/contextpolicy.cpp \
    $$PWD/../../tools/detectormanifest.cpp
