#pragma once

// Read-only Omarchy theme and desktop-language follower (SYS-239).
//
// Colors follow $XDG_STATE_HOME/omarchy/current/theme (default ~/.local/state),
// then the legacy config location. Language follows --language, the explicit
// $XDG_CONFIG_HOME/omarchy/language code, then locale.conf/environment.
// Only bounded regular-file reads and debounced filesystem events are used;
// parent watches survive atomic replacement and initially absent files.
// No settings writes, subprocesses or polling. Corners stay sharp (radius 0);
// compositor rounding is deliberately not queried.

#include <QByteArray>
#include <QColor>
#include <QFileSystemWatcher>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>

#include <optional>

namespace rasklad::app {

inline constexpr qint64 kMaxThemeFileBytes = 64LL * 1024;

struct ThemePalette {
    QColor background;
    QColor surface;
    QColor foreground;
    QColor muted;
    QColor accent;
    QColor error;
    bool dark = true;
};

[[nodiscard]] ThemePalette defaultPalette();
// Minimal TOML subset: [section] headers and key = "#rrggbb" / "0xrrggbb".
// A complete palette or an accent-only file; missing palette uses the fallback.
[[nodiscard]] std::optional<ThemePalette> parseThemeColors(const QByteArray &bytes);
// "ru" for ru*, "en" for any other non-empty locale name, "" otherwise.
[[nodiscard]] QString languageFromLocaleName(const QString &name);
// LC_ALL > LC_MESSAGES > LANG from a locale.conf body; "" if none.
[[nodiscard]] QString languageFromLocaleConf(const QByteArray &bytes);

struct ThemeSources {
    QString themeDirectory;
    // Parent of the theme symlink, watched to notice theme switches.
    QString themeParent;
    QString fallbackThemeDirectory;
    QString languageFile;
    QStringList localeFiles;
};

[[nodiscard]] ThemeSources defaultThemeSources();

class ThemeWatcher final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QColor background READ background NOTIFY paletteChanged)
    Q_PROPERTY(QColor surface READ surface NOTIFY paletteChanged)
    Q_PROPERTY(QColor foreground READ foreground NOTIFY paletteChanged)
    Q_PROPERTY(QColor muted READ muted NOTIFY paletteChanged)
    Q_PROPERTY(QColor accent READ accent NOTIFY paletteChanged)
    Q_PROPERTY(QColor error READ error NOTIFY paletteChanged)
    Q_PROPERTY(bool dark READ dark NOTIFY paletteChanged)
    Q_PROPERTY(int radius READ radius CONSTANT)
    Q_PROPERTY(QString language READ language NOTIFY languageChanged)
    Q_PROPERTY(bool fromDesktopTheme READ fromDesktopTheme NOTIFY paletteChanged)

public:
    // forcedLanguage: "", "en" or "ru". watch: install file watchers.
    ThemeWatcher(ThemeSources sources, const QString &forcedLanguage, bool watch, QObject *parent = nullptr);

    [[nodiscard]] QColor background() const { return m_palette.background; }
    [[nodiscard]] QColor surface() const { return m_palette.surface; }
    [[nodiscard]] QColor foreground() const { return m_palette.foreground; }
    [[nodiscard]] QColor muted() const { return m_palette.muted; }
    [[nodiscard]] QColor accent() const { return m_palette.accent; }
    [[nodiscard]] QColor error() const { return m_palette.error; }
    [[nodiscard]] bool dark() const { return m_palette.dark; }
    [[nodiscard]] int radius() const { return 0; }
    [[nodiscard]] QString language() const { return m_language; }
    [[nodiscard]] bool fromDesktopTheme() const { return m_fromDesktop; }

    // Re-read now (also used by tests).
    void reload();

Q_SIGNALS:
    void paletteChanged();
    void languageChanged();

private:
    void rearmWatcher();

    ThemeSources m_sources;
    QString m_forcedLanguage;
    ThemePalette m_palette;
    bool m_fromDesktop = false;
    QString m_language;
    QFileSystemWatcher *m_watcher = nullptr;
    QTimer m_debounce;
};

} // namespace rasklad::app
