# Offline CLI эксперимента 003 (SYS-235): морфологический контроль Hunspell.
# Не GUI, не служба, не устанавливается и не входит в runtime приложения.
# Собирается только при наличии pkg-config hunspell (см. rasklad.pro);
# использует уже установленную системную библиотеку и ничего не скачивает.
# См. docs/EXPERIMENT-003-METHOD.md.
TEMPLATE = app
TARGET = morphology_benchmark

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
OBJECTS_DIR = morphology_benchmark_obj
MOC_DIR = morphology_benchmark_obj

INCLUDEPATH += $$PWD/../src
DEPENDPATH += $$PWD/../src

LIBS += -L$$OUT_PWD/../src -lrasklad_core
PRE_TARGETDEPS += $$OUT_PWD/../src/librasklad_core.a

HEADERS += \
    benchmarkeval.h \
    benchmarkio.h \
    benchmarkprotocol.h \
    detectordataset.h \
    detectormanifest.h \
    evaluationdata.h \
    frontcodedlexicon.h \
    hunspellengine.h \
    morphologydata.h \
    morphologyeval.h
SOURCES += \
    morphology_benchmark.cpp \
    benchmarkeval.cpp \
    benchmarkio.cpp \
    benchmarkprotocol.cpp \
    detectordataset.cpp \
    detectormanifest.cpp \
    evaluationdata.cpp \
    frontcodedlexicon.cpp \
    hunspellengine.cpp \
    morphologydata.cpp \
    morphologyeval.cpp
