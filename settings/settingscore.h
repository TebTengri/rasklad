// SPDX-License-Identifier: MIT
// SYS-227: pure logic of the settings app, kept free of D-Bus and processes so
// tests run without Fcitx or Hyprland.
#pragma once

#include "configrules.h"

#include <QString>
#include <QStringList>
#include <QVariantMap>

#include <optional>

namespace rasklad::settings {

// The addon's own caps (src/configrules.h), so the two never disagree.
inline constexpr int kMaxApps = static_cast<int>(rasklad::config::kMaxApps);
inline constexpr int kMaxExceptions = static_cast<int>(rasklad::config::kMaxExceptions);

// Same character rules as the addon: [A-Za-z0-9._-], 1..128.
bool validAppId(const QString &id);
bool isTerminal(const QString &id);
// Apps checked end to end by the project (shown as "tested").
bool isTested(const QString &id);

// An exception word as stored: lowercase, one word of Latin or Cyrillic letters
// (same rule as the addon, checked as written: no trimming). "" if it is not a
// valid exception.
QString normalizeException(const QString &word);
// Same entry in either layout: "ok" and "щл" share their keys.
QString exceptionKey(const QString &word);

struct AddonConfig {
    QStringList apps;
    bool sound = true;
    // The addon has AutoCorrect and Exceptions (0.5 and later); an older addon
    // never gets them written.
    bool wordOptions = false;
    bool autoCorrect = true;
    QStringList exceptions;
    // The addon has the on/off switch (0.5.30 and later; off until turned on).
    bool switchable = false;
    bool enabled = false;
};

// From the a{sv} Fcitx returns for fcitx://config/addon/rasklad (nested maps,
// string leaves; an empty list arrives as ""). nullopt if "Applications" is
// missing or of another shape (or "Exceptions" present in another shape): the
// caller must then not write, or it would replace the user's list. Invalid,
// terminal and duplicate ids and invalid or duplicate exceptions are dropped.
std::optional<AddonConfig> configFromMap(const QVariantMap &map);
QVariantMap mapFromConfig(const AddonConfig &config);

// SYS-253: "SeenApplications" from the addon status (fcitx://config/addon/rasklad/status):
// program names, exactly as Fcitx reports them, of apps where a text field got
// focus since the addon started. Sorted, unique, valid ids only, at most kMaxApps;
// the settings app itself is left out. Empty if the key is missing (older addon)
// or of another shape.
QStringList seenAppsFromStatus(const QVariantMap &status);

// SYS-272: "OnEpoch" from the addon status ("<hex>-<decimal>", at most 40
// characters); empty if missing (older addon) or malformed.
QString onEpochFromStatus(const QVariantMap &status);

// SYS-254: Hyprland's reply to `j/getoption decoration:rounding`: the corner
// radius Omarchy apps use. 0 (sharp, Omarchy's default) for any other reply;
// capped at 24 so a stray value cannot distort the window.
int roundingFromReply(const QByteArray &json);

// SYS-260: `rasklad-setup --check` output ("<state> <step> [note]" per line) as
// {step: state}, known steps and states only; notes are dropped. Empty for
// anything else, so the section hides instead of guessing.
QVariantMap setupFromCheck(const QByteArray &output);

// SYS-260: one row state for several steps, for the window:
//   "ok" every step ok; "missing" something to connect; "other" set up some
//   other way (the tool leaves it alone); "problem" the tool reported an error;
//   "unknown" a step absent or not checkable. Worst first: problem, missing,
//   unknown, other; never "ok" with an unknown step.
QString setupRowState(const QVariantMap &setup, const QStringList &steps);
// SYS-272: why a row is "set up differently": "link-taken", "symlink",
// "other-layouts", "edited" or "other" for the first such step; "" if none.
QString setupRowReason(const QVariantMap &setup, const QStringList &steps);

// True if the current input-method group has both keyboard-us and keyboard-ru.
bool layoutsReady(const QStringList &groupMethods);

} // namespace rasklad::settings
