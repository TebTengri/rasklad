# Developer-only CLI опыта 004 (SYS-236): per-case действия девяти
# замороженных политик на техническом домене. Не GUI, не служба, не
# устанавливается и не входит в runtime приложения. Собирается только при
# наличии pkg-config hunspell (см. rasklad.pro); использует уже установленную
# системную библиотеку и ничего не скачивает. Вывод — только в stdout.
TEMPLATE = app
TARGET = domain_decisions

QT = core
CONFIG += console c++17 warn_on link_pkgconfig
CONFIG -= app_bundle
PKGCONFIG += hunspell
# dladdr для проверки загруженной библиотеки (в glibc >= 2.34 — часть libc).
LIBS += -ldl

DEFINES += \
    QT_NO_CAST_FROM_ASCII \
    QT_NO_CAST_TO_ASCII \
    QT_NO_KEYWORDS

# Общий build/tools с другими CLI: отдельный каталог объектов.
OBJECTS_DIR = domain_decisions_obj
MOC_DIR = domain_decisions_obj

INCLUDEPATH += $$PWD/../src
DEPENDPATH += $$PWD/../src

LIBS += -L$$OUT_PWD/../src -lrasklad_core
PRE_TARGETDEPS += $$OUT_PWD/../src/librasklad_core.a

HEADERS += \
    benchmarkeval.h \
    benchmarkio.h \
    benchmarkprotocol.h \
    detectormanifest.h \
    domaindecisions.h \
    evaluationdata.h \
    frontcodedlexicon.h \
    hunspellengine.h \
    morphologydata.h \
    morphologyeval.h
SOURCES += \
    domain_decisions.cpp \
    benchmarkeval.cpp \
    benchmarkio.cpp \
    benchmarkprotocol.cpp \
    detectormanifest.cpp \
    domaindecisions.cpp \
    evaluationdata.cpp \
    frontcodedlexicon.cpp \
    hunspellengine.cpp \
    morphologydata.cpp \
    morphologyeval.cpp
