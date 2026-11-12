# SYS-239: production DecisionRouter — gates before evidence access, the
# decision table, reason codes and exact parity with the frozen experiment-006
# research router (tools/routerpolicy) for every combination of field,
# layout, token class, membership, ambiguity and model action, including
# invalid metadata. Synthetic tokens only; no dictionaries, model or corpus.
TEMPLATE = app
TARGET = decision_tests

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

HEADERS += \
    $$PWD/../../tools/contextpolicy.h \
    $$PWD/../../tools/detectormanifest.h \
    $$PWD/../../tools/routerpolicy.h
SOURCES += decision_tests.cpp \
    $$PWD/../../tools/contextpolicy.cpp \
    $$PWD/../../tools/detectormanifest.cpp \
    $$PWD/../../tools/routerpolicy.cpp
