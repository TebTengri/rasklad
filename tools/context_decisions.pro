# Developer-only CLI опыта 005 (SYS-237): per-row действия шести
# замороженных политик на синтетических признаках. Не GUI, не служба, не
# устанавливается и не входит в runtime приложения. Без Hunspell, модели,
# словарей и корпуса: только ядро (canonicalizeToken) и проверенные готовые
# признаки. Вывод — только в stdout.
TEMPLATE = app
TARGET = context_decisions

QT = core
CONFIG += console c++17 warn_on
CONFIG -= app_bundle

DEFINES += \
    QT_NO_CAST_FROM_ASCII \
    QT_NO_CAST_TO_ASCII \
    QT_NO_KEYWORDS

# Общий build/tools с другими CLI: отдельный каталог объектов.
OBJECTS_DIR = context_decisions_obj
MOC_DIR = context_decisions_obj

INCLUDEPATH += $$PWD/../src
DEPENDPATH += $$PWD/../src

LIBS += -L$$OUT_PWD/../src -lrasklad_core
PRE_TARGETDEPS += $$OUT_PWD/../src/librasklad_core.a

HEADERS += \
    benchmarkio.h \
    contextpolicy.h \
    detectormanifest.h
SOURCES += \
    context_decisions.cpp \
    benchmarkio.cpp \
    contextpolicy.cpp \
    detectormanifest.cpp
