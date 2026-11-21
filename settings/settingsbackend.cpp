// SPDX-License-Identifier: MIT
#include "settingsbackend.h"

#include <algorithm>

namespace rasklad::settings {

SettingsBackend::SettingsBackend(FcitxSource *fcitx, WindowSource *windows, QObject *parent)
    : QObject(parent), m_fcitx(fcitx), m_windows(windows)
{
    refresh();
}

void SettingsBackend::reloadConfig()
{
    const auto map = m_running ? m_fcitx->readConfig() : std::nullopt;
    m_loaded = map.has_value();
    if (map) m_config = configFromMap(*map);
}

void SettingsBackend::refresh()
{
    m_error.clear();
    m_running = m_fcitx->running();
    m_addon = m_running && m_fcitx->addonEnabled();
    m_layouts = m_running && settings::layoutsReady(m_fcitx->groupMethods());
    reloadConfig();
    if (!m_loaded) m_error = QStringLiteral("fcitx");
    m_open = m_windows->openApps();
    rebuildApps();
}

bool SettingsBackend::commit(const AddonConfig &next)
{
    m_error.clear();
    if (!m_loaded || !m_fcitx->writeConfig(mapFromConfig(next))) {
        m_error = m_loaded ? QStringLiteral("write") : QStringLiteral("fcitx");
        rebuildApps();
        return false;
    }
    // Show what the addon actually holds now, not what was asked for.
    reloadConfig();
    if (!m_loaded) m_error = QStringLiteral("fcitx");
    rebuildApps();
    return true;
}

void SettingsBackend::setAppEnabled(const QString &id, bool enabled)
{
    if (!validAppId(id)) return;
    if (enabled && isTerminal(id)) {
        m_error = QStringLiteral("terminal");
        rebuildApps();
        return;
    }
    AddonConfig next = m_config;
    if (enabled) {
        if (next.apps.contains(id)) return;
        if (next.apps.size() >= kMaxApps) {
            m_error = QStringLiteral("full");
            rebuildApps();
            return;
        }
        next.apps.append(id);
    } else {
        if (!next.apps.removeAll(id)) return;
        if (!m_seen.contains(id)) m_seen.append(id);
    }
    commit(next);
}

bool SettingsBackend::addApp(const QString &raw)
{
    const QString id = raw.trimmed();
    m_error.clear();
    if (!validAppId(id)) {
        m_error = QStringLiteral("invalid");
        rebuildApps();
        return false;
    }
    if (isTerminal(id)) {
        m_error = QStringLiteral("terminal");
        rebuildApps();
        return false;
    }
    setAppEnabled(id, true);
    return m_error.isEmpty();
}

void SettingsBackend::setSound(bool on)
{
    if (on == m_config.sound) return;
    AddonConfig next = m_config;
    next.sound = on;
    commit(next);
}

void SettingsBackend::rebuildApps()
{
    QStringList ids = m_config.apps;
    for (const QStringList *more : {&m_open, &m_seen})
        for (const QString &id : *more)
            if (!ids.contains(id)) ids.append(id);
    for (const char *tested : {"slack", "chromium"})
        if (!ids.contains(QLatin1String(tested))) ids.append(QLatin1String(tested));
    std::sort(ids.begin(), ids.end(), [this](const QString &a, const QString &b) {
        const bool ea = m_config.apps.contains(a), eb = m_config.apps.contains(b);
        if (ea != eb) return ea;
        const bool ta = isTerminal(a), tb = isTerminal(b);
        if (ta != tb) return tb;
        return a.compare(b, Qt::CaseInsensitive) < 0;
    });
    m_apps.clear();
    for (const QString &id : ids)
        m_apps.append(QVariantMap{{QStringLiteral("id"), id},
                                  {QStringLiteral("enabled"), m_config.apps.contains(id)},
                                  {QStringLiteral("tested"), isTested(id)},
                                  {QStringLiteral("open"), m_open.contains(id)},
                                  {QStringLiteral("terminal"), isTerminal(id)}});
    Q_EMIT changed();
}

} // namespace rasklad::settings
