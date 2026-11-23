// SPDX-License-Identifier: MIT
#include "sources.h"

#include "settingscore.h"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusInterface>
#include <QDBusMessage>
#include <QDBusVariant>

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

QStringList FcitxSeenApps::seenApps()
{
    const auto status = statusMap();
    return status ? seenAppsFromStatus(*status) : QStringList{};
}

} // namespace rasklad::settings
