# SPDX-License-Identifier: MIT
# SYS-246 product Fcitx5 addon (docs/SYS-246-PRODUCT-ADDON.md): manual double-Shift
# gesture and SYS-226 automatic correction, applications from the addon configuration. Produces
# $$OUT_PWD/rasklad.so (Library=rasklad in addon/rasklad.conf). The build never
# installs anything; installation is packaging (SYS-228) with owner approval.

TEMPLATE = lib
TARGET = rasklad
CONFIG += plugin c++20 link_pkgconfig warn_on
CONFIG += qt
CONFIG -= app_bundle
QT = core
PKGCONFIG += Fcitx5Core Fcitx5Utils Fcitx5Config hunspell alsa
INCLUDEPATH += $$PWD/../src $$PWD/../tools

DESTDIR = $$OUT_PWD
DEFINES += RASKLAD_PRODUCT RASKLAD_MANUAL_LAB RASKLAD_WITH_HUNSPELL QT_NO_KEYWORDS

SOURCES += $$PWD/../src/raskladproduct.cpp \
    $$PWD/../src/applicationpolicy.cpp \
    $$PWD/../src/applicationpermissions.cpp \
    $$PWD/../src/compositiondisable.cpp \
    $$PWD/../src/fcitxlabprofile.cpp \
    $$PWD/../src/layoutcue.cpp \
    $$PWD/../src/layoutconverter.cpp \
    $$PWD/../src/ngramdetector.cpp \
    $$PWD/../src/decisionrouter.cpp \
    $$PWD/../src/profileverify.cpp \
    $$PWD/../src/localprofile.cpp \
    $$PWD/../src/spellengine.cpp \
    $$PWD/../tools/frontcodedlexicon.cpp
LIBS += -ldl -lpthread
HEADERS += $$PWD/../src/manualgesture.h $$PWD/../src/wipe.h

QMAKE_CXXFLAGS += -fvisibility=hidden -fvisibility-inlines-hidden
QMAKE_LFLAGS += -Wl,--no-undefined

QMAKE_PREFIX_PLUGIN =
CONFIG += no_plugin_name_prefix
