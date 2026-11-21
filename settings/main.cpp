// SPDX-License-Identifier: MIT
// SYS-227: rasklad-settings, the settings window for the rasklad Fcitx addon.
//
// Turns correction on or off per application and switches the layout sound.
// Changes go to Fcitx over the session D-Bus (Controller1 SetConfig), which the
// addon applies at once and saves itself; the window then reads the result
// back. It reads window classes from `hyprctl -j clients` to offer open apps.
// No typed text, no network, no autostart, no files written by this process.

#include "jsontranslator.h"
#include "settingsbackend.h"
#include "sources.h"
#include "themewatcher.h"

#include <QCoreApplication>
#include <QFile>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QTextStream>

using namespace rasklad;

int main(int argc, char *argv[])
{
    for (int i = 1; i < argc; ++i) {
        const QByteArray a(argv[i]);
        if (a == "--version") {
            QTextStream(stdout) << "rasklad-settings " RASKLAD_VERSION "\n";
            return 0;
        }
        if (a == "--help" || a == "-h") {
            QTextStream(stdout) << "Usage: rasklad-settings\nSettings window for the rasklad Fcitx addon.\n";
            return 0;
        }
    }
    qputenv("QML_DISABLE_DISK_CACHE", "1");
    QQuickStyle::setStyle(QStringLiteral("Material"));
    QGuiApplication app(argc, argv);
    QGuiApplication::setApplicationName(QStringLiteral("rasklad-settings"));
    QGuiApplication::setApplicationVersion(QStringLiteral(RASKLAD_VERSION));
    QGuiApplication::setDesktopFileName(QStringLiteral("rasklad-settings"));

    app::ThemeWatcher theme(app::defaultThemeSources(), QString(), true);
    settings::DBusFcitx fcitx;
    settings::HyprlandWindows windows;
    settings::SettingsBackend backend(&fcitx, &windows);

    QQmlApplicationEngine engine;
    app::JsonTranslator ru;
    {
        QFile f(QStringLiteral(":/i18n/ru.json"));
        if (f.open(QIODevice::ReadOnly)) ru.loadJson(f.read(app::kMaxTranslationBytes), QStringLiteral("ru"));
    }
    bool installed = false;
    const auto applyLanguage = [&] {
        const bool want = theme.language() == QLatin1String("ru") && !ru.isEmpty();
        if (want == installed) return;
        if (want) QCoreApplication::installTranslator(&ru);
        else QCoreApplication::removeTranslator(&ru);
        installed = want;
        engine.retranslate();
    };
    applyLanguage();
    QObject::connect(&theme, &app::ThemeWatcher::languageChanged, &app, applyLanguage);

    engine.rootContext()->setContextProperty(QStringLiteral("backend"), &backend);
    engine.rootContext()->setContextProperty(QStringLiteral("theme"), &theme);
    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreationFailed, &app, [] { QCoreApplication::exit(1); },
        Qt::QueuedConnection);
    engine.load(QUrl(QStringLiteral("qrc:/Main.qml")));
    if (engine.rootObjects().isEmpty()) return 1;
    const int code = app.exec();
    if (installed) QCoreApplication::removeTranslator(&ru);
    return code;
}
