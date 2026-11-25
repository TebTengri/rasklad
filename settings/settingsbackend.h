// SPDX-License-Identifier: MIT
// SYS-227: state and actions of the settings window. Every change is written to
// Fcitx and then read back, so the window always shows what the addon applied.
#pragma once

#include "settingscore.h"
#include "sources.h"

#include <QElapsedTimer>
#include <QHash>
#include <QObject>
#include <QTimer>
#include <QStringList>
#include <QVariantList>

namespace rasklad::settings {

class SettingsBackend final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool fcitxRunning READ fcitxRunning NOTIFY changed)
    Q_PROPERTY(bool addonEnabled READ addonEnabled NOTIFY changed)
    Q_PROPERTY(bool configLoaded READ configLoaded NOTIFY changed)
    Q_PROPERTY(bool layoutsReady READ layoutsReady NOTIFY changed)
    Q_PROPERTY(bool sound READ sound NOTIFY changed)
    Q_PROPERTY(bool wordOptions READ wordOptions NOTIFY changed)
    Q_PROPERTY(bool switchable READ switchable NOTIFY changed)
    Q_PROPERTY(bool raskladEnabled READ raskladEnabled NOTIFY changed)
    Q_PROPERTY(bool autoCorrect READ autoCorrect NOTIFY changed)
    Q_PROPERTY(bool profileBroken READ profileBroken NOTIFY changed)
    Q_PROPERTY(QStringList exceptions READ exceptions NOTIFY changed)
    Q_PROPERTY(QVariantList apps READ apps NOTIFY changed)
    Q_PROPERTY(int enabledCount READ enabledCount NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
    // SYS-260: {step: state} from rasklad-setup --check; empty hides the section.
    Q_PROPERTY(QVariantMap setup READ setup NOTIFY changed)
    // Row states for the three rows; true when rasklad-setup --install left something to revert.
    Q_PROPERTY(QVariantMap setupRows READ setupRows NOTIFY changed)
    Q_PROPERTY(bool setupInstalled READ setupInstalled NOTIFY changed)

public:
    // names may be null: rows then show program names (tests, or no .desktop entries).
    SettingsBackend(FcitxSource *fcitx, AppSource *apps, AppNames *names = nullptr, SetupTool *setup = nullptr,
                    QObject *parent = nullptr);
    ~SettingsBackend() override;

    bool fcitxRunning() const { return m_running; }
    bool addonEnabled() const { return m_addon; }
    bool configLoaded() const { return m_loaded; }
    bool layoutsReady() const { return m_layouts; }
    bool sound() const { return m_config.sound; }
    bool wordOptions() const { return m_loaded && m_config.wordOptions; }
    // An addon without the switch (before 0.5.30) is always on.
    bool switchable() const { return m_loaded && m_config.switchable; }
    bool raskladEnabled() const { return !switchable() || m_config.enabled; }
    bool autoCorrect() const { return m_config.autoCorrect; }
    // Only a definite "not verified" from a running addon; unknown is not broken.
    bool profileBroken() const { return m_profileBroken; }
    QStringList exceptions() const { return m_config.exceptions; }
    QVariantList apps() const { return m_apps; }
    int enabledCount() const { return static_cast<int>(m_config.apps.size()); }
    // "", "fcitx", "write", "invalid", "terminal", "full", "word", "words-full"
    QString error() const { return m_error; }
    QVariantMap setup() const { return m_setup; }
    QVariantMap setupRows() const;
    bool setupInstalled() const { return m_setupInstalled; }

    Q_INVOKABLE void refresh();
    // Opens rasklad-setup --install or --revert in a terminal; false if it could not.
    Q_INVOKABLE bool openSetup(const QString &action);
    Q_INVOKABLE void refreshIfStale();
    Q_INVOKABLE void setAppEnabled(const QString &id, bool enabled);
    Q_INVOKABLE bool addApp(const QString &id);
    Q_INVOKABLE void setSound(bool on);
    Q_INVOKABLE void setAutoCorrect(bool on);
    Q_INVOKABLE void setRaskladEnabled(bool on);
    Q_INVOKABLE bool addException(const QString &word);
    Q_INVOKABLE void removeException(const QString &word);
    // SYS-256: "rasklad", "en", "ru", "data" or "ukacd" from the package license directory.
    Q_INVOKABLE QString licenseText(const QString &key) const;

Q_SIGNALS:
    void changed();

private:
    void reloadConfig();
    bool fresh();
    bool commit(const AddonConfig &next);
    void rebuildApps();

    FcitxSource *m_fcitx;
    AppSource *m_appSource;
    AppNames *m_names;
    SetupTool *m_setupTool;
    QVariantMap m_setup;
    bool m_setupInstalled = false;
    QTimer m_setupPoll; // after a terminal opens: look again until it is done
    int m_setupPolls = 0;
    bool m_running = false;
    bool m_addon = false;
    bool m_loaded = false;
    bool m_profileBroken = false;
    bool m_layouts = false;
    AddonConfig m_config;
    QVariantMap m_raw; // the addon's config exactly as read last (SYS-272: conflict check)
    QStringList m_focused; // SYS-253: programs the addon saw with a focused field
    QStringList m_seen; // switched off in this session: stay visible to switch back on
    std::optional<qint64> m_timesOn; // the addon's TimesOn at the last refresh
    // Group of each row, fixed at refresh: a switched row stays where it is under the cursor.
    QHash<QString, QString> m_groups;
    QVariantList m_apps;
    QString m_error;
    QElapsedTimer m_lastRefresh;
};

} // namespace rasklad::settings
