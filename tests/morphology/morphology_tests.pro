# Модульные тесты эксперимента 003 (SYS-235): протокол 003, паспорт
# морфологии, контроль .aff без PFX/SFX, обёртка Hunspell, девять методов,
# общая метка неоднозначности и тип с точным регистром. Fixtures .aff/.dic
# искусственные и создаются в QTemporaryDir; системные словари и корпус не
# нужны. Собирается только при наличии pkg-config hunspell.
TEMPLATE = app
TARGET = morphology_tests

QT = core testlib
CONFIG += console c++17 warn_on link_pkgconfig
CONFIG -= app_bundle
PKGCONFIG += hunspell
LIBS += -ldl

DEFINES += \
    QT_NO_CAST_FROM_ASCII \
    QT_NO_CAST_TO_ASCII \
    QT_NO_KEYWORDS

# Корень исходников: тест сверяет замороженный протокол 003 из репозитория.
DEFINES += RASKLAD_SOURCE_ROOT=\\\"$$clean_path($$PWD/../..)\\\"

INCLUDEPATH += $$PWD/../../src $$PWD/../../tools
DEPENDPATH += $$PWD/../../src $$PWD/../../tools

LIBS += -L$$OUT_PWD/../../src -lrasklad_core
PRE_TARGETDEPS += $$OUT_PWD/../../src/librasklad_core.a

HEADERS += \
    $$PWD/../../tools/benchmarkeval.h \
    $$PWD/../../tools/benchmarkio.h \
    $$PWD/../../tools/benchmarkprotocol.h \
    $$PWD/../../tools/detectormanifest.h \
    $$PWD/../../tools/evaluationdata.h \
    $$PWD/../../tools/frontcodedlexicon.h \
    $$PWD/../../tools/hunspellengine.h \
    $$PWD/../../tools/morphologydata.h \
    $$PWD/../../tools/morphologyeval.h
SOURCES += morphology_tests.cpp \
    $$PWD/../../tools/benchmarkeval.cpp \
    $$PWD/../../tools/benchmarkio.cpp \
    $$PWD/../../tools/benchmarkprotocol.cpp \
    $$PWD/../../tools/detectormanifest.cpp \
    $$PWD/../../tools/evaluationdata.cpp \
    $$PWD/../../tools/frontcodedlexicon.cpp \
    $$PWD/../../tools/hunspellengine.cpp \
    $$PWD/../../tools/morphologydata.cpp \
    $$PWD/../../tools/morphologyeval.cpp
