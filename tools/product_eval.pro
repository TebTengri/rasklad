# SYS-272: the product's decision path on a Tatoeba export, for the dictionary
# update gate (tools/update_dictionaries.py). Offline; not installed; needs Hunspell.
TEMPLATE = app
TARGET = product_eval
QT = core
CONFIG += console c++20 warn_on link_pkgconfig
CONFIG -= app_bundle
PKGCONFIG += hunspell
LIBS += -ldl
DEFINES += QT_NO_CAST_FROM_ASCII QT_NO_CAST_TO_ASCII QT_NO_KEYWORDS RASKLAD_WITH_HUNSPELL
OBJECTS_DIR = product_eval_obj
MOC_DIR = product_eval_obj
INCLUDEPATH += $$PWD/../src $$PWD
SOURCES += product_eval.cpp \
    evaluationdata.cpp \
    detectormanifest.cpp \
    $$PWD/../src/decisionrouter.cpp \
    $$PWD/../src/profileverify.cpp \
    $$PWD/../src/localprofile.cpp \
    $$PWD/../src/spellengine.cpp \
    $$PWD/../src/ngramdetector.cpp \
    $$PWD/../src/layoutconverter.cpp \
    frontcodedlexicon.cpp
