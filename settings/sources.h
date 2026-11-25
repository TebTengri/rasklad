// SPDX-License-Identifier: MIT
// SYS-227: where the settings app reads and writes. The real implementations
// talk to Fcitx over the session D-Bus (the same Controller1 calls
// fcitx5-configtool uses, so the addon applies changes at once and saves its
// own file). Since SYS-253 the apps on offer come from the addon too: the
// program names it saw with a focused text field. Tests inject fakes.
#pragma once

#include <QString>
#include <optional>
#include <QStringList>
#include <QVariantMap>

#include "desktopindex.h"

#include <QElapsedTimer>
#include <QHash>
#include <QProcess>

#include <functional>
#include <memory>
#include <optional>

namespace rasklad::settings {

class FcitxSource
{
public:
    virtual ~FcitxSource() = default;
    virtual bool running() = 0;
    virtual bool addonEnabled() = 0;
    virtual QStringList groupMethods() = 0;
    virtual std::optional<QVariantMap> readConfig() = 0;
    virtual bool writeConfig(const QVariantMap &config) = 0;
    // SYS-248: the addon's read-only status. nullopt: unknown (Fcitx down or an
    // older addon); false: the dictionary profile did not pass verification.
    virtual std::optional<bool> profileReady() = 0;
};

class AppSource
{
public:
    virtual ~AppSource() = default;
    // Program names the addon saw with a focused text field since it started;
    // empty if Fcitx is not reachable or the addon is older than SYS-253.
    virtual QStringList seenApps() = 0;
    // SYS-272: "OnEpoch" from the addon status; empty from an older addon.
    virtual QString onEpoch() { return {}; }
};

// SYS-254: Hyprland's decoration:rounding over its request socket (no subprocess),
// read once at start; 0 outside Hyprland or on any failure.
int hyprlandRounding();
// SYS-261: asks Hyprland to focus the window of this process; false outside Hyprland.
bool hyprlandFocusPid(qint64 pid);

// SYS-254: human names, descriptions and icons for program names.
class AppNames
{
public:
    virtual ~AppNames() = default;
    virtual void reload() {}
    virtual std::optional<AppInfo> find(const QString &program) const = 0;
};

// From the installed .desktop entries, in the window's language; reloaded on refresh.
class DesktopAppNames final : public AppNames
{
public:
    explicit DesktopAppNames(std::function<QString()> language) : m_language(std::move(language)) {}
    void reload() override { m_index.load(DesktopIndex::defaultDirs(), m_language()); }
    std::optional<AppInfo> find(const QString &program) const override { return m_index.find(program); }

private:
    std::function<QString()> m_language;
    DesktopIndex m_index;
};

// SYS-260: the packaged rasklad-setup. requestCheck() starts `--check`
// (read-only, a few lines of states) without blocking the window; when it ends,
// lastCheck() holds the output and updated() is called. open() shows `--install`
// or `--revert` in Omarchy's floating terminal, where the user sees the changes
// and answers the prompt.
class SetupTool
{
public:
    virtual ~SetupTool() = default;
    virtual void requestCheck() = 0;                          // no-op while one runs
    virtual std::optional<QByteArray> lastCheck() const = 0; // nullopt: not installed or no answer
    virtual bool open(const QString &action) = 0;            // "install" or "revert"
    virtual bool installed() const = 0; // --install recorded changes that --revert can undo
    std::function<void()> updated;
};

class RaskladSetup final : public SetupTool
{
public:
    RaskladSetup();
    ~RaskladSetup() override;
    void requestCheck() override;
    std::optional<QByteArray> lastCheck() const override { return m_last; }
    bool open(const QString &action) override;
    bool installed() const override;

private:
    void finish(std::optional<QByteArray> result);
    std::unique_ptr<QProcess> m_process;
    bool m_again = false; // asked again while one ran: its answer may predate the request
    QByteArray m_out;
    std::optional<QByteArray> m_last;
    QHash<QString, QElapsedTimer> m_opened; // per action: a double click opens one terminal
};

class DBusFcitx final : public FcitxSource
{
public:
    bool running() override;
    bool addonEnabled() override;
    QStringList groupMethods() override;
    std::optional<QVariantMap> readConfig() override;
    std::optional<bool> profileReady() override;
    bool writeConfig(const QVariantMap &config) override;
};

class FcitxSeenApps final : public AppSource
{
public:
    QStringList seenApps() override;
    QString onEpoch() override;
};

} // namespace rasklad::settings
