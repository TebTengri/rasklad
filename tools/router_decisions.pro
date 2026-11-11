# Developer-only CLI опыта 006 (SYS-238): per-row действия восьми
# замороженных router-политик для одного шарда признаков. Не GUI, не служба,
# не устанавливается и не входит в runtime приложения. Без Hunspell, модели,
# словарей и корпуса: только ядро (canonicalizeToken), неизменённые helper'ы
# опыта 005 и проверенные готовые признаки. Вывод — только в stdout.
TEMPLATE = app
TARGET = router_decisions

QT = core
CONFIG += console c++17 warn_on
CONFIG -= app_bundle

DEFINES += \
    QT_NO_CAST_FROM_ASCII \
    QT_NO_CAST_TO_ASCII \
    QT_NO_KEYWORDS

# Общий build/tools с другими CLI: отдельный каталог объектов.
OBJECTS_DIR = router_decisions_obj
MOC_DIR = router_decisions_obj

INCLUDEPATH += $$PWD/../src
DEPENDPATH += $$PWD/../src

LIBS += -L$$OUT_PWD/../src -lrasklad_core
PRE_TARGETDEPS += $$OUT_PWD/../src/librasklad_core.a

HEADERS += \
    benchmarkio.h \
    contextpolicy.h \
    detectormanifest.h \
    routerpolicy.h
SOURCES += \
    router_decisions.cpp \
    benchmarkio.cpp \
    contextpolicy.cpp \
    detectormanifest.cpp \
    routerpolicy.cpp
