// SPDX-License-Identifier: MIT
// SYS-227: settings app logic against fake Fcitx and app sources (SYS-253).
#include "settingsbackend.h"
#include "licenses.h"
#include "settingscore.h"
#include "seenprograms.h"

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
    std::optional<bool> ready = true; // the addon's status ProfileReady
    int writes = 0;
    QVariantMap stored = fcitxMap({QStringLiteral("slack"), QStringLiteral("chromium")}, true);
    QStringList group{QStringLiteral("keyboard-us"), QStringLiteral("keyboard-ru")};
    bool running() override { return up; }
    bool addonEnabled() override { return up; }
    QStringList groupMethods() override { return group; }
    std::optional<bool> profileReady() override { return up ? ready : std::nullopt; }
    int reads = 0;
    int otherEditAtRead = 0; // on this read, another tool has just changed the store
    QVariantMap otherEdit;
    std::optional<QVariantMap> readConfig() override
    {
        if (!up) return std::nullopt;
        if (++reads == otherEditAtRead)
            for (auto it = otherEdit.cbegin(); it != otherEdit.cend(); ++it) stored.insert(it.key(), it.value());
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

class FakeApps final : public AppSource
{
public:
    QStringList open{QStringLiteral("foot"), QStringLiteral("chatgpt"), QStringLiteral("slack")};
    QStringList seenApps() override { return open; }
};

class FakeSetup final : public SetupTool
{
public:
    std::optional<QByteArray> out = QByteArray("ok       package\nmissing  double-alt\nok       bar-widget\n");
    QStringList opened;
    void requestCheck() override
    {
        if (updated) updated(); // answers at once
    }
    std::optional<QByteArray> lastCheck() const override { return out; }
    bool installed() const override { return true; }
    bool open(const QString &action) override
    {
        opened.append(action);
        return true;
    }
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
    void licensesFixedNamesBoundedRegularFiles()
    {
        QCOMPARE(licenseFileName(QStringLiteral("rasklad")), QStringLiteral("LICENSE"));
        QCOMPARE(licenseFileName(QStringLiteral("en")), QStringLiteral("en_US.README.txt"));
        QCOMPARE(licenseFileName(QStringLiteral("ru")), QStringLiteral("ru_RU.README.txt"));
        QCOMPARE(licenseFileName(QStringLiteral("data")), QStringLiteral("DATA-NOTICE.txt"));
        QCOMPARE(licenseFileName(QStringLiteral("ukacd")), QStringLiteral("UKACD-readme.txt"));
        QVERIFY(licenseFileName(QStringLiteral("../LICENSE")).isEmpty());
        QVERIFY(licenseFileName(QStringLiteral("LICENSE")).isEmpty());
        QVERIFY(licenseFileName(QString()).isEmpty());

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const auto put = [&](const QString &name, const QByteArray &bytes) {
            QFile f(dir.filePath(name));
            QVERIFY(f.open(QIODevice::WriteOnly));
            QCOMPARE(f.write(bytes), qint64(bytes.size()));
        };
        put(QStringLiteral("LICENSE"), "MIT License\n");
        QCOMPARE(readLicense(dir.path(), QStringLiteral("rasklad")), QStringLiteral("MIT License\n"));
        put(QStringLiteral("ru_RU.README.txt"), QString::fromUtf8("Лебедев").toUtf8());
        QCOMPARE(readLicense(dir.path(), QStringLiteral("ru")), QString::fromUtf8("Лебедев"));
        QVERIFY(readLicense(dir.path(), QStringLiteral("en")).isEmpty()); // missing
        QVERIFY(readLicense(dir.path(), QStringLiteral("other")).isEmpty()); // unknown key
        put(QStringLiteral("UKACD-readme.txt"), "a\r\nb\fc\r\n");
        QCOMPARE(readLicense(dir.path(), QStringLiteral("ukacd")), QStringLiteral("a\nb\nc\n")); // shown without CR/FF
        put(QStringLiteral("DATA-NOTICE.txt"), QByteArray(kMaxLicenseBytes + 1, 'x'));
        QVERIFY(readLicense(dir.path(), QStringLiteral("data")).isEmpty()); // too large
        QVERIFY(QFile::link(dir.filePath(QStringLiteral("LICENSE")), dir.filePath(QStringLiteral("en_US.README.txt"))));
        QVERIFY(readLicense(dir.path(), QStringLiteral("en")).isEmpty()); // symlink
        QVERIFY(QDir(dir.path()).mkdir(QStringLiteral("sub")));
        QVERIFY(readLicense(dir.filePath(QStringLiteral("sub")), QStringLiteral("rasklad")).isEmpty());
    }

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
    void seenAppsParse()
    {
        // SYS-253: a Fcitx list option, keyed "0", "1", …; names exactly as reported.
        const QVariantMap names{{QStringLiteral("0"), QStringLiteral("Telegram")},
                                {QStringLiteral("1"), QStringLiteral("org.gnome.Nautilus")},
                                {QStringLiteral("2"), QStringLiteral("Telegram")},
                                {QStringLiteral("3"), QStringLiteral("a b")},
                                {QStringLiteral("4"), QString()},
                                {QStringLiteral("5"), QStringLiteral("rasklad-settings")},
                                {QStringLiteral("6"), QStringLiteral("omawrite")}};
        QCOMPARE(seenAppsFromStatus({{QStringLiteral("SeenApplications"), names},
                                     {QStringLiteral("ProfileReady"), QStringLiteral("True")}}),
                 (QStringList{QStringLiteral("Telegram"), QStringLiteral("omawrite"),
                              QStringLiteral("org.gnome.Nautilus")}));
        // Empty list, older addon without the key, other shapes: nothing on offer.
        QVERIFY(seenAppsFromStatus({{QStringLiteral("SeenApplications"), QString()}}).isEmpty());
        QVERIFY(seenAppsFromStatus({{QStringLiteral("ProfileReady"), QStringLiteral("True")}}).isEmpty());
        QVERIFY(seenAppsFromStatus({{QStringLiteral("SeenApplications"), QStringLiteral("slack")}}).isEmpty());
        QVariantMap many;
        for (int i = 0; i < 70; ++i) many.insert(QString::number(i), QStringLiteral("app%1").arg(i, 2, 10, QLatin1Char('0')));
        QCOMPARE(seenAppsFromStatus({{QStringLiteral("SeenApplications"), many}}).size(), kMaxApps);
    }
    void addonRemembersProgramNames()
    {
        // The addon side (src/seenprograms.h), the same filter in memory.
        std::vector<std::string> seen;
        QVERIFY(rasklad::rememberProgram(seen, "Telegram"));
        QVERIFY(!rasklad::rememberProgram(seen, "Telegram"));
        QVERIFY(!rasklad::rememberProgram(seen, ""));
        QVERIFY(!rasklad::rememberProgram(seen, "foot"));
        QVERIFY(!rasklad::rememberProgram(seen, "Alacritty"));
        QVERIFY(!rasklad::rememberProgram(seen, "a b"));
        QVERIFY(!rasklad::rememberProgram(seen, std::string(129, 'a')));
        QVERIFY(!rasklad::rememberProgram(seen, "rasklad-settings"));
        QCOMPARE(seen.size(), std::size_t{1});
        // Full: the least recently focused name goes; one focused again stays.
        for (int i = 0; seen.size() < rasklad::kMaxSeenPrograms; ++i)
            QVERIFY(rasklad::rememberProgram(seen, "app" + std::to_string(i)));
        QVERIFY(rasklad::rememberProgram(seen, "Telegram")); // moves to the end
        QCOMPARE(seen.back(), std::string("Telegram"));
        QVERIFY(rasklad::rememberProgram(seen, "late"));
        QCOMPARE(seen.size(), rasklad::kMaxSeenPrograms);
        QCOMPARE(seen.back(), std::string("late"));
        QVERIFY(std::find(seen.begin(), seen.end(), std::string("Telegram")) != seen.end());
        QVERIFY(std::find(seen.begin(), seen.end(), std::string("app0")) == seen.end());
    }
    void layouts()
    {
        QVERIFY(layoutsReady({QStringLiteral("keyboard-us"), QStringLiteral("keyboard-ru")}));
        QVERIFY(!layoutsReady({QStringLiteral("keyboard-us")}));
    }
    void listGroupsRowsAndHidesTerminals()
    {
        FakeFcitx f;
        FakeApps w;
        w.open << QStringLiteral("quickshell") << QStringLiteral("chromium-browser");
        SettingsBackend b(&f, &w);
        QVERIFY(b.fcitxRunning() && b.addonEnabled() && b.configLoaded() && b.layoutsReady());
        QCOMPARE(b.enabledCount(), 2);
        const QVariantMap slack = entry(b, QStringLiteral("slack"));
        QVERIFY(slack.value(QStringLiteral("enabled")).toBool());
        QCOMPARE(slack.value(QStringLiteral("group")).toString(), QStringLiteral("on"));
        QCOMPARE(slack.value(QStringLiteral("name")).toString(), QStringLiteral("slack")); // no index: the id
        QCOMPARE(entry(b, QStringLiteral("chatgpt")).value(QStringLiteral("group")).toString(), QStringLiteral("recent"));
        // Terminals and system components are never offered; aliases share one row.
        QVERIFY(entry(b, QStringLiteral("foot")).isEmpty());
        QVERIFY(entry(b, QStringLiteral("quickshell")).isEmpty());
        QVERIFY(entry(b, QStringLiteral("chromium-browser")).isEmpty());
        const QVariantMap chromium = entry(b, QStringLiteral("chromium"));
        QCOMPARE(chromium.value(QStringLiteral("names")).toStringList(),
                 QStringList({QStringLiteral("chromium"), QStringLiteral("chromium-browser")}));
        QVERIFY(chromium.value(QStringLiteral("partial")).toBool()); // only "chromium" is stored
        QCOMPARE(b.apps().constFirst().toMap().value(QStringLiteral("group")).toString(), QStringLiteral("on"));
    }
    void aliasesSwitchTogetherAndRowsStay()
    {
        FakeFcitx f;
        FakeApps w;
        SettingsBackend b(&f, &w);
        b.setAppEnabled(QStringLiteral("chromium"), true); // completes the partial row
        QVERIFY(configFromMap(f.stored)->apps.contains(QStringLiteral("chromium-browser")));
        QVERIFY(!entry(b, QStringLiteral("chromium")).value(QStringLiteral("partial")).toBool());
        b.setAppEnabled(QStringLiteral("chromium-browser"), false);
        const QStringList stored = configFromMap(f.stored)->apps;
        QVERIFY(!stored.contains(QStringLiteral("chromium")) && !stored.contains(QStringLiteral("chromium-browser")));
        // Switching does not move a row until the next refresh.
        QCOMPARE(entry(b, QStringLiteral("chromium")).value(QStringLiteral("group")).toString(), QStringLiteral("on"));
        b.setAppEnabled(QStringLiteral("chatgpt"), true);
        QCOMPARE(entry(b, QStringLiteral("chatgpt")).value(QStringLiteral("group")).toString(), QStringLiteral("recent"));
        b.refresh();
        QCOMPARE(entry(b, QStringLiteral("chatgpt")).value(QStringLiteral("group")).toString(), QStringLiteral("on"));
        QCOMPARE(entry(b, QStringLiteral("chromium")).value(QStringLiteral("group")).toString(), QStringLiteral("recent"));
    }
    void enabledSystemComponentStaysListed()
    {
        FakeFcitx f;
        f.stored = fcitxMap({QStringLiteral("quickshell")}, true);
        FakeApps w;
        SettingsBackend b(&f, &w);
        QVERIFY(entry(b, QStringLiteral("quickshell")).value(QStringLiteral("enabled")).toBool());
        b.setAppEnabled(QStringLiteral("quickshell"), false);
        QVERIFY(configFromMap(f.stored)->apps.isEmpty());
        b.refresh();
        QVERIFY(entry(b, QStringLiteral("quickshell")).isEmpty()); // off: hidden again
    }
    void setupCheckParse()
    {
        const QVariantMap m = setupFromCheck("ok       package\non       auto-correction\n"
                                             "other    double-alt         something else is there\n"
                                             "unknown  hyprland-layout    Hyprland not running\n"
                                             "ok       made-up-step\nweird    bar-widget\n\n");
        QCOMPARE(m.value(QStringLiteral("package")).toString(), QStringLiteral("ok"));
        QCOMPARE(m.value(QStringLiteral("double-alt")).toString(), QStringLiteral("other"));
        QCOMPARE(m.value(QStringLiteral("hyprland-layout")).toString(), QStringLiteral("unknown"));
        QVERIFY(!m.contains(QStringLiteral("made-up-step")));
        QVERIFY(!m.contains(QStringLiteral("bar-widget"))); // unknown state word
        QVERIFY(setupFromCheck(QByteArray(5000, 'x')).isEmpty());
    }
    void setupRowStates()
    {
        const auto row = [](const QVariantMap &m, QStringList steps) { return setupRowState(m, steps); };
        const QVariantMap m{{QStringLiteral("a"), QStringLiteral("ok")},       {QStringLiteral("b"), QStringLiteral("unknown")},
                            {QStringLiteral("c"), QStringLiteral("missing-file")}, {QStringLiteral("d"), QStringLiteral("symlink")},
                            {QStringLiteral("e"), QStringLiteral("no-slot")},  {QStringLiteral("f"), QStringLiteral("no-fcitx")},
                            {QStringLiteral("g"), QStringLiteral("unparsable")}};
        QCOMPARE(row(m, {QStringLiteral("a")}), QStringLiteral("ok"));
        QCOMPARE(row(m, {QStringLiteral("a"), QStringLiteral("b")}), QStringLiteral("unknown")); // never "ok" with an unknown
        QCOMPARE(row(m, {QStringLiteral("a"), QStringLiteral("zz")}), QStringLiteral("unknown")); // absent step
        QCOMPARE(row(m, {QStringLiteral("a"), QStringLiteral("c")}), QStringLiteral("missing"));
        QCOMPARE(row(m, {QStringLiteral("a"), QStringLiteral("d")}), QStringLiteral("other"));
        QCOMPARE(row(m, {QStringLiteral("c"), QStringLiteral("e")}), QStringLiteral("problem"));
        QCOMPARE(row(m, {QStringLiteral("f")}), QStringLiteral("unknown"));
        QCOMPARE(row(m, {QStringLiteral("g")}), QStringLiteral("problem"));
        QCOMPARE(setupFromCheck("unparsable bar-widget\n").value(QStringLiteral("bar-widget")).toString(),
                 QStringLiteral("unparsable"));
        // SYS-268: the layout step through the real parser. Keeping Hyprland's
        // layouts is a choice, other layouts are left as is, an old block is a fault.
        const QStringList layout{QStringLiteral("hyprland-layout")};
        QCOMPARE(row(setupFromCheck("optional hyprland-layout Hyprland keeps its layouts\n"), layout),
                 QStringLiteral("ok"));
        QCOMPARE(row(setupFromCheck("other-layouts hyprland-layout layouts other than us and ru\n"), layout),
                 QStringLiteral("other"));
        QCOMPARE(row(setupFromCheck("legacy hyprland-layout an older block\n"), layout), QStringLiteral("problem"));
        QCOMPARE(row(setupFromCheck("edited hyprland-layout the layout block was edited\n"), layout),
                 QStringLiteral("other"));
    }
    void setupShownAndOpened()
    {
        FakeFcitx f;
        FakeApps w;
        FakeSetup t;
        SettingsBackend b(&f, &w, nullptr, &t);
        QCOMPARE(b.setup().value(QStringLiteral("double-alt")).toString(), QStringLiteral("missing"));
        QCOMPARE(b.setupRows().value(QStringLiteral("alt")).toString(), QStringLiteral("missing"));
        QCOMPARE(b.setupRows().value(QStringLiteral("icon")).toString(), QStringLiteral("unknown")); // link step absent
        QVERIFY(b.setupInstalled());
        // The check's fcitx-group answer moves the layouts banner without a Fcitx call.
        f.group = {QStringLiteral("keyboard-us")};
        t.out.reset();
        b.refresh();
        QVERIFY(!b.layoutsReady());
        t.out = QByteArray("ok fcitx-group\n");
        t.updated();
        QVERIFY(b.layoutsReady());
        t.out = QByteArray("missing fcitx-group\n");
        t.updated();
        QVERIFY(!b.layoutsReady());
        f.group = {QStringLiteral("keyboard-us"), QStringLiteral("keyboard-ru")};
        t.out = QByteArray("ok package\nmissing double-alt\nok bar-widget\n");
        b.refresh();
        QVERIFY(b.openSetup(QStringLiteral("install")));
        QCOMPARE(t.opened, QStringList{QStringLiteral("install")});
        t.out.reset(); // no tool: the section hides
        b.refresh();
        QVERIFY(b.setup().isEmpty());
        SettingsBackend none(&f, &w);
        QVERIFY(none.setup().isEmpty());
        QVERIFY(!none.openSetup(QStringLiteral("install")));
    }
    void roundingReply()
    {
        QCOMPARE(roundingFromReply(R"({"option":"decoration:rounding","int":8,"set":true})"), 8);
        QCOMPARE(roundingFromReply(R"({"int":400})"), 24);
        QCOMPARE(roundingFromReply(R"({"int":-3})"), 0);
        QCOMPARE(roundingFromReply("no"), 0);
        QCOMPARE(roundingFromReply(R"({"str":"8"})"), 0);
    }
    void toggleWritesAndReadsBack()
    {
        FakeFcitx f;
        FakeApps w;
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
        FakeApps w;
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
        FakeApps w;
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
        FakeApps w;
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
        FakeApps w;
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
        FakeApps w;
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
    void onOffSwitch()
    {
        FakeFcitx f;
        f.stored.insert(QStringLiteral("AutoCorrect"), QStringLiteral("True"));
        f.stored.insert(QStringLiteral("Enabled"), QStringLiteral("False"));
        FakeApps w;
        SettingsBackend b(&f, &w);
        QVERIFY(b.switchable() && !b.raskladEnabled());
        b.setRaskladEnabled(true);
        QCOMPARE(f.stored.value(QStringLiteral("Enabled")).toString(), QStringLiteral("True"));
        QVERIFY(b.raskladEnabled());
        b.setAutoCorrect(false); // other changes keep the switch
        QCOMPARE(f.stored.value(QStringLiteral("Enabled")).toString(), QStringLiteral("True"));
        b.setRaskladEnabled(false);
        QCOMPARE(f.stored.value(QStringLiteral("Enabled")).toString(), QStringLiteral("False"));
        // An addon without the switch is always on and never gets the key.
        FakeFcitx old;
        SettingsBackend o(&old, &w);
        QVERIFY(!o.switchable() && o.raskladEnabled());
        const int before = old.writes;
        o.setRaskladEnabled(false);
        QCOMPARE(old.writes, before);
        o.setSound(false);
        QVERIFY(!old.stored.contains(QStringLiteral("Enabled")));
    }
    void olderAddonNeverGetsWordOptions()
    {
        FakeFcitx f; // stored has no AutoCorrect: an addon before 0.5
        FakeApps w;
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
        FakeApps w;
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
        FakeApps w;
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
    void editOfAListChangedMeanwhileIsNotWritten()
    {
        // SYS-272: another tool edits the list between the read the edit starts from
        // and the write: nothing is written, the window shows the other tool's list.
        FakeFcitx f;
        f.stored.insert(QStringLiteral("AutoCorrect"), QStringLiteral("True"));
        FakeApps w;
        SettingsBackend b(&f, &w);
        f.otherEditAtRead = f.reads + 2; // the add's fresh read, then the one right before writing
        f.otherEdit = {{QStringLiteral("Exceptions"), QVariantMap{{QStringLiteral("0"), QStringLiteral("rfr")}}}};
        const int before = f.writes;
        QVERIFY(!b.addException(QStringLiteral("ok")));
        QCOMPARE(b.error(), QStringLiteral("changed"));
        QCOMPARE(f.writes, before);
        QCOMPARE(b.exceptions(), (QStringList{QStringLiteral("rfr")}));
        // Repeated, the edit applies on top of the other tool's list.
        QVERIFY(b.addException(QStringLiteral("ok")));
        QCOMPARE(configFromMap(f.stored)->exceptions, (QStringList{QStringLiteral("ok"), QStringLiteral("rfr")}));
        // A change to a key this edit does not write is no reason to refuse.
        f.otherEditAtRead = f.reads + 2;
        f.otherEdit = {{QStringLiteral("Sound"), QStringLiteral("False")}};
        QVERIFY(b.addException(QStringLiteral("hello")));
        QCOMPARE(b.error(), QString());
    }

    void entryBeyondTheCapCountsAsAChange()
    {
        // 256 words shown; another tool appends a 257th while the user removes one.
        // The capped view would not see it; the raw comparison does.
        FakeFcitx f;
        f.stored.insert(QStringLiteral("AutoCorrect"), QStringLiteral("True"));
        QVariantMap words;
        for (int i = 0; i < 256; ++i) words.insert(QString::number(i), QStringLiteral("w%1").arg(QChar(u'a' + i % 26)) + QString(i / 26 + 1, u'x'));
        f.stored.insert(QStringLiteral("Exceptions"), words);
        FakeApps w;
        SettingsBackend b(&f, &w);
        QVariantMap more = words;
        more.insert(QStringLiteral("256"), QStringLiteral("extra"));
        f.otherEditAtRead = f.reads + 2;
        f.otherEdit = {{QStringLiteral("Exceptions"), more}};
        const int before = f.writes;
        b.removeException(b.exceptions().first());
        QCOMPARE(b.error(), QStringLiteral("changed"));
        QCOMPARE(f.writes, before);
    }

    void otherClientsChangeIsNotAnError()
    {
        FakeFcitx f;
        f.stored.insert(QStringLiteral("AutoCorrect"), QStringLiteral("True"));
        FakeApps w;
        SettingsBackend b(&f, &w);
        f.otherClientMutesSound = true;
        QVERIFY(b.addException(QStringLiteral("ok")));
        QCOMPARE(b.error(), QString());
        QVERIFY(!b.sound()); // shows the other client's change
    }
    void profileStatusShownOnlyWhenDefinite()
    {
        FakeFcitx f;
        FakeApps w;
        SettingsBackend b(&f, &w);
        QVERIFY(!b.profileBroken());
        f.ready = false;
        b.refresh();
        QVERIFY(b.profileBroken());
        f.ready = true; // a change re-reads it first: a restarted Fcitx clears the warning
        const int writes = f.writes;
        b.setSound(b.sound()); // no change, so no write: only the pre-change read ran
        QCOMPARE(f.writes, writes);
        QVERIFY(!b.profileBroken());
        f.ready = false;
        b.refresh();
        QVERIFY(b.profileBroken());
        f.ready = std::nullopt; // an older addon without the status: not broken
        b.refresh();
        QVERIFY(!b.profileBroken());
        f.ready = false;
        f.up = false; // Fcitx down: unknown, the Fcitx error is shown instead
        b.refresh();
        QVERIFY(!b.profileBroken());
    }
    void capAt64()
    {
        FakeFcitx f;
        QStringList many;
        for (int i = 0; i < kMaxApps; ++i) many.append(QStringLiteral("app%1").arg(i));
        f.stored = fcitxMap(many, true);
        FakeApps w;
        SettingsBackend b(&f, &w);
        QVERIFY(!b.addApp(QStringLiteral("one-more")));
        QCOMPARE(b.error(), QStringLiteral("full"));
    }
};

QTEST_GUILESS_MAIN(SettingsTests)
#include "settings_tests.moc"
