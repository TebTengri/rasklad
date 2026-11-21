// SPDX-License-Identifier: MIT
// SYS-227: pure logic of the settings app, kept free of D-Bus and processes so
// tests run without Fcitx or Hyprland.
#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QVariantMap>

#include <optional>

namespace rasklad::settings {

inline constexpr int kMaxApps = 64; // same cap as the addon

// Same character rules as the addon: [A-Za-z0-9._-], 1..128.
bool validAppId(const QString &id);
bool isTerminal(const QString &id);
// Apps checked end to end by the project (shown as "tested").
bool isTested(const QString &id);

struct AddonConfig {
    QStringList apps;
    bool sound = true;
};

// From the a{sv} Fcitx returns for fcitx://config/addon/rasklad (nested maps,
// string leaves; an empty list arrives as ""). nullopt if "Applications" is
// missing or of another shape: the caller must then not write, or it would
// replace the user's list. Invalid, terminal and duplicate ids are dropped.
std::optional<AddonConfig> configFromMap(const QVariantMap &map);
QVariantMap mapFromConfig(const AddonConfig &config);

// Window classes from `hyprctl -j clients`; only "class" is read, everything
// else (titles included) is ignored. Sorted, unique, valid ids only.
QStringList classesFromClients(const QByteArray &json);

// True if the current input-method group has both keyboard-us and keyboard-ru.
bool layoutsReady(const QStringList &groupMethods);

} // namespace rasklad::settings
