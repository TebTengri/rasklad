// SPDX-License-Identifier: MIT
#include "settingsbackend.h"

#include <algorithm>

namespace rasklad::settings {

SettingsBackend::SettingsBackend(FcitxSource *fcitx, AppSource *apps, AppNames *names, SetupTool *setup,
                                 QObject *parent)
    : QObject(parent), m_fcitx(fcitx), m_appSource(apps), m_names(names), m_setupTool(setup)
{
    if (m_setupTool) {
        m_setupTool->updated = [this] {
            const auto out = m_setupTool->lastCheck();
            const QVariantMap next = out ? setupFromCheck(*out) : QVariantMap{};
            const bool installed = m_setupTool->installed();
            const bool moved = !m_setup.isEmpty() && (next != m_setup || installed != m_setupInstalled);
            m_setup = next;
            m_setupInstalled = installed;
            // Something outside changed the desktop (the terminal): read Fcitx again
            // once, so the banner and lists match. The next check finds no change.
            if (moved) refresh();
            else Q_EMIT changed();
        };
    }
    // The terminal gives no completion signal: every 4 s for 3 minutes, start the
    // asynchronous check (no blocking call here); a changed answer refreshes once.
    m_setupPoll.setInterval(4000);
    connect(&m_setupPoll, &QTimer::timeout, this, [this] {
        if (++m_setupPolls >= 45) m_setupPoll.stop();
        if (m_setupTool) m_setupTool->requestCheck();
    });
    refresh();
}

SettingsBackend::~SettingsBackend()
{
    if (m_setupTool) m_setupTool->updated = nullptr;
}

void SettingsBackend::reloadConfig()
{
    const auto map = m_running ? m_fcitx->readConfig() : std::nullopt;
    const auto parsed = map ? configFromMap(*map) : std::nullopt;
    m_loaded = parsed.has_value();
    if (parsed) m_config = *parsed;
    // Asked again on every read (refresh and before each change), so a restarted
    // Fcitx never keeps showing the previous daemon's warning.
    m_profileBroken = m_running && m_fcitx->profileReady() == std::optional<bool>(false);
}

bool SettingsBackend::openSetup(const QString &action)
{
    // The window learns the result on its next focus refresh, after the terminal.
    if (!m_setupTool || !m_setupTool->open(action)) return false;
    m_lastRefresh.invalidate(); // the next focus refreshes everything
    m_setupPolls = 0;
    m_setupPoll.start();
    return true;
}

QVariantMap SettingsBackend::setupRows() const
{
    if (m_setup.isEmpty()) return {};
    return {{QStringLiteral("icon"), setupRowState(m_setup, {QStringLiteral("bar-widget-link"), QStringLiteral("bar-widget")})},
            {QStringLiteral("alt"), setupRowState(m_setup, {QStringLiteral("double-alt")})},
            {QStringLiteral("layouts"),
             setupRowState(m_setup, {QStringLiteral("fcitx-group"), QStringLiteral("hyprland-layout")})}};
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
    m_focused = m_running ? m_appSource->seenApps() : QStringList{};
    if (m_names) m_names->reload();
    if (m_setupTool) m_setupTool->requestCheck(); // answers later through updated()
    m_groups.clear(); // regrouped only here, never on a switch
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
    // SYS-254: a row is one app; every program name it answers to switches with it
    // (Chromium is "chromium" under X11/GTK and "chromium-browser" on Wayland).
    const QString app = canonicalApp(id);
    const QStringList names = appAliases(app);
    AddonConfig next = m_config;
    if (enabled) {
        QStringList missing;
        for (const QString &n : names)
            if (!next.apps.contains(n)) missing.append(n);
        if (missing.isEmpty()) {
            rebuildApps();
            return;
        }
        if (next.apps.size() + missing.size() > kMaxApps) {
            m_error = QStringLiteral("full");
            rebuildApps();
            return;
        }
        next.apps.append(missing);
    } else {
        qsizetype removed = 0;
        for (const QString &n : names) removed += next.apps.removeAll(n);
        if (removed == 0) {
            rebuildApps();
            return;
        }
        if (!m_seen.contains(app)) m_seen.append(app);
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
    // One row per app (canonical name). System components (the shell, launchers)
    // are never offered; terminals never reach this list (configFromMap drops them).
    QStringList apps;
    const auto add = [&](const QString &id, bool stored) {
        const QString app = canonicalApp(id);
        // A component already switched on stays listed, so it can be switched off.
        if ((stored || !isSystemComponent(app)) && !isTerminal(app) && !apps.contains(app)) apps.append(app);
    };
    for (const QString &id : m_config.apps) add(id, true);
    for (const QStringList *list : {&m_focused, &m_seen})
        for (const QString &id : *list) add(id, false);
    for (const char *tested : {"slack", "chromium"}) add(QLatin1String(tested), false);

    QList<QVariantMap> rows;
    for (const QString &app : apps) {
        const QStringList names = appAliases(app);
        int on = 0;
        for (const QString &n : names) on += m_config.apps.contains(n) ? 1 : 0;
        bool focused = false;
        for (const QString &n : names) focused = focused || m_focused.contains(n);
        QString group = m_groups.value(app);
        if (group.isEmpty()) {
            // Fixed for this refresh, so switching never moves a row under the cursor.
            group = on > 0 ? QStringLiteral("on")
                  : (focused || m_seen.contains(app) || !isTested(app)) ? QStringLiteral("recent")
                  : QStringLiteral("recommended");
            m_groups.insert(app, group);
        }
        QVariantMap row{{QStringLiteral("id"), app},
                        {QStringLiteral("names"), names},
                        {QStringLiteral("enabled"), on > 0},
                        {QStringLiteral("partial"), on > 0 && on < names.size()},
                        {QStringLiteral("group"), group},
                        {QStringLiteral("name"), app},
                        {QStringLiteral("description"), QString()},
                        {QStringLiteral("icon"), QString()},
                        {QStringLiteral("webApp"), false}};
        std::optional<AppInfo> info;
        for (const QString &n : names) // the entry may know only one of the names
            if (m_names && !info) info = m_names->find(n);
        if (info) {
            row.insert(QStringLiteral("name"), info->name);
            row.insert(QStringLiteral("description"), info->description);
            row.insert(QStringLiteral("icon"), info->icon);
            row.insert(QStringLiteral("webApp"), info->webApp);
        }
        rows.append(row);
    }
    static const QStringList order{QStringLiteral("on"), QStringLiteral("recent"), QStringLiteral("recommended")};
    std::sort(rows.begin(), rows.end(), [](const QVariantMap &a, const QVariantMap &b) {
        const auto ga = order.indexOf(a.value(QStringLiteral("group")).toString());
        const auto gb = order.indexOf(b.value(QStringLiteral("group")).toString());
        if (ga != gb) return ga < gb;
        return a.value(QStringLiteral("name")).toString().compare(b.value(QStringLiteral("name")).toString(),
                                                                  Qt::CaseInsensitive) < 0;
    });
    m_apps.clear();
    for (const QVariantMap &row : rows) m_apps.append(row);
    Q_EMIT changed();
}

} // namespace rasklad::settings
