// SPDX-License-Identifier: MIT
// SYS-227: state and actions of the settings window. Every change is written to
// Fcitx and then read back, so the window always shows what the addon applied.
#pragma once

#include "settingscore.h"
#include "sources.h"

#include <QElapsedTimer>
#include <QObject>
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
    Q_PROPERTY(bool autoCorrect READ autoCorrect NOTIFY changed)
    Q_PROPERTY(QStringList exceptions READ exceptions NOTIFY changed)
    Q_PROPERTY(QVariantList apps READ apps NOTIFY changed)
    Q_PROPERTY(int enabledCount READ enabledCount NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)

public:
    SettingsBackend(FcitxSource *fcitx, WindowSource *windows, QObject *parent = nullptr);

    bool fcitxRunning() const { return m_running; }
    bool addonEnabled() const { return m_addon; }
    bool configLoaded() const { return m_loaded; }
    bool layoutsReady() const { return m_layouts; }
    bool sound() const { return m_config.sound; }
    bool wordOptions() const { return m_loaded && m_config.wordOptions; }
    bool autoCorrect() const { return m_config.autoCorrect; }
    QStringList exceptions() const { return m_config.exceptions; }
    QVariantList apps() const { return m_apps; }
    int enabledCount() const { return static_cast<int>(m_config.apps.size()); }
    // "", "fcitx", "write", "invalid", "terminal", "full", "word", "words-full"
    QString error() const { return m_error; }

    Q_INVOKABLE void refresh();
    Q_INVOKABLE void refreshIfStale();
    Q_INVOKABLE void setAppEnabled(const QString &id, bool enabled);
    Q_INVOKABLE bool addApp(const QString &id);
    Q_INVOKABLE void setSound(bool on);
    Q_INVOKABLE void setAutoCorrect(bool on);
    Q_INVOKABLE bool addException(const QString &word);
    Q_INVOKABLE void removeException(const QString &word);

Q_SIGNALS:
    void changed();

private:
    void reloadConfig();
    bool commit(const AddonConfig &next);
    void rebuildApps();

    FcitxSource *m_fcitx;
    WindowSource *m_windows;
    bool m_running = false;
    bool m_addon = false;
    bool m_loaded = false;
    bool m_layouts = false;
    AddonConfig m_config;
    QStringList m_open;
    QStringList m_seen; // switched off in this session: stay visible to switch back on
    QVariantList m_apps;
    QString m_error;
    QElapsedTimer m_lastRefresh;
};

} // namespace rasklad::settings
