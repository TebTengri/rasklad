# SYS-239 (only with pkg-config hunspell): production profile ports compared
# with the research helpers (removePfxSfxRows, isQueryableToken,
# verifyArtifactMetadata), and the real LocalEvidenceProvider loaded end to end
# from a synthetic fixture tree with tiny artificial .aff/.dic files. The
# installed system Hunspell is used; nothing is downloaded. No generated
# research data, corpus or real dictionaries are read.
TEMPLATE = app
TARGET = profile_tests

QT = core testlib
CONFIG += console c++17 warn_on link_pkgconfig
CONFIG -= app_bundle
PKGCONFIG += hunspell
LIBS += -ldl

DEFINES += \
    QT_NO_CAST_FROM_ASCII \
    QT_NO_CAST_TO_ASCII \
    QT_NO_KEYWORDS \
    RASKLAD_WITH_HUNSPELL

INCLUDEPATH += $$PWD/../../src $$PWD/../../tools $$PWD/../common
DEPENDPATH += $$PWD/../../src $$PWD/../../tools

LIBS += -L$$OUT_PWD/../../src -lrasklad_core
PRE_TARGETDEPS += $$OUT_PWD/../../src/librasklad_core.a

# Research helpers (reference side), same list as tests/domain.
HEADERS += \
    $$PWD/../../tools/benchmarkeval.h \
    $$PWD/../../tools/detectormanifest.h \
    $$PWD/../../tools/domaindecisions.h \
    $$PWD/../../tools/evaluationdata.h \
    $$PWD/../../tools/frontcodedlexicon.h \
    $$PWD/../../tools/hunspellengine.h \
    $$PWD/../../tools/morphologydata.h \
    $$PWD/../../tools/morphologyeval.h
SOURCES += \
    $$PWD/../../tools/benchmarkeval.cpp \
    $$PWD/../../tools/benchmarkio.cpp \
    $$PWD/../../tools/benchmarkprotocol.cpp \
    $$PWD/../../tools/detectormanifest.cpp \
    $$PWD/../../tools/domaindecisions.cpp \
    $$PWD/../../tools/evaluationdata.cpp \
    $$PWD/../../tools/frontcodedlexicon.cpp \
    $$PWD/../../tools/hunspellengine.cpp \
    $$PWD/../../tools/morphologydata.cpp \
    $$PWD/../../tools/morphologyeval.cpp

# Production side.
HEADERS += \
    $$PWD/../../src/localprofile.h \
    $$PWD/../../src/profileverify.h \
    $$PWD/../../src/spellengine.h \
    $$PWD/../common/profilefixture.h
SOURCES += profile_tests.cpp \
    $$PWD/../../src/localprofile.cpp \
    $$PWD/../../src/profileverify.cpp \
    $$PWD/../../src/spellengine.cpp
