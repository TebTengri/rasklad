TEMPLATE = app
TARGET = rasklad-wayland-keys
CONFIG += console c++20 link_pkgconfig
CONFIG -= qt app_bundle
PKGCONFIG += wayland-client xkbcommon

VK_XML = $$PWD/protocol/virtual-keyboard-unstable-v1.xml
VK_HEADER = $$OUT_PWD/virtual-keyboard-client.h
VK_CODE = $$OUT_PWD/virtual-keyboard-code.c

# Generate protocol glue at qmake time so SOURCES exist; no installation.
!system(wayland-scanner client-header $$shell_quote($$VK_XML) $$shell_quote($$VK_HEADER)): \
    error("wayland-scanner client-header failed")
!system(wayland-scanner private-code $$shell_quote($$VK_XML) $$shell_quote($$VK_CODE)): \
    error("wayland-scanner private-code failed")

INCLUDEPATH += $$OUT_PWD
HEADERS += $$VK_HEADER
SOURCES += keys.cpp $$VK_CODE
