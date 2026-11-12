#include "themewatcher.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QLocale>
#include <QRegularExpression>
#include <QStringDecoder>

#include <initializer_list>
#include <utility>

namespace rasklad::app {

namespace {

bool readSmallFile(const QString &path, QByteArray &out)
{
    out.clear();
    const QFileInfo info(path);
    if (path.isEmpty() || !info.exists() || !info.isFile() || info.size() > kMaxThemeFileBytes)
        return false;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return false;
    QByteArray bytes = file.read(kMaxThemeFileBytes + 1);
    if (file.error() != QFileDevice::NoError || qint64(bytes.size()) > kMaxThemeFileBytes)
        return false;
    out = std::move(bytes);
    return true;
}

bool isKeyChar(QChar ch)
{
    const char16_t c = ch.unicode();
    return (c >= u'a' && c <= u'z') || (c >= u'A' && c <= u'Z') || (c >= u'0' && c <= u'9') || c == u'_'
        || c == u'-' || c == u'.';
}

bool isValidKey(const QString &key)
{
    if (key.isEmpty() || key.size() > 64)
        return false;
    for (const QChar ch : key) {
        if (!isKeyChar(ch))
            return false;
    }
    return true;
}

// "#rrggbb", "#rrggbbaa" (alpha ignored) or "0xrrggbb".
std::optional<QColor> parseColorValue(QString value)
{
    if (value.startsWith(QLatin1String("0x")) || value.startsWith(QLatin1String("0X")))
        value = QLatin1Char('#') + value.mid(2);
    if (!value.startsWith(QLatin1Char('#')) || (value.size() != 7 && value.size() != 9))
        return std::nullopt;
    for (qsizetype i = 1; i < value.size(); ++i) {
        const char16_t c = value.at(i).unicode();
        if (!((c >= u'0' && c <= u'9') || (c >= u'a' && c <= u'f') || (c >= u'A' && c <= u'F')))
            return std::nullopt;
    }
    const QColor color = QColor::fromString(value.left(7));
    if (!color.isValid())
        return std::nullopt;
    return color;
}

QColor firstOf(const QHash<QString, QColor> &colors, std::initializer_list<const char *> keys)
{
    for (const char *k : keys) {
        const auto it = colors.constFind(QString::fromLatin1(k));
        if (it != colors.constEnd())
            return it.value();
    }
    return {};
}

QColor blend(const QColor &a, const QColor &b, double t)
{
    return QColor::fromRgbF(float(a.redF() * (1.0 - t) + b.redF() * t), float(a.greenF() * (1.0 - t) + b.greenF() * t),
                            float(a.blueF() * (1.0 - t) + b.blueF() * t));
}

void finishPalette(ThemePalette &p)
{
    p.dark = p.background.lightnessF() < 0.5;
    p.surface = p.dark ? p.background.lighter(125) : p.background.darker(106);
    if (!p.muted.isValid())
        p.muted = blend(p.foreground, p.background, 0.45);
    if (!p.accent.isValid())
        p.accent = QColor(0xff, 0xd6, 0x0a);
    if (!p.error.isValid())
        p.error = p.dark ? QColor(0xf7, 0x76, 0x8e) : QColor(0xb0, 0x2a, 0x37);
}

QString configHome()
{
    const QString xdg = qEnvironmentVariable("XDG_CONFIG_HOME");
    if (!xdg.isEmpty() && QDir::isAbsolutePath(xdg))
        return QDir::cleanPath(xdg);
    return QDir::homePath() + QStringLiteral("/.config");
}

} // namespace

ThemePalette defaultPalette()
{
    ThemePalette p;
    p.background = QColor(0x1a, 0x1b, 0x26);
    p.foreground = QColor(0xc0, 0xca, 0xf5);
    p.accent = QColor(0xff, 0xd6, 0x0a);
    p.error = QColor(0xf7, 0x76, 0x8e);
    p.muted = QColor(0x73, 0x7a, 0xa2);
    finishPalette(p);
    return p;
}

std::optional<ThemePalette> parseThemeColors(const QByteArray &bytes)
{
    if (bytes.isEmpty() || qint64(bytes.size()) > kMaxThemeFileBytes)
        return std::nullopt;
    QStringDecoder decoder(QStringDecoder::Utf8, QStringDecoder::Flag::Stateless);
    const QString text = decoder(bytes);
    if (decoder.hasError())
        return std::nullopt;
    QHash<QString, QColor> colors;
    QString section;
    const QStringList lines = text.split(QLatin1Char('\n'));
    for (const QString &rawLine : lines) {
        const QString line = rawLine.trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#')))
            continue;
        if (line.startsWith(QLatin1Char('['))) {
            const qsizetype close = line.indexOf(QLatin1Char(']'));
            if (close <= 1)
                continue;
            const QString name = line.mid(1, close - 1).trimmed();
            section = isValidKey(name) ? name : QString();
            continue;
        }
        const qsizetype eq = line.indexOf(QLatin1Char('='));
        if (eq <= 0)
            continue;
        const QString keyName = line.left(eq).trimmed();
        QString value = line.mid(eq + 1).trimmed();
        if (!isValidKey(keyName) || value.isEmpty())
            continue;
        const QChar quote = value.at(0);
        if (quote == QLatin1Char('"') || quote == QLatin1Char('\'')) {
            const qsizetype end = value.indexOf(quote, 1);
            if (end < 0)
                continue;
            value = value.mid(1, end - 1);
        } else {
            const qsizetype hash = value.indexOf(QLatin1String(" #"));
            if (hash >= 0)
                value = value.left(hash).trimmed();
        }
        const std::optional<QColor> color = parseColorValue(value);
        if (!color)
            continue;
        const QString fullKey = section.isEmpty() ? keyName : section + QLatin1Char('.') + keyName;
        colors.insert(fullKey, *color);
        if (colors.size() > 512)
            break;
    }
    ThemePalette p;
    p.background = firstOf(colors, {"background", "colors.primary.background"});
    p.foreground = firstOf(colors, {"foreground", "colors.primary.foreground"});
    p.accent = firstOf(colors, {"accent", "colors.normal.blue", "color4"});
    if (!p.background.isValid() || !p.foreground.isValid()) {
        if (!p.accent.isValid())
            return std::nullopt;
        const auto fallback = defaultPalette();
        p.background = fallback.background;
        p.foreground = fallback.foreground;
    }
    p.error = firstOf(colors, {"colors.normal.red", "color1"});
    p.muted = firstOf(colors, {"colors.bright.black", "color8"});
    finishPalette(p);
    return p;
}

QString languageFromLocaleName(const QString &name)
{
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty())
        return {};
    if (trimmed.startsWith(QLatin1String("ru")))
        return QStringLiteral("ru");
    return QStringLiteral("en");
}

QString languageFromLocaleConf(const QByteArray &bytes)
{
    if (qint64(bytes.size()) > kMaxThemeFileBytes)
        return {};
    QString lcAll;
    QString lcMessages;
    QString lang;
    const QString text = QString::fromUtf8(bytes);
    for (const QString &rawLine : text.split(QLatin1Char('\n'))) {
        const QString line = rawLine.trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#')))
            continue;
        const qsizetype eq = line.indexOf(QLatin1Char('='));
        if (eq <= 0)
            continue;
        const QString name = line.left(eq).trimmed();
        QString value = line.mid(eq + 1).trimmed();
        if (value.size() >= 2 && (value.startsWith(QLatin1Char('"')) || value.startsWith(QLatin1Char('\'')))
            && value.endsWith(value.at(0)))
            value = value.mid(1, value.size() - 2);
        if (name == QLatin1String("LC_ALL"))
            lcAll = value;
        else if (name == QLatin1String("LC_MESSAGES"))
            lcMessages = value;
        else if (name == QLatin1String("LANG"))
            lang = value;
    }
    for (const QString &candidate : {lcAll, lcMessages, lang}) {
        const QString language = languageFromLocaleName(candidate);
        if (!language.isEmpty())
            return language;
    }
    return {};
}

ThemeSources defaultThemeSources()
{
    const QString config = configHome();
    QString state = qEnvironmentVariable("XDG_STATE_HOME");
    if (state.isEmpty() || !QDir::isAbsolutePath(state))
        state = QDir::homePath() + QStringLiteral("/.local/state");
    ThemeSources s;
    s.themeParent = QDir::cleanPath(state) + QStringLiteral("/omarchy/current");
    s.themeDirectory = s.themeParent + QStringLiteral("/theme");
    s.fallbackThemeDirectory = config + QStringLiteral("/omarchy/current/theme");
    s.languageFile = config + QStringLiteral("/omarchy/language");
    s.localeFiles = {config + QStringLiteral("/locale.conf"), QStringLiteral("/etc/locale.conf")};
    return s;
}

ThemeWatcher::ThemeWatcher(ThemeSources sources, const QString &forcedLanguage, bool watch, QObject *parent)
    : QObject(parent)
    , m_sources(std::move(sources))
    , m_forcedLanguage(forcedLanguage)
    , m_palette(defaultPalette())
{
    m_debounce.setSingleShot(true);
    m_debounce.setInterval(200);
    connect(&m_debounce, &QTimer::timeout, this, [this]() {
        reload();
        rearmWatcher();
    });
    if (watch) {
        m_watcher = new QFileSystemWatcher(this);
        connect(m_watcher, &QFileSystemWatcher::fileChanged, this, [this]() { m_debounce.start(); });
        connect(m_watcher, &QFileSystemWatcher::directoryChanged, this, [this]() { m_debounce.start(); });
    }
    reload();
    rearmWatcher();
}

void ThemeWatcher::reload()
{
    // Palette.
    ThemePalette palette = defaultPalette();
    bool fromDesktop = false;
    for (const QString &directory : {m_sources.themeDirectory, m_sources.fallbackThemeDirectory}) {
        if (directory.isEmpty())
            continue;
        for (const char *name : {"colors.toml", "alacritty.toml"}) {
            QByteArray bytes;
            if (!readSmallFile(directory + QLatin1Char('/') + QString::fromLatin1(name), bytes))
                continue;
            if (const std::optional<ThemePalette> parsed = parseThemeColors(bytes)) {
                palette = *parsed;
                fromDesktop = true;
                break;
            }
        }
        if (fromDesktop)
            break;
    }
    const bool paletteDiffers = palette.background != m_palette.background || palette.surface != m_palette.surface
        || palette.foreground != m_palette.foreground || palette.muted != m_palette.muted
        || palette.accent != m_palette.accent || palette.error != m_palette.error || palette.dark != m_palette.dark
        || fromDesktop != m_fromDesktop;
    m_palette = palette;
    m_fromDesktop = fromDesktop;

    // Language.
    QString language = m_forcedLanguage;
    if (language.isEmpty()) {
        QByteArray bytes;
        if (readSmallFile(m_sources.languageFile, bytes)) {
            QStringDecoder decoder(QStringDecoder::Utf8, QStringDecoder::Flag::Stateless);
            const QString decoded = decoder(bytes);
            const QString code = decoded.trimmed();
            static const QRegularExpression registry(QStringLiteral("^[a-z]{2}(?:-[A-Za-z]{2})?$"));
            if (!decoder.hasError() && code.size() <= 5 && registry.match(code).hasMatch())
                language = code == QLatin1String("ru") || code.startsWith(QLatin1String("ru-"))
                    ? QStringLiteral("ru") : QStringLiteral("en");
        }
    }
    if (language.isEmpty()) {
        for (const QString &file : m_sources.localeFiles) {
            QByteArray bytes;
            if (!readSmallFile(file, bytes))
                continue;
            language = languageFromLocaleConf(bytes);
            if (!language.isEmpty())
                break;
        }
    }
    if (language.isEmpty()) {
        for (const char *variable : {"LC_ALL", "LC_MESSAGES", "LANG"}) {
            language = languageFromLocaleName(qEnvironmentVariable(variable));
            if (!language.isEmpty())
                break;
        }
    }
    if (language.isEmpty())
        language = languageFromLocaleName(QLocale::system().name());
    if (language.isEmpty())
        language = QStringLiteral("en");
    const bool languageDiffers = language != m_language;
    m_language = language;

    if (paletteDiffers)
        Q_EMIT paletteChanged();
    if (languageDiffers)
        Q_EMIT languageChanged();
}

void ThemeWatcher::rearmWatcher()
{
    if (!m_watcher)
        return;
    const QStringList files = m_watcher->files();
    if (!files.isEmpty())
        m_watcher->removePaths(files);
    const QStringList dirs = m_watcher->directories();
    if (!dirs.isEmpty())
        m_watcher->removePaths(dirs);
    QStringList candidates{m_sources.themeParent, m_sources.themeDirectory,
                           m_sources.fallbackThemeDirectory};
    for (const QString &directory : {m_sources.themeDirectory, m_sources.fallbackThemeDirectory}) {
        if (!directory.isEmpty()) {
            candidates << directory + QStringLiteral("/colors.toml");
            candidates << directory + QStringLiteral("/alacritty.toml");
        }
    }
    if (m_forcedLanguage.isEmpty()) {
        candidates << m_sources.languageFile;
        candidates << m_sources.localeFiles;
    }
    QStringList paths;
    for (const QString &candidate : std::as_const(candidates)) {
        if (candidate.isEmpty())
            continue;
        // Watch parents as well as files: atomic replacements discard inode
        // watches, and initially missing paths must become discoverable.
        if (QFileInfo::exists(candidate))
            paths << candidate;
        QString ancestor = QFileInfo(candidate).absolutePath();
        while (!QFileInfo::exists(ancestor) && ancestor != QLatin1String("/"))
            ancestor = QFileInfo(ancestor).absolutePath();
        paths << ancestor;
    }
    paths.removeDuplicates();
    if (!paths.isEmpty())
        m_watcher->addPaths(paths);
}

} // namespace rasklad::app
