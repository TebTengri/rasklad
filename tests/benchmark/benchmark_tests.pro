# Модульные тесты helper'ов стенда SYS-233: front-coded словарь, протокол,
# паспорт и разбор Tatoeba TSV, выборка, токенизация, методы, исходы,
# испытание и границы записи артефактов. Только искусственные fixtures;
# корпус оценки и словари не нужны.
TEMPLATE = app
TARGET = benchmark_tests

QT = core testlib
CONFIG += console c++17 warn_on
CONFIG -= app_bundle

DEFINES += \
    QT_NO_CAST_FROM_ASCII \
    QT_NO_CAST_TO_ASCII \
    QT_NO_KEYWORDS

# Корень исходников: тест сверяет замороженный протокол из репозитория.
DEFINES += RASKLAD_SOURCE_ROOT=\\\"$$clean_path($$PWD/../..)\\\"

INCLUDEPATH += $$PWD/../../src $$PWD/../../tools
DEPENDPATH += $$PWD/../../src $$PWD/../../tools

LIBS += -L$$OUT_PWD/../../src -lrasklad_core
PRE_TARGETDEPS += $$OUT_PWD/../../src/librasklad_core.a

HEADERS += \
    $$PWD/../../tools/benchmarkeval.h \
    $$PWD/../../tools/benchmarkio.h \
    $$PWD/../../tools/benchmarkprotocol.h \
    $$PWD/../../tools/detectordataset.h \
    $$PWD/../../tools/detectormanifest.h \
    $$PWD/../../tools/evaluationdata.h \
    $$PWD/../../tools/frontcodedlexicon.h
SOURCES += benchmark_tests.cpp \
    $$PWD/../../tools/benchmarkeval.cpp \
    $$PWD/../../tools/benchmarkio.cpp \
    $$PWD/../../tools/benchmarkprotocol.cpp \
    $$PWD/../../tools/detectordataset.cpp \
    $$PWD/../../tools/detectormanifest.cpp \
    $$PWD/../../tools/evaluationdata.cpp \
    $$PWD/../../tools/frontcodedlexicon.cpp
