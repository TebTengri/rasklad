// SPDX-License-Identifier: MIT
#include "desktopindex.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QRegularExpression>
#include <QSet>
#include <QStandardPaths>

#include <algorithm>

namespace rasklad::settings {

namespace {

// Per the Desktop Entry spec: \s \n \t \r \\; any other escape stays as written.
QString unescape(const QString &value)
{
    QString out;
    out.reserve(value.size());
    for (qsizetype i = 0; i < value.size(); ++i) {
        const QChar c = value.at(i);
        if (c != QLatin1Char('\\') || i + 1 >= value.size()) {
            out += c;
            continue;
        }
        const QChar n = value.at(i + 1);
        switch (n.unicode()) {
        case 's': out += QLatin1Char(' '); break;
        case 'n': out += QLatin1Char('\n'); break;
        case 't': out += QLatin1Char('\t'); break;
        case 'r': out += QLatin1Char('\r'); break;
        case '\\': out += QLatin1Char('\\'); break;
        default: out += c; out += n; break;
        }
        ++i;
    }
    return out;
}

// The program basename of an Exec line, lowercase; nothing else is kept.
// A leading `env` and its VAR=value tokens are skipped, quotes stripped and
// field codes (%f, %U, ...) ignored.
QString execBasename(const QString &exec)
{
    QStringList tokens;
    QString cur;
    bool quoted = false, any = false;
    for (qsizetype i = 0; i < exec.size(); ++i) {
        const QChar c = exec.at(i);
        if (c == QLatin1Char('"')) {
            quoted = !quoted;
            any = true;
        } else if (quoted && c == QLatin1Char('\\') && i + 1 < exec.size()) {
            cur += exec.at(++i);
        } else if (!quoted && c.isSpace()) {
            if (any || !cur.isEmpty()) tokens << cur;
            cur.clear();
            any = false;
        } else {
            cur += c;
        }
    }
    if (any || !cur.isEmpty()) tokens << cur;

    auto base = [](const QString &t) { return t.mid(t.lastIndexOf(QLatin1Char('/')) + 1); };
    qsizetype i = 0;
    while (i < tokens.size() && tokens.at(i).startsWith(QLatin1Char('%'))) ++i;
    if (i < tokens.size() && base(tokens.at(i)) == QLatin1String("env")) {
        ++i;
        while (i < tokens.size() &&
               (tokens.at(i).contains(QLatin1Char('=')) || tokens.at(i).startsWith(QLatin1Char('%'))))
            ++i;
    }
    if (i >= tokens.size()) return {};
    return base(tokens.at(i)).toLower();
}

bool isTrue(const QString &v)
{
    return v.trimmed().compare(QLatin1String("true"), Qt::CaseInsensitive) == 0;
}

// Raw values of the keys we read, from the [Desktop Entry] group only.
struct RawEntry {
    QHash<QString, QString> keys; // full key, e.g. "Name[ru]"
};

bool wantedKey(const QString &key)
{
    const qsizetype bracket = key.indexOf(QLatin1Char('['));
    const QString base = bracket < 0 ? key : key.left(bracket);
    if (bracket >= 0)
        return base == QLatin1String("Name") || base == QLatin1String("GenericName") ||
               base == QLatin1String("Comment");
    static const QSet<QString> plain{QStringLiteral("Type"),     QStringLiteral("Name"),
                                     QStringLiteral("GenericName"), QStringLiteral("Comment"),
                                     QStringLiteral("Icon"),     QStringLiteral("StartupWMClass"),
                                     QStringLiteral("Exec"),     QStringLiteral("NoDisplay"),
                                     QStringLiteral("Hidden")};
    return plain.contains(base);
}

RawEntry parse(const QByteArray &data)
{
    RawEntry raw;
    bool inEntry = false;
    const QString text = QString::fromUtf8(data);
    const QStringList lines = text.split(QLatin1Char('\n'));
    for (const QString &rawLine : lines) {
        const QString line = rawLine.trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#'))) continue;
        if (line.startsWith(QLatin1Char('['))) {
            if (inEntry) break; // the next group ends [Desktop Entry]
            inEntry = line == QLatin1String("[Desktop Entry]");
            continue;
        }
        if (!inEntry) continue;
        const qsizetype eq = line.indexOf(QLatin1Char('='));
        if (eq <= 0) continue;
        const QString key = line.left(eq).trimmed();
        if (!wantedKey(key) || raw.keys.contains(key)) continue; // first occurrence wins
        raw.keys.insert(key, unescape(line.mid(eq + 1).trimmed()));
    }
    return raw;
}

QString localized(const RawEntry &raw, const QString &key, const QStringList &locales)
{
    for (const QString &loc : locales) {
        const QString v = raw.keys.value(key + QLatin1Char('[') + loc + QLatin1Char(']'));
        if (!v.isEmpty()) return v;
    }
    return {};
}

const QRegularExpression &webAppPattern()
{
    static const QRegularExpression re(QStringLiteral("^chrome-(.+?)__.*$"),
                                       QRegularExpression::CaseInsensitiveOption);
    return re;
}

// Trailing reverse-DNS segments too generic to name an app ("org.telegram.desktop").
bool genericSegment(const QString &s)
{
    return s == QLatin1String("desktop") || s == QLatin1String("app") || s == QLatin1String("client") ||
           s == QLatin1String("application");
}

} // namespace

QStringList DesktopIndex::defaultDirs()
{
    return QStandardPaths::standardLocations(QStandardPaths::ApplicationsLocation);
}

void DesktopIndex::load(const QStringList &dirs, const QString &rawLanguage)
{
    m_entries.clear();

    // "ru" -> {"ru_RU", "ru"}; "ru_RU", "ru-RU", "ru_RU.UTF-8@x" -> {"ru_RU", "ru"}.
    QString language = rawLanguage;
    language.replace(QLatin1Char('-'), QLatin1Char('_'));
    for (const QChar cut : {QLatin1Char('.'), QLatin1Char('@')}) {
        const qsizetype at = language.indexOf(cut);
        if (at >= 0) language.truncate(at);
    }
    QStringList locales;
    const qsizetype us = language.indexOf(QLatin1Char('_'));
    const QString lang = us < 0 ? language : language.left(us);
    if (!lang.isEmpty()) {
        locales << (us < 0 ? lang + QLatin1Char('_') + lang.toUpper() : language) << lang;
    }

    QSet<QString> seenIds;
    int files = 0;
    for (const QString &dirPath : dirs) {
        if (files >= kMaxFiles) break;
        const QDir dir(dirPath);
        if (dirPath.isEmpty() || !dir.exists()) continue;
        // Symlinked directories are not followed, so there are no loops.
        QDirIterator it(dirPath, {QStringLiteral("*.desktop")}, QDir::Files | QDir::NoDotAndDotDot,
                        QDirIterator::Subdirectories);
        QStringList paths;
        while (it.hasNext() && files + paths.size() < kMaxFiles) paths << it.next();
        files += static_cast<int>(paths.size());
        std::sort(paths.begin(), paths.end());

        for (const QString &path : std::as_const(paths)) {
            QString rel = dir.relativeFilePath(path);
            rel.replace(QLatin1Char('/'), QLatin1Char('-'));
            const QString id = rel.left(rel.size() - 8); // without ".desktop"
            const QString idKey = id.toLower();
            if (seenIds.contains(idKey)) continue;

            const QFileInfo fi(path);
            if (fi.size() > kMaxFileSize) continue;
            QFile f(path);
            if (!f.open(QIODevice::ReadOnly)) continue;
            const QByteArray data = f.read(kMaxFileSize + 1);
            if (data.size() > kMaxFileSize) continue;
            seenIds.insert(idKey); // this file owns the id, even if it is skipped below

            const RawEntry raw = parse(data);
            if (raw.keys.value(QStringLiteral("Type")) != QLatin1String("Application")) continue;
            if (isTrue(raw.keys.value(QStringLiteral("Hidden")))) continue;

            Entry e;
            e.id = idKey;
            e.wmClass = raw.keys.value(QStringLiteral("StartupWMClass")).toLower();
            e.execBase = execBasename(raw.keys.value(QStringLiteral("Exec")));
            e.noDisplay = isTrue(raw.keys.value(QStringLiteral("NoDisplay")));
            e.info.name = localized(raw, QStringLiteral("Name"), locales);
            if (e.info.name.isEmpty()) e.info.name = raw.keys.value(QStringLiteral("Name"));
            if (e.info.name.isEmpty()) continue;
            QString desc = localized(raw, QStringLiteral("Comment"), locales);
            if (desc.isEmpty()) desc = localized(raw, QStringLiteral("GenericName"), locales);
            if (desc.isEmpty()) desc = raw.keys.value(QStringLiteral("Comment"));
            if (desc.isEmpty()) desc = raw.keys.value(QStringLiteral("GenericName"));
            if (desc.compare(e.info.name, Qt::CaseInsensitive) == 0) desc.clear();
            e.info.description = desc;
            e.info.icon = raw.keys.value(QStringLiteral("Icon"));
            e.info.webApp = webAppPattern().match(id).hasMatch();
            m_entries.push_back(std::move(e));
        }
    }
}

std::optional<AppInfo> DesktopIndex::find(const QString &program) const
{
    const QString p = program.toLower();
    if (p.isEmpty()) return std::nullopt;

    // The first entry the rule accepts, preferring one without NoDisplay.
    auto pick = [this](auto &&matches) -> const Entry * {
        const Entry *hidden = nullptr;
        for (const Entry &e : m_entries) {
            if (!matches(e)) continue;
            if (!e.noDisplay) return &e;
            if (!hidden) hidden = &e;
        }
        return hidden;
    };

    // Launchers sharing a window class: the one whose id is the program wins.
    const Entry *hit = pick([&](const Entry &e) { return !e.wmClass.isEmpty() && e.wmClass == p && e.id == p; });
    if (!hit) hit = pick([&](const Entry &e) { return !e.wmClass.isEmpty() && e.wmClass == p; });
    if (!hit) hit = pick([&](const Entry &e) { return e.id == p; });
    if (!hit) {
        // A reverse-DNS segment names an app only when exactly one entry has it:
        // com.a.editor and org.b.editor both answer "editor", so neither does.
        auto segMatch = [&](const Entry &e) {
            const QStringList seg = e.id.split(QLatin1Char('.'));
            if (seg.size() < 2) return false;
            if (seg.last() == p) return !genericSegment(p);
            return seg.size() >= 3 && genericSegment(seg.last()) && seg.at(seg.size() - 2) == p;
        };
        int count = 0;
        for (const Entry &e : m_entries)
            if (segMatch(e)) ++count;
        if (count == 1) hit = pick(segMatch);
    }
    if (!hit) {
        // Web app entries run the browser itself, so they never claim its basename.
        auto execMatch = [&](const Entry &e) { return !e.info.webApp && e.execBase == p; };
        int count = 0;
        for (const Entry &e : m_entries)
            if (execMatch(e)) ++count;
        if (count == 1) hit = pick(execMatch);
    }

    const QRegularExpressionMatch web = webAppPattern().match(program);
    if (hit) {
        AppInfo info = hit->info;
        if (web.hasMatch()) info.webApp = true;
        return info;
    }
    if (!web.hasMatch()) return std::nullopt;
    QString host = web.captured(1);
    if (host.startsWith(QLatin1String("www."), Qt::CaseInsensitive)) host = host.mid(4);
    if (host.isEmpty()) return std::nullopt;
    AppInfo info;
    info.name = host;
    info.webApp = true;
    return info;
}

int DesktopIndex::size() const
{
    return static_cast<int>(m_entries.size());
}

bool isSystemComponent(const QString &id)
{
    static const QStringList components{
        QStringLiteral("quickshell"), QStringLiteral("org.quickshell"), QStringLiteral("walker"),
        QStringLiteral("elephant"),   QStringLiteral("mako"),           QStringLiteral("waybar"),
        QStringLiteral("swayosd"),    QStringLiteral("hyprlauncher"),   QStringLiteral("rasklad-settings"),
        QStringLiteral("org.omarchy.screensaver")};
    return components.contains(id, Qt::CaseInsensitive);
}

QString canonicalApp(const QString &id)
{
    if (id.compare(QLatin1String("chromium-browser"), Qt::CaseInsensitive) == 0)
        return QStringLiteral("chromium");
    return id;
}

QStringList appAliases(const QString &canonical)
{
    if (canonical.compare(QLatin1String("chromium"), Qt::CaseInsensitive) == 0)
        return {QStringLiteral("chromium"), QStringLiteral("chromium-browser")};
    return {canonical};
}

} // namespace rasklad::settings
