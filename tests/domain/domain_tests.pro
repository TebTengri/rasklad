# Модульные тесты helper'ов опыта 004 (SYS-236): протокол 004 по точному
# SHA-256, паспорт случаев, строгий TSV, девять действий с oracle-защитой,
# формат вывода и metadata закреплённых артефактов. Только синтетические
# fixtures в памяти; корпус, словари и артефакты не читаются. Собирается
# вместе с tools/domain_decisions (при наличии pkg-config hunspell); сам тест
# Hunspell не вызывает.
TEMPLATE = app
TARGET = domain_tests

QT = core testlib
CONFIG += console c++17 warn_on link_pkgconfig
CONFIG -= app_bundle
PKGCONFIG += hunspell
LIBS += -ldl

DEFINES += \
    QT_NO_CAST_FROM_ASCII \
    QT_NO_CAST_TO_ASCII \
    QT_NO_KEYWORDS

# Корень исходников: тест сверяет замороженный протокол 004 из репозитория.
DEFINES += RASKLAD_SOURCE_ROOT=\\\"$$clean_path($$PWD/../..)\\\"

INCLUDEPATH += $$PWD/../../src $$PWD/../../tools
DEPENDPATH += $$PWD/../../src $$PWD/../../tools

LIBS += -L$$OUT_PWD/../../src -lrasklad_core
PRE_TARGETDEPS += $$OUT_PWD/../../src/librasklad_core.a

HEADERS += \
    $$PWD/../../tools/benchmarkeval.h \
    $$PWD/../../tools/detectormanifest.h \
    $$PWD/../../tools/domaindecisions.h \
    $$PWD/../../tools/evaluationdata.h \
    $$PWD/../../tools/frontcodedlexicon.h \
    $$PWD/../../tools/hunspellengine.h \
    $$PWD/../../tools/morphologydata.h \
    $$PWD/../../tools/morphologyeval.h
SOURCES += domain_tests.cpp \
    $$PWD/../../tools/benchmarkeval.cpp \
    $$PWD/../../tools/benchmarkio.cpp \
    $$PWD/../../tools/benchmarkprotocol.cpp \
    $$PWD/../../tools/detectormanifest.cpp \
    $$PWD/../../tools/domaindecisions.cpp \
    $$PWD/../../tools/evaluationdata.cpp \
    $$PWD/../../tools/frontcodedlexicon.cpp \
    $$PWD/../../tools/hunspellengine.cpp \
    $$PWD/../../tools/morphologydata.cpp \
    $$PWD/../../tools/morphologyeval.cpp
