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
    const auto parsed = map ? configFromMap(*map) : std::nullopt;
    m_loaded = parsed.has_value();
    if (parsed) m_config = *parsed;
}

void SettingsBackend::refreshIfStale()
{
    // Window focus calls this: at most one full refresh every 3 s.
    if (m_lastRefresh.isValid() && m_lastRefresh.elapsed() < 3000) return;
    refresh();
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
    // Only a good refresh pauses the focus refresh, so recovery is retried at once.
    if (m_loaded) m_lastRefresh.start();
    else m_lastRefresh.invalidate();
    rebuildApps();
}

bool SettingsBackend::fresh()
{
    // A change starts from what the addon holds now, not from the window's copy:
    // fcitx5-configtool may have changed it, or Fcitx may have restarted with an
    // older addon that must never get keys it does not know.
    m_error.clear();
    m_running = m_fcitx->running();
    reloadConfig();
    if (m_loaded) return true;
    m_error = QStringLiteral("fcitx");
    rebuildApps();
    return false;
}

bool SettingsBackend::commit(const AddonConfig &next)
{
    m_error.clear();
    // Write only what changed. Fcitx applies an addon's SetConfig partially, so a
    // list this window shows capped (more than 64 apps or 256 words written by
    // another tool) stays as stored unless the user edits that list.
    QVariantMap changes = mapFromConfig(next);
    const QVariantMap now = mapFromConfig(m_config);
    for (auto it = now.cbegin(); it != now.cend(); ++it)
        if (changes.value(it.key()) == it.value()) changes.remove(it.key());
    if (!m_loaded || !m_fcitx->writeConfig(changes)) {
        m_error = m_loaded ? QStringLiteral("write") : QStringLiteral("fcitx");
        rebuildApps();
        return false;
    }
    // Show what the addon actually holds now, not what was asked for.
    reloadConfig();
    // Only the written keys must match: another client may change the others.
    if (!m_loaded) {
        m_error = QStringLiteral("fcitx");
    } else {
        const QVariantMap after = mapFromConfig(m_config);
        for (auto it = changes.cbegin(); it != changes.cend(); ++it)
            if (after.value(it.key()) != it.value()) m_error = QStringLiteral("write");
    }
    rebuildApps();
    return m_error.isEmpty();
}

void SettingsBackend::setAppEnabled(const QString &id, bool enabled)
{
    if (!validAppId(id)) return;
    if (enabled && isTerminal(id)) {
        m_error = QStringLiteral("terminal");
        rebuildApps();
        return;
    }
    if (!fresh()) return;
    AddonConfig next = m_config;
    if (enabled) {
        if (next.apps.contains(id)) {
            rebuildApps();
            return;
        }
        if (next.apps.size() >= kMaxApps) {
            m_error = QStringLiteral("full");
            rebuildApps();
            return;
        }
        next.apps.append(id);
    } else {
        if (!next.apps.removeAll(id)) {
            rebuildApps();
            return;
        }
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
    if (!fresh()) return;
    if (on == m_config.sound) {
        rebuildApps();
        return;
    }
    AddonConfig next = m_config;
    next.sound = on;
    commit(next);
}

void SettingsBackend::setAutoCorrect(bool on)
{
    if (!fresh()) return;
    if (!wordOptions() || on == m_config.autoCorrect) {
        rebuildApps();
        return;
    }
    AddonConfig next = m_config;
    next.autoCorrect = on;
    commit(next);
}

bool SettingsBackend::addException(const QString &raw)
{
    if (!fresh()) return false;
    if (!wordOptions()) {
        rebuildApps();
        return false;
    }
    const QString word = normalizeException(raw.trimmed()); // typed input: spaces around are fine
    if (word.isEmpty()) {
        m_error = QStringLiteral("word");
        rebuildApps();
        return false;
    }
    // Already there in either layout's form ("ok" for "щл"): nothing to add.
    const QString key = exceptionKey(word);
    for (const QString &have : m_config.exceptions)
        if (exceptionKey(have) == key) {
            rebuildApps();
            return true;
        }
    if (m_config.exceptions.size() >= kMaxExceptions) {
        m_error = QStringLiteral("words-full");
        rebuildApps();
        return false;
    }
    AddonConfig next = m_config;
    next.exceptions.append(word);
    std::sort(next.exceptions.begin(), next.exceptions.end());
    return commit(next);
}

void SettingsBackend::removeException(const QString &word)
{
    if (!fresh()) return;
    // By key: the chip may show "ok" while another tool has since stored "щл".
    const QString key = exceptionKey(word);
    AddonConfig next = m_config;
    const auto removed = next.exceptions.removeIf([&](const QString &have) { return exceptionKey(have) == key; });
    if (!wordOptions() || key.isEmpty() || removed == 0) {
        rebuildApps();
        return;
    }
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
