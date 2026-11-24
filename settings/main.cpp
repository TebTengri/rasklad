// SPDX-License-Identifier: MIT
// SYS-227: rasklad-settings, the settings window for the rasklad Fcitx addon.
//
// Turns correction on or off per application and switches the layout sound.
// Changes go to Fcitx over the session D-Bus (Controller1 SetConfig), which the
// addon applies at once and saves itself; the window then reads the result
// back. It offers the program names the addon saw with a focused text field.
// No typed text, no network, no autostart, no files written by this process.

#include "appiconprovider.h"
#include "jsontranslator.h"
#include "settingsbackend.h"
#include "sources.h"
#include "themewatcher.h"

#include <QCoreApplication>
#include <QFile>
#include <QGuiApplication>
#include <QLocalServer>
#include <QLocalSocket>
#include <QLockFile>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QTextStream>
#include <QThread>
#include <QWindow>

#include <sys/stat.h>
#include <unistd.h>

using namespace rasklad;

namespace {
// $XDG_RUNTIME_DIR only when it is an absolute, real directory owned by this user
// and closed to group and others (mode 0700); otherwise empty: no single-instance.
QString privateRuntimeDir()
{
    const QByteArray dir = qgetenv("XDG_RUNTIME_DIR");
    if (dir.isEmpty() || !dir.startsWith('/')) return {};
    struct stat st {};
    if (::lstat(dir.constData(), &st) != 0 || !S_ISDIR(st.st_mode) || st.st_uid != ::getuid() ||
        (st.st_mode & 077) != 0)
        return {};
    return QString::fromLocal8Bit(dir);
}

// Removes the socket left by a crashed window: only a socket, never another file.
void removeStaleSocket(const QString &path)
{
    struct stat st {};
    const QByteArray p = QFile::encodeName(path);
    if (::lstat(p.constData(), &st) == 0 && S_ISSOCK(st.st_mode) && st.st_uid == ::getuid())
        ::unlink(p.constData());
}
} // namespace

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

    // SYS-261: one settings window. The first start holds a lock file for its whole
    // life and listens on a socket; a later start finds the lock taken, asks the
    // first window to come forward over the socket, and exits. Only in a runtime
    // directory that is this user's own and closed to others (XDG basedir rules).
    const QString runtime = privateRuntimeDir();
    QLockFile lock(runtime + QStringLiteral("/rasklad-settings.lock"));
    QLocalServer single;
    if (!runtime.isEmpty()) {
        const QString socketPath = runtime + QStringLiteral("/rasklad-settings.socket");
        if (lock.tryLock(0)) {
            removeStaleSocket(socketPath); // we hold the lock: no live window owns it
            single.setSocketOptions(QLocalServer::UserAccessOption);
            single.listen(socketPath); // on failure: still a working window, only no single-instance
        } else if (lock.error() == QLockFile::LockFailedError) {
            // The first window may still be starting: try for up to two seconds.
            for (int attempt = 0; attempt < 10; ++attempt) {
                QLocalSocket probe;
                probe.connectToServer(socketPath);
                if (probe.waitForConnected(200)) {
                    probe.write("focus\n");
                    probe.waitForBytesWritten(200);
                    return 0;
                }
                QThread::msleep(100);
            }
            return 0; // a window holds the lock but does not answer: never a second one
        }
        // Any other lock error (unwritable dir): run without single-instance.
    }

    app::ThemeWatcher theme(app::defaultThemeSources(), QString(), true);
    settings::DBusFcitx fcitx;
    settings::FcitxSeenApps seen;
    // SYS-254: human names from .desktop entries, in the window's language.
    settings::DesktopAppNames names([&theme] { return theme.language(); });
    settings::RaskladSetup setup;
    settings::SettingsBackend backend(&fcitx, &seen, &names, &setup);

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
    // SYS-254: Omarchy apps round corners like Hyprland windows (decoration:rounding).
    engine.rootContext()->setContextProperty(QStringLiteral("corner"), settings::hyprlandRounding());
    engine.addImageProvider(QStringLiteral("appicon"), new settings::AppIconProvider); // engine owns it
    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreationFailed, &app, [] { QCoreApplication::exit(1); },
        Qt::QueuedConnection);
    engine.load(QUrl(QStringLiteral("qrc:/Main.qml")));
    if (engine.rootObjects().isEmpty()) return 1;
    auto *window = qobject_cast<QWindow *>(engine.rootObjects().constFirst());
    QObject::connect(&single, &QLocalServer::newConnection, &app, [&single, window] {
        while (QLocalSocket *peer = single.nextPendingConnection()) {
            peer->disconnectFromServer(); // the message is only "come forward"
            peer->deleteLater();
        }
        if (!window) return;
        window->show();
        window->raise();
        // Wayland lets a compositor refuse requestActivate; Hyprland focuses on request.
        if (!settings::hyprlandFocusPid(QCoreApplication::applicationPid()))
            window->requestActivate(); // outside Hyprland: ask the compositor, the window stays open either way
    });
    const int code = app.exec();
    if (installed) QCoreApplication::removeTranslator(&ru);
    return code;
}
