// SPDX-License-Identifier: MIT
#include "settingscore.h"

#include "manualgesture.h"
#include "terminalapps.h"

#include <QByteArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>

#include <algorithm>

namespace rasklad::settings {

bool validAppId(const QString &id)
{
    const QByteArray latin = id.toLatin1(); // a non-Latin-1 character becomes '?', which is refused
    return id.size() == latin.size() &&
           rasklad::config::appIdentifier(std::string_view(latin.constData(), static_cast<std::size_t>(latin.size())));
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

// A list as the UTF-8 the addon reads, for the shared rules in src/configrules.h.
struct Utf8List {
    std::vector<QByteArray> bytes;
    std::vector<std::string_view> views;
};
Utf8List utf8Views(const QStringList &list)
{
    Utf8List out;
    out.bytes.reserve(static_cast<std::size_t>(list.size()));
    for (const QString &s : list) out.bytes.push_back(s.toUtf8());
    for (const QByteArray &b : out.bytes) out.views.emplace_back(b.constData(), static_cast<std::size_t>(b.size()));
    return out;
}

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
    // The addon's rule (src/configrules.h) picks the entries: the window shows what the addon uses.
    for (const std::size_t i : rasklad::config::keptApplications(utf8Views(*apps).views))
        out.apps.append(apps->at(static_cast<qsizetype>(i)));
    out.sound = map.value(QStringLiteral("Sound"), QStringLiteral("True")).toString() != QLatin1String("False");
    out.wordOptions = map.contains(QStringLiteral("AutoCorrect"));
    if (out.wordOptions) {
        out.autoCorrect = map.value(QStringLiteral("AutoCorrect")).toString() != QLatin1String("False");
        const auto words = map.contains(QStringLiteral("Exceptions"))
                               ? listFromValue(map.value(QStringLiteral("Exceptions")))
                               : std::optional<QStringList>(QStringList{});
        if (!words) return std::nullopt;
        for (const std::size_t i : rasklad::config::keptExceptions(utf8Views(*words).views))
            out.exceptions.append(words->at(static_cast<qsizetype>(i)).toLower()); // stored lowercase, as normalizeException
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

QVariantMap setupFromCheck(const QByteArray &output)
{
    static const QStringList steps{QStringLiteral("package"),         QStringLiteral("fcitx-addon"),
                                   QStringLiteral("auto-correction"), QStringLiteral("fcitx-group"),
                                   QStringLiteral("double-alt"),      QStringLiteral("hyprland-layout"),
                                   QStringLiteral("bar-widget-link"), QStringLiteral("bar-widget")};
    // Exactly the words `rasklad-setup --check` prints (SYS-252).
    static const QStringList states{QStringLiteral("ok"),       QStringLiteral("missing"),     QStringLiteral("broken"),
                                    QStringLiteral("other"),    QStringLiteral("symlink"),     QStringLiteral("unknown"),
                                    QStringLiteral("no-fcitx"), QStringLiteral("missing-file"), QStringLiteral("lua-error"),
                                    QStringLiteral("no-slot"),  QStringLiteral("not-a-file"),  QStringLiteral("unparsable"),
                                    QStringLiteral("on"),       QStringLiteral("paused"),
                                    QStringLiteral("other-layouts"), QStringLiteral("optional"),
                                    QStringLiteral("legacy")};
    QVariantMap result;
    if (output.size() > 4096) return result;
    for (const QByteArray &raw : output.split('\n')) {
        const QList<QByteArray> words = raw.simplified().split(' ');
        if (words.size() < 2) continue;
        const QString state = QString::fromLatin1(words.at(0));
        const QString step = QString::fromLatin1(words.at(1));
        if (steps.contains(step) && states.contains(state)) result.insert(step, state);
    }
    return result;
}

QString setupRowState(const QVariantMap &setup, const QStringList &steps)
{
    bool problem = false, missing = false, unknown = false, other = false;
    for (const QString &step : steps) {
        const QString s = setup.value(step).toString();
        // SYS-268: "optional" is the user's choice to keep Hyprland's layouts.
        if (s == QLatin1String("ok") || s == QLatin1String("optional")) continue;
        if (s == QLatin1String("lua-error") || s == QLatin1String("no-slot") || s == QLatin1String("broken") ||
            s == QLatin1String("not-a-file") || s == QLatin1String("unparsable") ||
            s == QLatin1String("legacy")) // a pre-SYS-268 block: revert, then install
            problem = true;
        else if (s == QLatin1String("missing") || s == QLatin1String("missing-file"))
            missing = true;
        else if (s == QLatin1String("other") || s == QLatin1String("symlink") ||
                 s == QLatin1String("other-layouts")) // SYS-268: kz, de, ... left as is
            other = true;
        else
            unknown = true; // "unknown", "no-fcitx", or absent
    }
    if (problem) return QStringLiteral("problem");
    if (missing) return QStringLiteral("missing");
    if (unknown) return QStringLiteral("unknown");
    if (other) return QStringLiteral("other");
    return QStringLiteral("ok");
}

int roundingFromReply(const QByteArray &json)
{
    const QJsonDocument doc = QJsonDocument::fromJson(json.left(4096));
    if (!doc.isObject()) return 0;
    const QJsonValue v = doc.object().value(QStringLiteral("int"));
    if (!v.isDouble()) return 0;
    return std::clamp(v.toInt(), 0, 24);
}

bool layoutsReady(const QStringList &groupMethods)
{
    return groupMethods.contains(QStringLiteral("keyboard-us")) &&
           groupMethods.contains(QStringLiteral("keyboard-ru"));
}

} // namespace rasklad::settings
