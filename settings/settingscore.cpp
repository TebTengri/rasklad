// SPDX-License-Identifier: MIT
#include "settingscore.h"

#include "manualgesture.h"
#include "terminalapps.h"

#include <QByteArray>

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

QString normalizeException(const QString &word)
{
    // Checked as written, by the addon's own rule, so the window never shows a word
    // the addon ignores (" rfr " or a lookalike letter that lowercases to Latin).
    // Every accepted letter is A-Z, a-z, А-Я, а-я, Ё or ё, which Qt lowercases
    // exactly as the addon does.
    if (exceptionKey(word).isEmpty()) return {};
    return word.toLower();
}

QString exceptionKey(const QString &word)
{
    const QByteArray utf8 = word.toUtf8();
    return QString::fromStdString(
        rasklad::manual::exceptionKeys(std::string_view(utf8.constData(), static_cast<std::size_t>(utf8.size()))));
}

namespace {

// A Fcitx list option: a map keyed "0", "1", … (kept in numeric order), or "" when
// empty. nullopt for any other shape.
std::optional<QStringList> listFromValue(const QVariant &raw)
{
    if (raw.metaType() == QMetaType::fromType<QString>() && raw.toString().isEmpty()) return QStringList{};
    if (raw.metaType() != QMetaType::fromType<QVariantMap>()) return std::nullopt;
    const QVariantMap items = raw.toMap();
    QList<int> keys;
    for (auto it = items.cbegin(); it != items.cend(); ++it) {
        bool ok = false;
        const int k = it.key().toInt(&ok);
        if (ok && k >= 0) keys.append(k);
    }
    std::sort(keys.begin(), keys.end());
    QStringList out;
    for (const int k : keys) out.append(items.value(QString::number(k)).toString());
    return out;
}

QVariantMap valueFromList(const QStringList &items)
{
    QVariantMap out;
    for (int i = 0; i < items.size(); ++i) out.insert(QString::number(i), items[i]);
    return out;
}

} // namespace

std::optional<AddonConfig> configFromMap(const QVariantMap &map)
{
    AddonConfig out;
    const auto apps = listFromValue(map.value(QStringLiteral("Applications")));
    if (!apps) return std::nullopt;
    for (const QString &id : *apps) {
        if (out.apps.size() == kMaxApps) break;
        if (validAppId(id) && !isTerminal(id) && !out.apps.contains(id)) out.apps.append(id);
    }
    out.sound = map.value(QStringLiteral("Sound"), QStringLiteral("True")).toString() != QLatin1String("False");
    out.wordOptions = map.contains(QStringLiteral("AutoCorrect"));
    if (out.wordOptions) {
        out.autoCorrect = map.value(QStringLiteral("AutoCorrect")).toString() != QLatin1String("False");
        const auto words = map.contains(QStringLiteral("Exceptions"))
                               ? listFromValue(map.value(QStringLiteral("Exceptions")))
                               : std::optional<QStringList>(QStringList{});
        if (!words) return std::nullopt;
        QStringList seen;
        for (const QString &raw : *words) {
            if (out.exceptions.size() == kMaxExceptions) break;
            const QString word = normalizeException(raw);
            const QString key = exceptionKey(word);
            if (word.isEmpty() || seen.contains(key)) continue;
            seen.append(key);
            out.exceptions.append(word);
        }
    }
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
    QVariantMap out{{QStringLiteral("Applications"), apps},
                    {QStringLiteral("Sound"), config.sound ? QStringLiteral("True") : QStringLiteral("False")}};
    if (config.wordOptions) {
        QStringList words;
        for (const QString &raw : config.exceptions) {
            const QString word = normalizeException(raw);
            if (!word.isEmpty() && words.size() < kMaxExceptions) words.append(word);
        }
        out.insert(QStringLiteral("AutoCorrect"), config.autoCorrect ? QStringLiteral("True") : QStringLiteral("False"));
        out.insert(QStringLiteral("Exceptions"), valueFromList(words));
    }
    return out;
}

QStringList seenAppsFromStatus(const QVariantMap &status)
{
    QStringList out;
    const auto names = listFromValue(status.value(QStringLiteral("SeenApplications")));
    if (!names) return out;
    for (const QString &id : *names) {
        if (out.size() == kMaxApps) break;
        if (!validAppId(id) || id == QLatin1String("rasklad-settings") || out.contains(id)) continue;
        out.append(id);
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
