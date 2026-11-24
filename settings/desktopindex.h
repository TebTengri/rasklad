// SPDX-License-Identifier: MIT
// SYS-254: display names for program ids, read from the XDG .desktop files.
// A small parser of its own (no QSettings, no QtGui): it reads only the
// [Desktop Entry] group and only the keys it needs. Nothing is logged or
// written, and of Exec only the program basename is kept, for matching.
#pragma once

#include <QString>
#include <QStringList>

#include <optional>
#include <vector>

namespace rasklad::settings {

struct AppInfo {
    QString name;        // localized display name, never empty when found
    QString description; // localized Comment/GenericName; empty if none or equal to name
    QString icon;        // Icon= value (theme name or absolute path), may be empty
    bool webApp = false; // chrome-<site>__-Default style id
};

class DesktopIndex
{
public:
    inline static constexpr int kMaxFiles = 2000;          // across all directories
    inline static constexpr qint64 kMaxFileSize = 64 * 1024; // larger files are skipped

    // Directories in XDG precedence (earlier wins). Default: QStandardPaths::standardLocations(ApplicationsLocation).
    static QStringList defaultDirs();
    // Replaces what was loaded before. "ru" tries Name[ru_RU], then Name[ru];
    // "ru_RU" may be given as well.
    void load(const QStringList &dirs, const QString &language = QStringLiteral("ru"));
    // Case-insensitive: StartupWMClass, then the desktop id (or its last
    // reverse-DNS segment), then a unique Exec basename. nullopt: no entry and
    // not a web app.
    std::optional<AppInfo> find(const QString &program) const;
    int size() const;

private:
    struct Entry {
        QString id;       // desktop file id without ".desktop", lowercase
        QString wmClass;  // lowercase
        QString execBase; // lowercase basename of the program, nothing else of Exec
        bool noDisplay = false;
        AppInfo info;
    };
    std::vector<Entry> m_entries; // in precedence order
};

// Programs never offered: system shells, launchers, notifiers and the settings app itself.
bool isSystemComponent(const QString &id);
// One row per app: "chromium-browser" -> "chromium"; any other id returns itself. Case preserved for others.
QString canonicalApp(const QString &id);
// All program names sharing the canonical id, canonical first. ("chromium" -> {"chromium","chromium-browser"}).
QStringList appAliases(const QString &canonical);

} // namespace rasklad::settings
