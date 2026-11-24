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

#include <cerrno>
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
        (st.st_mode & 07777) != 0700)
        return {};
    return QString::fromLocal8Bit(dir);
}

// Frees the socket path for a new listener: true when nothing is there, or when
// it held a leftover socket of ours and that is now removed. Never touches another file.
bool removeStaleSocket(const QString &path)
{
    struct stat st {};
    const QByteArray p = QFile::encodeName(path);
    if (::lstat(p.constData(), &st) != 0) return errno == ENOENT;
    return S_ISSOCK(st.st_mode) && st.st_uid == ::getuid() && ::unlink(p.constData()) == 0;
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
        bool owner = lock.tryLock(0);
        if (!owner && lock.error() == QLockFile::LockFailedError) {
            // Another window holds the lock; it may still be starting, or closing.
            // Try for up to two seconds: bring it forward, or take the lock it left.
            for (int attempt = 0; attempt < 10 && !owner; ++attempt) {
                QLocalSocket probe;
                probe.connectToServer(socketPath);
                if (probe.waitForConnected(200)) {
                    // Done only when the window answers: one that is closing may still
                    // accept the connection, and then this start takes its place.
                    probe.write("focus\n");
                    if (probe.waitForBytesWritten(200) && probe.waitForReadyRead(1000) &&
                        probe.readAll().startsWith("ok"))
                        return 0;
                }
                QThread::msleep(100);
                owner = lock.tryLock(0);
            }
            if (!owner) return 0; // a window holds the lock but does not answer: never a second one
        }
        // We hold the lock: no live window owns the socket path. Listen only when the
        // path is free or a leftover socket (never replace another file); if listening
        // fails, give the lock up so a later start opens its own window.
        if (owner && !(removeStaleSocket(socketPath) && [&] {
                single.setSocketOptions(QLocalServer::UserAccessOption);
                return single.listen(socketPath);
            }()))
            lock.unlock();
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
    // Closing: stop answering at once, so a new start opens its own window.
    QObject::connect(&app, &QCoreApplication::aboutToQuit, &single, &QLocalServer::close);
    QObject::connect(&single, &QLocalServer::newConnection, &app, [&single, window] {
        while (QLocalSocket *peer = single.nextPendingConnection()) {
            // The message is only "come forward"; the answer says this window is alive.
            peer->write("ok\n");
            peer->flush();
            peer->disconnectFromServer();
            QObject::connect(peer, &QLocalSocket::disconnected, peer, &QObject::deleteLater);
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
