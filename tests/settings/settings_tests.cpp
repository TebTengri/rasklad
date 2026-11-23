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

// Behaves like the addon's SetConfig: partial, so keys not written stay as stored.
class FakeFcitx final : public FcitxSource
{
public:
    bool up = true;
    bool failWrites = false;
    bool dropSound = false; // accepts the call but keeps Sound on
    bool otherClientMutesSound = false; // another client switches Sound off during a write
    bool broken = false;    // returns a config without a readable list
    int writes = 0;
    QVariantMap stored = fcitxMap({QStringLiteral("slack"), QStringLiteral("chromium")}, true);
    QStringList group{QStringLiteral("keyboard-us"), QStringLiteral("keyboard-ru")};
    bool running() override { return up; }
    bool addonEnabled() override { return up; }
    QStringList groupMethods() override { return group; }
    std::optional<QVariantMap> readConfig() override
    {
        if (!up) return std::nullopt;
        if (broken) return QVariantMap{{QStringLiteral("Sound"), QStringLiteral("True")}};
        return stored;
    }
    bool writeConfig(const QVariantMap &config) override
    {
        ++writes;
        if (!up || failWrites) return false;
        for (auto it = config.cbegin(); it != config.cend(); ++it) stored.insert(it.key(), it.value());
        if (dropSound) stored.insert(QStringLiteral("Sound"), QStringLiteral("True"));
        if (otherClientMutesSound) stored.insert(QStringLiteral("Sound"), QStringLiteral("False"));
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
        const AddonConfig c = *configFromMap({{QStringLiteral("Applications"), apps},
                                              {QStringLiteral("Sound"), QStringLiteral("False")}});
        QCOMPARE(c.apps, (QStringList{QStringLiteral("slack"), QStringLiteral("chromium")}));
        QVERIFY(!c.sound);
        const QVariantMap back = mapFromConfig(c);
        QCOMPARE(back.value(QStringLiteral("Sound")).toString(), QStringLiteral("False"));
        QCOMPARE(back.value(QStringLiteral("Applications")).toMap().value(QStringLiteral("1")).toString(),
                 QStringLiteral("chromium"));
        // Fcitx sends an empty list as ""; a missing or odd-shaped list is a failed read.
        const auto empty = configFromMap({{QStringLiteral("Applications"), QString()}});
        QVERIFY(empty && empty->apps.isEmpty() && empty->sound);
        QVERIFY(!configFromMap({}));
        QVERIFY(!configFromMap({{QStringLiteral("Applications"), QStringLiteral("slack")}}));
        QVERIFY(!configFromMap({{QStringLiteral("Applications"), QVariant()}}));
        QVERIFY(isTerminal(QStringLiteral("org.gnome.Ptyxis")));
        QVERIFY(isTerminal(QStringLiteral("xfce4-terminal")));
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
        QVERIFY(configFromMap(f.stored)->apps.contains(QStringLiteral("chatgpt")));
        QVERIFY(entry(b, QStringLiteral("chatgpt")).value(QStringLiteral("enabled")).toBool());
        b.setAppEnabled(QStringLiteral("chromium"), false);
        QVERIFY(!configFromMap(f.stored)->apps.contains(QStringLiteral("chromium")));
        QVERIFY(!entry(b, QStringLiteral("chromium")).isEmpty()); // still listed to switch back on
        w.open.clear();
        b.refresh();
        QVERIFY(!entry(b, QStringLiteral("chromium")).isEmpty());
        b.setSound(false);
        QVERIFY(!configFromMap(f.stored)->sound);
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
        QVERIFY(configFromMap(f.stored)->apps.contains(QStringLiteral("org.telegram.desktop")));
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
    void unreadableListNeverOverwritten()
    {
        FakeFcitx f;
        f.broken = true;
        FakeWindows w;
        SettingsBackend b(&f, &w);
        QVERIFY(!b.configLoaded());
        b.setSound(false);
        b.setAppEnabled(QStringLiteral("chatgpt"), true);
        QCOMPARE(f.writes, 0);
    }
    void readBackMismatchIsReported()
    {
        FakeFcitx f;
        f.dropSound = true;
        FakeWindows w;
        SettingsBackend b(&f, &w);
        b.setSound(false);
        QCOMPARE(b.error(), QStringLiteral("write"));
        QVERIFY(b.sound()); // shows what Fcitx kept
    }
    void exceptionRules()
    {
        QCOMPARE(normalizeException(QStringLiteral("Привет")), QStringLiteral("привет"));
        // Stored words are checked as written, exactly as the addon reads them.
        QVERIFY(normalizeException(QStringLiteral(" rfr ")).isEmpty());
        QVERIFY(normalizeException(QString(QChar(0x212A))).isEmpty()); // Kelvin sign, lowercases to k
        QCOMPARE(normalizeException(QStringLiteral("OK")), QStringLiteral("ok"));
        QCOMPARE(normalizeException(QStringLiteral("Ёлка")), QStringLiteral("ёлка"));
        QVERIFY(normalizeException(QStringLiteral("two words")).isEmpty());
        QVERIFY(normalizeException(QStringLiteral("okей")).isEmpty());
        QVERIFY(normalizeException(QStringLiteral("ok,")).isEmpty());
        QVERIFY(normalizeException(QString()).isEmpty());
        QCOMPARE(exceptionKey(QStringLiteral("щл")), exceptionKey(QStringLiteral("ok")));
        // An older addon (no AutoCorrect key) never gets the new keys written.
        const AddonConfig old = *configFromMap(fcitxMap({QStringLiteral("slack")}, true));
        QVERIFY(!old.wordOptions);
        QVERIFY(!mapFromConfig(old).contains(QStringLiteral("Exceptions")));
        // Words are normalised, invalid ones and same-key duplicates dropped.
        QVariantMap words{{QStringLiteral("0"), QStringLiteral("Привет")},
                          {QStringLiteral("1"), QStringLiteral("bad word")},
                          {QStringLiteral("2"), QStringLiteral("ok")},
                          {QStringLiteral("3"), QStringLiteral("щл")}};
        QVariantMap m = fcitxMap({}, true);
        m.insert(QStringLiteral("AutoCorrect"), QStringLiteral("False"));
        m.insert(QStringLiteral("Exceptions"), words);
        const AddonConfig c = *configFromMap(m);
        QVERIFY(c.wordOptions && !c.autoCorrect);
        QCOMPARE(c.exceptions, (QStringList{QStringLiteral("привет"), QStringLiteral("ok")}));
        const QVariantMap back = mapFromConfig(c);
        QCOMPARE(back.value(QStringLiteral("AutoCorrect")).toString(), QStringLiteral("False"));
        QCOMPARE(back.value(QStringLiteral("Exceptions")).toMap().size(), 2);
        // Empty list as "", missing list as empty; another shape is a failed read.
        m.insert(QStringLiteral("Exceptions"), QString());
        QVERIFY(configFromMap(m)->exceptions.isEmpty());
        m.remove(QStringLiteral("Exceptions"));
        QVERIFY(configFromMap(m)->exceptions.isEmpty());
        m.insert(QStringLiteral("Exceptions"), QStringLiteral("ok"));
        QVERIFY(!configFromMap(m));
    }
    void exceptionsAndAutoCorrect()
    {
        FakeFcitx f;
        f.stored.insert(QStringLiteral("AutoCorrect"), QStringLiteral("True"));
        FakeWindows w;
        SettingsBackend b(&f, &w);
        QVERIFY(b.wordOptions() && b.autoCorrect());
        QVERIFY(b.addException(QStringLiteral(" Привет "))); // typed input is trimmed
        QVERIFY(b.addException(QStringLiteral("rfr")));
        QCOMPARE(b.exceptions(), (QStringList{QStringLiteral("rfr"), QStringLiteral("привет")}));
        QCOMPARE(configFromMap(f.stored)->exceptions, b.exceptions());
        const int before = f.writes;
        QVERIFY(b.addException(QStringLiteral("GHBDTN"))); // same keys as "привет": nothing to add
        QCOMPARE(f.writes, before);
        QVERIFY(!b.addException(QStringLiteral("two words")));
        QCOMPARE(b.error(), QStringLiteral("word"));
        QCOMPARE(f.writes, before);
        b.removeException(QStringLiteral("rfr"));
        QCOMPARE(configFromMap(f.stored)->exceptions, (QStringList{QStringLiteral("привет")}));
        b.setAutoCorrect(false);
        QVERIFY(!configFromMap(f.stored)->autoCorrect);
        QVERIFY(!b.autoCorrect());
        QCOMPARE(b.error(), QString());
    }
    void olderAddonNeverGetsWordOptions()
    {
        FakeFcitx f; // stored has no AutoCorrect: an addon before 0.5
        FakeWindows w;
        SettingsBackend b(&f, &w);
        QVERIFY(b.configLoaded() && !b.wordOptions());
        const int before = f.writes;
        QVERIFY(!b.addException(QStringLiteral("ok")));
        b.setAutoCorrect(false);
        QCOMPARE(f.writes, before);
        b.setSound(false);
        QVERIFY(!f.stored.contains(QStringLiteral("AutoCorrect")));
    }
    void changesStartFromTheAddonsCurrentConfig()
    {
        FakeFcitx f;
        f.stored.insert(QStringLiteral("AutoCorrect"), QStringLiteral("True"));
        FakeWindows w;
        SettingsBackend b(&f, &w);
        // Another client switches automatic correction off while the window is open.
        f.stored.insert(QStringLiteral("AutoCorrect"), QStringLiteral("False"));
        b.setSound(false);
        QCOMPARE(b.error(), QString());
        QVERIFY(!configFromMap(f.stored)->autoCorrect);
        QVERIFY(!b.autoCorrect());
        // Fcitx restarts with an older addon: no new key is ever written.
        f.stored = fcitxMap({QStringLiteral("slack")}, false);
        b.setSound(true);
        QVERIFY(!f.stored.contains(QStringLiteral("AutoCorrect")));
        QVERIFY(!f.stored.contains(QStringLiteral("Exceptions")));
        QVERIFY(!b.wordOptions());
        const int before = f.writes;
        QVERIFY(!b.addException(QStringLiteral("ok")));
        b.setAutoCorrect(true);
        QCOMPARE(f.writes, before);
    }
    void writesOnlyWhatChanged()
    {
        FakeFcitx f;
        // 257 distinct words written by another tool: the window shows 256.
        QVariantMap words;
        for (int i = 0; i < kMaxExceptions + 1; ++i) {
            QString w;
            for (int n = i; ; n /= 26) { w.prepend(QChar(u'a' + n % 26)); if (n < 26) break; }
            words.insert(QString::number(i), QStringLiteral("x") + w);
        }
        f.stored.insert(QStringLiteral("AutoCorrect"), QStringLiteral("True"));
        f.stored.insert(QStringLiteral("Exceptions"), words);
        FakeWindows w;
        SettingsBackend b(&f, &w);
        QCOMPARE(b.exceptions().size(), kMaxExceptions);
        b.setSound(false);
        QCOMPARE(b.error(), QString());
        QCOMPARE(f.stored.value(QStringLiteral("Exceptions")).toMap().size(), kMaxExceptions + 1);
        // Remove by key: the chip says "ok", another tool stored "щл".
        f.stored.insert(QStringLiteral("Exceptions"), QVariantMap{{QStringLiteral("0"), QStringLiteral("щл")}});
        b.removeException(QStringLiteral("ok"));
        QCOMPARE(b.error(), QString());
        QVERIFY(configFromMap(f.stored)->exceptions.isEmpty());
    }
    void otherClientsChangeIsNotAnError()
    {
        FakeFcitx f;
        f.stored.insert(QStringLiteral("AutoCorrect"), QStringLiteral("True"));
        FakeWindows w;
        SettingsBackend b(&f, &w);
        f.otherClientMutesSound = true;
        QVERIFY(b.addException(QStringLiteral("ok")));
        QCOMPARE(b.error(), QString());
        QVERIFY(!b.sound()); // shows the other client's change
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
