// SPDX-License-Identifier: MIT
#include "sources.h"

#include <QDeadlineTimer>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QTimer>

#include "settingscore.h"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusInterface>
#include <QDBusMessage>
#include <QDBusVariant>

#include <sys/socket.h>
#include <sys/un.h>
#include <poll.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>

namespace rasklad::settings {

namespace {

constexpr int kTimeoutMs = 800; // the UI waits on these calls; Fcitx answers in milliseconds
const QString kService = QStringLiteral("org.fcitx.Fcitx5");
const QString kUri = QStringLiteral("fcitx://config/addon/rasklad");

QDBusMessage call(const QString &method, const QVariantList &args = {})
{
    QDBusMessage m = QDBusMessage::createMethodCall(kService, QStringLiteral("/controller"),
                                                    QStringLiteral("org.fcitx.Fcitx.Controller1"), method);
    m.setArguments(args);
    return QDBusConnection::sessionBus().call(m, QDBus::Block, kTimeoutMs);
}

// Nested a{sv} as QVariantMap, string leaves as QString.
QVariant plain(const QVariant &v)
{
    if (v.metaType() == QMetaType::fromType<QDBusVariant>())
        return plain(qvariant_cast<QDBusVariant>(v).variant());
    if (v.metaType() == QMetaType::fromType<QDBusArgument>()) {
        const QDBusArgument arg = qvariant_cast<QDBusArgument>(v);
        if (arg.currentType() != QDBusArgument::MapType) return {}; // invalid: caller fails closed
        QVariantMap map;
        arg.beginMap();
        while (!arg.atEnd()) {
            QString key;
            QDBusVariant value;
            arg.beginMapEntry();
            arg >> key >> value;
            arg.endMapEntry();
            map.insert(key, plain(value.variant()));
        }
        arg.endMap();
        return map;
    }
    return v;
}

} // namespace

bool DBusFcitx::running()
{
    const auto *bus = QDBusConnection::sessionBus().interface();
    return bus && bus->isServiceRegistered(kService).value();
}

bool DBusFcitx::addonEnabled()
{
    const QDBusMessage r = call(QStringLiteral("GetAddonsV2"));
    if (r.type() != QDBusMessage::ReplyMessage || r.arguments().isEmpty() ||
        r.arguments().constFirst().metaType() != QMetaType::fromType<QDBusArgument>())
        return false;
    const QDBusArgument arg = qvariant_cast<QDBusArgument>(r.arguments().constFirst());
    if (arg.currentType() != QDBusArgument::ArrayType) return false;
    bool found = false;
    arg.beginArray();
    while (!arg.atEnd()) {
        QString unique, name, comment;
        int category = 0;
        bool configurable = false, enabled = false, onDemand = false;
        QStringList deps, optDeps;
        arg.beginStructure();
        arg >> unique >> name >> comment >> category >> configurable >> enabled >> onDemand >> deps >> optDeps;
        arg.endStructure();
        if (unique == QLatin1String("rasklad")) found = enabled;
    }
    arg.endArray();
    return found;
}

QStringList DBusFcitx::groupMethods()
{
    const QDBusMessage g = call(QStringLiteral("CurrentInputMethodGroup"));
    if (g.type() != QDBusMessage::ReplyMessage || g.arguments().isEmpty()) return {};
    const QDBusMessage r = call(QStringLiteral("InputMethodGroupInfo"), {g.arguments().constFirst()});
    if (r.type() != QDBusMessage::ReplyMessage || r.arguments().size() < 2 ||
        r.arguments().at(1).metaType() != QMetaType::fromType<QDBusArgument>())
        return {};
    QStringList out;
    const QDBusArgument arg = qvariant_cast<QDBusArgument>(r.arguments().at(1));
    if (arg.currentType() != QDBusArgument::ArrayType) return {};
    arg.beginArray();
    while (!arg.atEnd()) {
        QString im, layout;
        arg.beginStructure();
        arg >> im >> layout;
        arg.endStructure();
        out.append(im);
    }
    arg.endArray();
    return out;
}

std::optional<QVariantMap> DBusFcitx::readConfig()
{
    const QDBusMessage r = call(QStringLiteral("GetConfig"), {kUri});
    if (r.type() != QDBusMessage::ReplyMessage || r.arguments().isEmpty()) return std::nullopt;
    const QVariant v = plain(r.arguments().constFirst());
    if (v.metaType() != QMetaType::fromType<QVariantMap>()) return std::nullopt;
    return v.toMap();
}

namespace {

// The addon's read-only status map; nullopt if Fcitx or the addon does not answer.
std::optional<QVariantMap> statusMap()
{
    const QDBusMessage r = call(QStringLiteral("GetConfig"), {QStringLiteral("fcitx://config/addon/rasklad/status")});
    if (r.type() != QDBusMessage::ReplyMessage || r.arguments().isEmpty()) return std::nullopt;
    const QVariant v = plain(r.arguments().constFirst());
    if (v.metaType() != QMetaType::fromType<QVariantMap>()) return std::nullopt;
    return v.toMap();
}

} // namespace

std::optional<bool> DBusFcitx::profileReady()
{
    const auto status = statusMap();
    if (!status) return std::nullopt;
    const QVariant ready = status->value(QStringLiteral("ProfileReady"));
    if (ready.metaType() != QMetaType::fromType<QString>()) return std::nullopt;
    if (ready.toString() == QLatin1String("True")) return true;
    if (ready.toString() == QLatin1String("False")) return false;
    return std::nullopt;
}

bool DBusFcitx::writeConfig(const QVariantMap &config)
{
    const QDBusMessage r = call(QStringLiteral("SetConfig"), {kUri, QVariant::fromValue(QDBusVariant(config))});
    return r.type() == QDBusMessage::ReplyMessage;
}

namespace {
const QString kSetupTool = QStringLiteral("/usr/bin/rasklad-setup");
constexpr qsizetype kMaxSetupOutput = 8192;
// More than the check's explicit per-call limits add up to (hyprctl and luac
// 5 s per call, D-Bus 2 s per call).
constexpr int kSetupTimeoutMs = 60000;
} // namespace

RaskladSetup::RaskladSetup() = default;

RaskladSetup::~RaskladSetup()
{
    if (m_process && m_process->state() != QProcess::NotRunning) {
        m_process->disconnect();
        m_process->kill();
        m_process->waitForFinished(200);
    }
}

void RaskladSetup::finish(std::optional<QByteArray> result)
{
    m_out.clear();
    if (m_process) m_process.release()->deleteLater(); // never deleted inside its own signal
    if (m_again) { // only an answer started after the last request is published
        m_again = false;
        requestCheck();
        return;
    }
    m_last = std::move(result);
    if (updated) updated();
}

void RaskladSetup::requestCheck()
{
    // Fixed program and argument, no shell, no input; asynchronous, so a stuck
    // tool never freezes the window: killed after 60 s or 8 KiB of output.
    if (m_process) {
        m_again = true;
        return;
    }
    if (!QFileInfo(kSetupTool).isExecutable()) {
        finish(std::nullopt);
        return;
    }
    m_process = std::make_unique<QProcess>();
    QProcess *p = m_process.get();
    p->setProgram(kSetupTool);
    p->setArguments({QStringLiteral("--check")});
    p->setStandardInputFile(QProcess::nullDevice());
    p->setStandardErrorFile(QProcess::nullDevice());
    QObject::connect(p, &QProcess::readyReadStandardOutput, p, [this, p] {
        m_out += p->readAllStandardOutput();
        if (m_out.size() > kMaxSetupOutput) p->kill();
    });
    QObject::connect(p, &QProcess::finished, p, [this, p](int, QProcess::ExitStatus status) {
        m_out += p->readAllStandardOutput();
        // Exit 1 only means "not all ok"; a crash, a kill or too much output means no answer.
        const bool good = status == QProcess::NormalExit && m_out.size() <= kMaxSetupOutput;
        finish(good ? std::optional<QByteArray>(m_out) : std::nullopt);
    });
    QObject::connect(p, &QProcess::errorOccurred, p, [this, p](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) finish(std::nullopt);
        else if (error == QProcess::Timedout) p->kill();
    });
    QTimer::singleShot(kSetupTimeoutMs, p, [p] { p->kill(); });
    p->start(QIODevice::ReadOnly);
}

bool RaskladSetup::installed() const
{
    // Only whether the record exists; its contents are rasklad-setup's business.
    QString state = qEnvironmentVariable("XDG_STATE_HOME");
    if (state.isEmpty() || !QDir::isAbsolutePath(state)) state = QDir::homePath() + QStringLiteral("/.local/state");
    return QFileInfo::exists(state + QStringLiteral("/rasklad-setup/state.json"));
}

bool RaskladSetup::open(const QString &action)
{
    if (action != QLatin1String("install") && action != QLatin1String("revert")) return false;
    // A double click opens one terminal, not two.
    const auto last = m_opened.constFind(action);
    if (last != m_opened.constEnd() && last->elapsed() < 2000) return true;
    // One of two fixed strings, the tool by absolute path (the launcher runs it with bash -c).
    const QString command = kSetupTool + QStringLiteral(" --") + action;
    // Omarchy's own way to run a setup step: a floating terminal with Done/Failed.
    const QString omarchy = QStringLiteral("/usr/bin/omarchy-launch-floating-terminal-with-presentation");
    bool started = false;
    if (QFileInfo(omarchy).isExecutable()) {
        started = QProcess::startDetached(omarchy, {command});
    } else {
        const QString terminal = QStringLiteral("/usr/bin/xdg-terminal-exec");
        if (QFileInfo(terminal).isExecutable())
            started = QProcess::startDetached(terminal, {kSetupTool, QStringLiteral("--") + action});
    }
    if (started) m_opened[action].start();
    return started;
}

namespace {
// One request to Hyprland's request socket (no subprocess); empty on any failure.
QByteArray hyprlandRequest(const QByteArray &request)
{
    const QByteArray runtime = qgetenv("XDG_RUNTIME_DIR");
    const QByteArray instance = qgetenv("HYPRLAND_INSTANCE_SIGNATURE");
    if (runtime.isEmpty() || instance.isEmpty() || instance.contains('/')) return {};
    const QByteArray path = runtime + "/hypr/" + instance + "/.socket.sock";
    sockaddr_un addr{};
    if (path.size() >= static_cast<qsizetype>(sizeof(addr.sun_path))) return {};
    addr.sun_family = AF_UNIX;
    std::memcpy(addr.sun_path, path.constData(), static_cast<size_t>(path.size()));
    // Non-blocking throughout: a wedged compositor never holds the window back.
    const int fd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
    if (fd < 0) return {};
    // One deadline for the whole exchange: a trickling reply cannot stretch it.
    const QDeadlineTimer deadline(kTimeoutMs);
    const auto wait = [fd, &deadline](short events) {
        pollfd p{fd, events, 0};
        int r;
        do {
            if (deadline.hasExpired()) return false; // never past the budget, even on EINTR
            r = ::poll(&p, 1, static_cast<int>(deadline.remainingTime()));
        } while (r < 0 && errno == EINTR);
        return r > 0 && (p.revents & events);
    };
    QByteArray reply;
    bool connected = ::connect(fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) == 0;
    if (!connected && (errno == EINPROGRESS || errno == EAGAIN) && wait(POLLOUT)) {
        int err = 0;
        socklen_t len = sizeof(err);
        connected = ::getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len) == 0 && err == 0;
    }
    if (connected && wait(POLLOUT) &&
        ::send(fd, request.constData(), static_cast<size_t>(request.size()), MSG_NOSIGNAL) == request.size()) {
        char buf[1024];
        while (reply.size() < 4096 && wait(POLLIN)) {
            const ssize_t n = ::recv(fd, buf, sizeof(buf), 0);
            if (n < 0 && errno == EINTR) continue;
            if (n <= 0) break;
            reply.append(buf, n);
        }
    }
    ::close(fd);
    return reply;
}
} // namespace

int hyprlandRounding()
{
    return roundingFromReply(hyprlandRequest("j/getoption decoration:rounding"));
}

bool hyprlandFocusPid(qint64 pid)
{
    // Only our own pid, a number. A Lua-configured Hyprland (0.56+) takes the Lua
    // form; older or hyprland.conf sessions take the classic one, as Omarchy does.
    if (pid <= 0) return false;
    const QByteArray n = QByteArray::number(pid);
    if (hyprlandRequest("dispatch hl.dsp.focus({ window = \"pid:" + n + "\" })").trimmed() == "ok") return true;
    return hyprlandRequest("dispatch focuswindow pid:" + n).trimmed() == "ok";
}

QStringList FcitxSeenApps::seenApps()
{
    const auto status = statusMap();
    return status ? seenAppsFromStatus(*status) : QStringList{};
}

} // namespace rasklad::settings
