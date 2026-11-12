# Qt Quick bootstrap shared by the app and the offscreen UI smoke test
# (SYS-239): write-only clipboard, language switcher, Main.qml and the RU table.

QT += gui qml quick quickcontrols2

HEADERS += \
    $$PWD/clipboardwriter.h \
    $$PWD/uisetup.h

SOURCES += \
    $$PWD/clipboardwriter.cpp \
    $$PWD/uisetup.cpp

RESOURCES += $$PWD/rasklad_app.qrc
