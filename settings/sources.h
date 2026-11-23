// SPDX-License-Identifier: MIT
// SYS-227: where the settings app reads and writes. The real implementations
// talk to Fcitx over the session D-Bus (the same Controller1 calls
// fcitx5-configtool uses, so the addon applies changes at once and saves its
// own file). Since SYS-253 the apps on offer come from the addon too: the
// program names it saw with a focused text field. Tests inject fakes.
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
};

} // namespace rasklad::settings
