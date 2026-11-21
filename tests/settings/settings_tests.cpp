// SPDX-License-Identifier: MIT
// SYS-227: settings app logic against fake Fcitx and Hyprland sources.
#include "settingsbackend.h"
#include "settingscore.h"

#include <QtTest>

using namespace rasklad::settings;

namespace {

QVariantMap fcitxMap(const QStringList &apps, bool sound)
{
    QVariantMap list;
    for (int i = 0; i < apps.size(); ++i) list.insert(QString::number(i), apps[i]);
    return {{QStringLiteral("Applications"), list},
            {QStringLiteral("Sound"), sound ? QStringLiteral("True") : QStringLiteral("False")}};
}

// Behaves like the addon: stores what it is given and drops terminals/invalid ids.
class FakeFcitx final : public FcitxSource
{
public:
    bool up = true;
    bool failWrites = false;
    int writes = 0;
    QVariantMap stored = fcitxMap({QStringLiteral("slack"), QStringLiteral("chromium")}, true);
    QStringList group{QStringLiteral("keyboard-us"), QStringLiteral("keyboard-ru")};
    bool running() override { return up; }
    bool addonEnabled() override { return up; }
    QStringList groupMethods() override { return group; }
    std::optional<QVariantMap> readConfig() override
    {
        if (!up) return std::nullopt;
        return stored;
    }
    bool writeConfig(const QVariantMap &config) override
    {
        ++writes;
        if (!up || failWrites) return false;
        stored = mapFromConfig(configFromMap(config));
        return true;
    }
};

class FakeWindows final : public WindowSource
{
public:
    QStringList open{QStringLiteral("foot"), QStringLiteral("chatgpt"), QStringLiteral("slack")};
    QStringList openApps() override { return open; }
};

QVariantMap entry(const SettingsBackend &b, const QString &id)
{
    for (const QVariant &v : b.apps())
        if (v.toMap().value(QStringLiteral("id")).toString() == id) return v.toMap();
    return {};
}

} // namespace

class SettingsTests final : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void idRulesAndTerminals()
    {
        QVERIFY(validAppId(QStringLiteral("org.omarchy.agent")));
        QVERIFY(validAppId(QStringLiteral("chrome-youtube.com__-Default")));
        QVERIFY(!validAppId(QString()));
        QVERIFY(!validAppId(QStringLiteral("a b")));
        QVERIFY(!validAppId(QStringLiteral("слак")));
        QVERIFY(!validAppId(QString(129, QLatin1Char('a'))));
        QVERIFY(isTerminal(QStringLiteral("foot")));
        QVERIFY(isTerminal(QStringLiteral("Alacritty")));
        QVERIFY(isTerminal(QStringLiteral("com.mitchellh.ghostty")));
        QVERIFY(!isTerminal(QStringLiteral("slack")));
        QVERIFY(!isTerminal(QStringLiteral("footx")));
    }
    void configRoundTrip()
    {
        QVariantMap apps{{QStringLiteral("1"), QStringLiteral("chromium")},
                         {QStringLiteral("0"), QStringLiteral("slack")},
                         {QStringLiteral("2"), QStringLiteral("foot")},
                         {QStringLiteral("3"), QStringLiteral("bad id")},
                         {QStringLiteral("4"), QStringLiteral("slack")},
                         {QStringLiteral("x"), QStringLiteral("ignored")}};
        const AddonConfig c = configFromMap({{QStringLiteral("Applications"), apps},
                                             {QStringLiteral("Sound"), QStringLiteral("False")}});
        QCOMPARE(c.apps, (QStringList{QStringLiteral("slack"), QStringLiteral("chromium")}));
        QVERIFY(!c.sound);
        const QVariantMap back = mapFromConfig(c);
        QCOMPARE(back.value(QStringLiteral("Sound")).toString(), QStringLiteral("False"));
        QCOMPARE(back.value(QStringLiteral("Applications")).toMap().value(QStringLiteral("1")).toString(),
                 QStringLiteral("chromium"));
        QVERIFY(configFromMap({}).sound); // default on, like the addon
    }
    void clientsParseOnlyClasses()
    {
        const QByteArray json = R"([{"class":"slack","title":"secret title"},{"class":"foot"},
            {"class":"slack"},{"class":"a b"},{"title":"no class"}])";
        QCOMPARE(classesFromClients(json), (QStringList{QStringLiteral("foot"), QStringLiteral("slack")}));
        QVERIFY(classesFromClients("not json").isEmpty());
    }
    void layouts()
    {
        QVERIFY(layoutsReady({QStringLiteral("keyboard-us"), QStringLiteral("keyboard-ru")}));
        QVERIFY(!layoutsReady({QStringLiteral("keyboard-us")}));
    }
    void listShowsEnabledOpenAndTerminals()
    {
        FakeFcitx f;
        FakeWindows w;
        SettingsBackend b(&f, &w);
        QVERIFY(b.fcitxRunning() && b.addonEnabled() && b.configLoaded() && b.layoutsReady());
        QCOMPARE(b.enabledCount(), 2);
        QVERIFY(entry(b, QStringLiteral("slack")).value(QStringLiteral("enabled")).toBool());
        QVERIFY(entry(b, QStringLiteral("slack")).value(QStringLiteral("tested")).toBool());
        QVERIFY(entry(b, QStringLiteral("slack")).value(QStringLiteral("open")).toBool());
        QVERIFY(!entry(b, QStringLiteral("chatgpt")).value(QStringLiteral("enabled")).toBool());
        QVERIFY(entry(b, QStringLiteral("foot")).value(QStringLiteral("terminal")).toBool());
        // Enabled first, terminals last.
        QCOMPARE(b.apps().constFirst().toMap().value(QStringLiteral("enabled")).toBool(), true);
        QCOMPARE(b.apps().constLast().toMap().value(QStringLiteral("id")).toString(), QStringLiteral("foot"));
    }
    void toggleWritesAndReadsBack()
    {
        FakeFcitx f;
        FakeWindows w;
        SettingsBackend b(&f, &w);
        b.setAppEnabled(QStringLiteral("chatgpt"), true);
        QCOMPARE(b.error(), QString());
        QVERIFY(configFromMap(f.stored).apps.contains(QStringLiteral("chatgpt")));
        QVERIFY(entry(b, QStringLiteral("chatgpt")).value(QStringLiteral("enabled")).toBool());
        b.setAppEnabled(QStringLiteral("chromium"), false);
        QVERIFY(!configFromMap(f.stored).apps.contains(QStringLiteral("chromium")));
        QVERIFY(!entry(b, QStringLiteral("chromium")).isEmpty()); // still listed to switch back on
        w.open.clear();
        b.refresh();
        QVERIFY(!entry(b, QStringLiteral("chromium")).isEmpty());
        b.setSound(false);
        QVERIFY(!configFromMap(f.stored).sound);
        QVERIFY(!b.sound());
    }
    void terminalsAndBadIdsRefused()
    {
        FakeFcitx f;
        FakeWindows w;
        SettingsBackend b(&f, &w);
        const int before = f.writes;
        b.setAppEnabled(QStringLiteral("foot"), true);
        QCOMPARE(b.error(), QStringLiteral("terminal"));
        QVERIFY(!b.addApp(QStringLiteral("kitty")));
        QCOMPARE(b.error(), QStringLiteral("terminal"));
        QVERIFY(!b.addApp(QStringLiteral("bad id")));
        QCOMPARE(b.error(), QStringLiteral("invalid"));
        QCOMPARE(f.writes, before);
        QVERIFY(b.addApp(QStringLiteral("  org.telegram.desktop ")));
        QVERIFY(configFromMap(f.stored).apps.contains(QStringLiteral("org.telegram.desktop")));
    }
    void failuresShowTruth()
    {
        FakeFcitx f;
        FakeWindows w;
        SettingsBackend b(&f, &w);
        f.failWrites = true;
        b.setAppEnabled(QStringLiteral("chatgpt"), true);
        QCOMPARE(b.error(), QStringLiteral("write"));
        QVERIFY(!entry(b, QStringLiteral("chatgpt")).value(QStringLiteral("enabled")).toBool());
        f.up = false;
        b.refresh();
        QVERIFY(!b.fcitxRunning());
        QCOMPARE(b.error(), QStringLiteral("fcitx"));
        const int before = f.writes;
        b.setAppEnabled(QStringLiteral("chatgpt"), true);
        QCOMPARE(f.writes, before); // never writes a config it could not read
    }
    void capAt64()
    {
        FakeFcitx f;
        QStringList many;
        for (int i = 0; i < kMaxApps; ++i) many.append(QStringLiteral("app%1").arg(i));
        f.stored = fcitxMap(many, true);
        FakeWindows w;
        SettingsBackend b(&f, &w);
        QVERIFY(!b.addApp(QStringLiteral("one-more")));
        QCOMPARE(b.error(), QStringLiteral("full"));
    }
};

QTEST_GUILESS_MAIN(SettingsTests)
#include "settings_tests.moc"
