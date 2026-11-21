// SPDX-License-Identifier: MIT
// SYS-227: where the settings app reads and writes. The real implementations
// talk to Fcitx over the session D-Bus (the same Controller1 calls
// fcitx5-configtool uses, so the addon applies changes at once and saves its
// own file) and ask Hyprland for open window classes. Tests inject fakes.
#pragma once

#include <QString>
#include <QStringList>
#include <QVariantMap>

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
};

class WindowSource
{
public:
    virtual ~WindowSource() = default;
    // Window classes of open windows; empty if Hyprland is not reachable.
    virtual QStringList openApps() = 0;
};

class DBusFcitx final : public FcitxSource
{
public:
    bool running() override;
    bool addonEnabled() override;
    QStringList groupMethods() override;
    std::optional<QVariantMap> readConfig() override;
    bool writeConfig(const QVariantMap &config) override;
};

class HyprlandWindows final : public WindowSource
{
public:
    QStringList openApps() override;
};

} // namespace rasklad::settings
