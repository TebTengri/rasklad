# Offline CLI эксперимента детектора (SYS-232). Не GUI, не служба, не
# устанавливается; не входит в runtime приложения. Данные и отчёты — только
# в build/, см. docs/DETECTOR_EXPERIMENT.md.
TEMPLATE = app
TARGET = detector_experiment

QT = core
CONFIG += console c++17 warn_on
CONFIG -= app_bundle

DEFINES += \
    QT_NO_CAST_FROM_ASCII \
    QT_NO_CAST_TO_ASCII \
    QT_NO_KEYWORDS

INCLUDEPATH += $$PWD/../src
DEPENDPATH += $$PWD/../src

LIBS += -L$$OUT_PWD/../src -lrasklad_core
PRE_TARGETDEPS += $$OUT_PWD/../src/librasklad_core.a

HEADERS += detectordataset.h detectormanifest.h
SOURCES += detector_experiment.cpp detectordataset.cpp detectormanifest.cpp
