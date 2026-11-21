# SPDX-License-Identifier: MIT
# Developer-only Fcitx LAB addon variant bridged to the verified evidence
# core (RASKLAD_PROFILE_LAB). Never installed; no UI/app/autostart.

include(fcitx_lab.pro)

CONFIG += qt c++20
QT = core
PKGCONFIG += hunspell

DEFINES += RASKLAD_CHROMIUM_LAB RASKLAD_PROFILE_LAB RASKLAD_WITH_HUNSPELL QT_NO_KEYWORDS

INCLUDEPATH += $$PWD/../src $$PWD/../tools

SOURCES += \
    $$PWD/../src/fcitxlabprofile.cpp \
    $$PWD/../src/layoutconverter.cpp \
    $$PWD/../src/ngramdetector.cpp \
    $$PWD/../src/decisionrouter.cpp \
    $$PWD/../src/profileverify.cpp \
    $$PWD/../src/localprofile.cpp \
    $$PWD/../src/spellengine.cpp \
    $$PWD/../tools/frontcodedlexicon.cpp

LIBS += -ldl
