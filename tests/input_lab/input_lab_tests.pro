# SYS-223 input-method-v2 wire laboratory (developer-only). Standalone project:
# NOT part of rasklad.pro, bin/build or bin/test, and not linked with the app.
# A real libwayland server and IM client talk over a socketpair inside one test
# process; no listening socket, no session display, no devices, invented text.
# Build and run only through bin/test-input-lab: it verifies the vendored XML
# hash and generates wayland-scanner code into build/input_lab/generated.
TEMPLATE = app
TARGET = input_lab_tests

QT = core testlib
CONFIG += console c++17 warn_on link_pkgconfig
CONFIG -= app_bundle
PKGCONFIG += wayland-server wayland-client

DEFINES += \
    QT_NO_CAST_FROM_ASCII \
    QT_NO_CAST_TO_ASCII \
    QT_NO_KEYWORDS

isEmpty(INPUT_LAB_GENERATED): \
    error("input_lab_tests: INPUT_LAB_GENERATED is not set; run bin/test-input-lab")

LAB_SERVER_HEADER = $$INPUT_LAB_GENERATED/input-method-unstable-v2-server-protocol.h
LAB_CLIENT_HEADER = $$INPUT_LAB_GENERATED/input-method-unstable-v2-client-protocol.h
LAB_PROTOCOL_CODE = $$INPUT_LAB_GENERATED/input-method-unstable-v2-protocol.c
LAB_GENERATED_FILES = $$LAB_SERVER_HEADER $$LAB_CLIENT_HEADER $$LAB_PROTOCOL_CODE
for(generated, LAB_GENERATED_FILES) {
    !exists($$generated): error("input_lab_tests: missing $$generated; run bin/test-input-lab")
}

INCLUDEPATH += $$INPUT_LAB_GENERATED

HEADERS += \
    labclient.h \
    labharness.h \
    labmodel.h \
    labserver.h

# labserver.cpp is the only unit that includes wayland-server; labclient.cpp the
# only one that includes wayland-client. Tests see neither.
SOURCES += \
    labclient.cpp \
    labharness.cpp \
    labmodel.cpp \
    labserver.cpp \
    tst_input_lab.cpp \
    $$LAB_PROTOCOL_CODE
