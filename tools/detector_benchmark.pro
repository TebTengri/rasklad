# Offline CLI независимой оценки детектора (SYS-233). Не GUI, не служба, не
# устанавливается; не входит в runtime приложения. Данные, отчёты и
# артефакты — только в build/, см. docs/EXPERIMENT-002-METHOD.md.
TEMPLATE = app
TARGET = detector_benchmark

QT = core
CONFIG += console c++17 warn_on
CONFIG -= app_bundle

DEFINES += \
    QT_NO_CAST_FROM_ASCII \
    QT_NO_CAST_TO_ASCII \
    QT_NO_KEYWORDS

# Общий каталог сборки build/tools с detector_experiment: объектные файлы
# разделены, чтобы параллельная сборка не писала одни и те же .o.
OBJECTS_DIR = detector_benchmark_obj
MOC_DIR = detector_benchmark_obj

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
    frontcodedlexicon.h
SOURCES += \
    detector_benchmark.cpp \
    benchmarkeval.cpp \
    benchmarkio.cpp \
    benchmarkprotocol.cpp \
    detectordataset.cpp \
    detectormanifest.cpp \
    evaluationdata.cpp \
    frontcodedlexicon.cpp
