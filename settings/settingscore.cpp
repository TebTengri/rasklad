// SPDX-License-Identifier: MIT
#include "settingscore.h"

#include "terminalapps.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <algorithm>

namespace rasklad::settings {

bool validAppId(const QString &id)
{
    if (id.isEmpty() || id.size() > 128) return false;
    for (const QChar c : id) {
        const ushort u = c.unicode();
        if (!((u >= 'a' && u <= 'z') || (u >= 'A' && u <= 'Z') || (u >= '0' && u <= '9') || u == '.' ||
              u == '_' || u == '-'))
            return false;
    }
    return true;
}

bool isTerminal(const QString &id)
{
    const QByteArray latin = id.toLatin1();
    return rasklad::terminalApp(std::string_view(latin.constData(), static_cast<std::size_t>(latin.size())));
}

bool isTested(const QString &id)
{
    return id == QLatin1String("slack") || id == QLatin1String("chromium") ||
           id == QLatin1String("chromium-browser");
}

AddonConfig configFromMap(const QVariantMap &map)
{
    AddonConfig out;
    const QVariantMap apps = map.value(QStringLiteral("Applications")).toMap();
    // Fcitx lists are keyed "0", "1", …: keep their numeric order.
    QList<int> keys;
    for (auto it = apps.cbegin(); it != apps.cend(); ++it) {
        bool ok = false;
        const int k = it.key().toInt(&ok);
        if (ok && k >= 0) keys.append(k);
    }
    std::sort(keys.begin(), keys.end());
    for (const int k : keys) {
        const QString id = apps.value(QString::number(k)).toString();
        if (out.apps.size() == kMaxApps) break;
        if (validAppId(id) && !isTerminal(id) && !out.apps.contains(id)) out.apps.append(id);
    }
    out.sound = map.value(QStringLiteral("Sound"), QStringLiteral("True")).toString() != QLatin1String("False");
    return out;
}

QVariantMap mapFromConfig(const AddonConfig &config)
{
    QVariantMap apps;
    int i = 0;
    for (const QString &id : config.apps) {
        if (i == kMaxApps) break;
        if (!validAppId(id) || isTerminal(id)) continue;
        apps.insert(QString::number(i++), id);
    }
    return {{QStringLiteral("Applications"), apps},
            {QStringLiteral("Sound"), config.sound ? QStringLiteral("True") : QStringLiteral("False")}};
}

QStringList classesFromClients(const QByteArray &json)
{
    QStringList out;
    const QJsonDocument doc = QJsonDocument::fromJson(json);
    if (!doc.isArray()) return out;
    for (const QJsonValue v : doc.array()) {
        const QString cls = v.toObject().value(QStringLiteral("class")).toString();
        if (validAppId(cls) && !out.contains(cls)) out.append(cls);
    }
    std::sort(out.begin(), out.end());
    return out;
}

bool layoutsReady(const QStringList &groupMethods)
{
    return groupMethods.contains(QStringLiteral("keyboard-us")) &&
           groupMethods.contains(QStringLiteral("keyboard-ru"));
}

} // namespace rasklad::settings
