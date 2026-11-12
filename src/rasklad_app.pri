# Shared non-UI sources of the manual-preview app (SYS-239): document
# controller, analysis worker, verified local profile, theme follower, JSON
# translator and options. Included by src/rasklad_app.pro and the app tests.
# The including project links librasklad_core.a itself.
#
# The local profile shares only the pure tools/frontcodedlexicon reader with
# the research code; corpus, benchmark CLI and feature parsers are not linked.
# Hunspell is optional: without pkg-config hunspell (or with
# CONFIG+=rasklad_no_hunspell) the app builds manual-only.

INCLUDEPATH += $$PWD $$PWD/../tools
include(rasklad_hardening.pri)
DEPENDPATH += $$PWD $$PWD/../tools

HEADERS += \
    $$PWD/analysisservice.h \
    $$PWD/appoptions.h \
    $$PWD/documentcontroller.h \
    $$PWD/jsontranslator.h \
    $$PWD/localprofile.h \
    $$PWD/profileverify.h \
    $$PWD/themewatcher.h \
    $$PWD/../tools/frontcodedlexicon.h

SOURCES += \
    $$PWD/analysisservice.cpp \
    $$PWD/appoptions.cpp \
    $$PWD/documentcontroller.cpp \
    $$PWD/jsontranslator.cpp \
    $$PWD/localprofile.cpp \
    $$PWD/profileverify.cpp \
    $$PWD/themewatcher.cpp \
    $$PWD/../tools/frontcodedlexicon.cpp

!rasklad_no_hunspell:packagesExist(hunspell) {
    CONFIG += link_pkgconfig
    PKGCONFIG += hunspell
    # dladdr for the loaded-library check (part of libc in glibc >= 2.34).
    LIBS += -ldl
    DEFINES += RASKLAD_WITH_HUNSPELL
    HEADERS += $$PWD/spellengine.h
    SOURCES += $$PWD/spellengine.cpp
}

HEADERS += $$PWD/soundfeedback.h
SOURCES += $$PWD/soundfeedback.cpp
