// SPDX-License-Identifier: MIT
#include "sources.h"

#include <QDeadlineTimer>

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

int hyprlandRounding()
{
    const QByteArray runtime = qgetenv("XDG_RUNTIME_DIR");
    const QByteArray instance = qgetenv("HYPRLAND_INSTANCE_SIGNATURE");
    if (runtime.isEmpty() || instance.isEmpty() || instance.contains('/')) return 0;
    const QByteArray path = runtime + "/hypr/" + instance + "/.socket.sock";
    sockaddr_un addr{};
    if (path.size() >= static_cast<qsizetype>(sizeof(addr.sun_path))) return 0;
    addr.sun_family = AF_UNIX;
    std::memcpy(addr.sun_path, path.constData(), static_cast<size_t>(path.size()));
    // Non-blocking throughout: a wedged compositor never holds the window back.
    const int fd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
    if (fd < 0) return 0;
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
    const QByteArray request = "j/getoption decoration:rounding";
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
    return roundingFromReply(reply);
}

QStringList FcitxSeenApps::seenApps()
{
    const auto status = statusMap();
    return status ? seenAppsFromStatus(*status) : QStringList{};
}

} // namespace rasklad::settings
