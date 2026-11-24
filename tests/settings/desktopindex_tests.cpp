// SPDX-License-Identifier: MIT
// SYS-254: the .desktop index against synthetic fixtures.
#include "desktopindex.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

using namespace rasklad::settings;

namespace {

QString fixtures()
{
    return QStringLiteral(FIXTURES_DIR "/applications");
}

bool writeFile(const QString &path, const QByteArray &data)
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) return false;
    return f.write(data) == data.size();
}

} // namespace

class DesktopIndexTests final : public QObject
{
    Q_OBJECT

private:
    DesktopIndex index;

private Q_SLOTS:
    void initTestCase()
    {
        index.load({fixtures()});
        // 12 files; Hidden and Type=Link are left out.
        QCOMPARE(index.size(), 10);
    }

    void wmClassMatch()
    {
        const auto info = index.find(QStringLiteral("Slack"));
        QVERIFY(info);
        QCOMPARE(info->name, QStringLiteral("Slack"));
        QCOMPARE(info->description, QStringLiteral("Team communication"));
        QCOMPARE(info->icon, QStringLiteral("slack"));
        QVERIFY(!info->webApp);
        QVERIFY(index.find(QStringLiteral("slack"))); // case-insensitive
    }

    void reverseDnsMatchAndRussian()
    {
        const auto info = index.find(QStringLiteral("Telegram"));
        QVERIFY(info);
        QCOMPARE(info->name, QStringLiteral("Телеграм")); // not the [Desktop Action] name
        QCOMPARE(info->description, QStringLiteral("Официальный клиент"));
        QCOMPARE(info->icon, QStringLiteral("org.telegram.desktop"));
        QVERIFY(index.find(QStringLiteral("org.telegram.desktop")));
    }

    void englishFallback()
    {
        DesktopIndex en;
        en.load({fixtures()}, QStringLiteral("en"));
        const auto info = en.find(QStringLiteral("telegram"));
        QVERIFY(info);
        QCOMPARE(info->name, QStringLiteral("Telegram Desktop"));
        QCOMPARE(info->description, QStringLiteral("Official desktop client"));
    }

    void desktopIdMatch()
    {
        const auto info = index.find(QStringLiteral("slack-desktop"));
        QVERIFY(info);
        QCOMPARE(info->name, QStringLiteral("Slack"));
    }

    void uniqueExecMatch()
    {
        // env, VAR=value, quotes and field codes skipped; GenericName[ru] beats Comment.
        const auto info = index.find(QStringLiteral("fooedit"));
        QVERIFY(info);
        QCOMPARE(info->name, QStringLiteral("Foo Editor"));
        QCOMPARE(info->description, QStringLiteral("Текстовый редактор"));
        QVERIFY(index.find(QStringLiteral("telegram-desktop")));
    }

    void ambiguousExecGivesNothing()
    {
        QVERIFY(!index.find(QStringLiteral("sharedtool")));
    }

    void webAppEntryDoesNotClaimBrowser()
    {
        QVERIFY(!index.find(QStringLiteral("chromium")));
    }

    void localePreference()
    {
        const auto info = index.find(QStringLiteral("locale-pick"));
        QVERIFY(info);
        QCOMPARE(info->name, QStringLiteral("Выбор языка (Россия)"));
        QCOMPARE(info->description, QStringLiteral("Line one\ntwo\\three")); // unescaped
        QVERIFY(info->icon.isEmpty());

        DesktopIndex de;
        de.load({fixtures()}, QStringLiteral("de_AT"));
        QCOMPARE(de.find(QStringLiteral("locale-pick"))->name, QStringLiteral("Sprachwahl"));
    }

    void descriptionEqualToNameIsEmpty()
    {
        const auto info = index.find(QStringLiteral("notes"));
        QVERIFY(info);
        QCOMPARE(info->name, QStringLiteral("Notes"));
        QVERIFY(info->description.isEmpty());
    }

    void noDisplayPreferredLess()
    {
        const auto info = index.find(QStringLiteral("dupe"));
        QVERIFY(info);
        QCOMPARE(info->name, QStringLiteral("Dupe"));
        // Still found when it is the only hit.
        QCOMPARE(index.find(QStringLiteral("dupe-a-helper"))->name, QStringLiteral("Dupe Helper"));
    }

    void hiddenAndNonApplicationSkipped()
    {
        QVERIFY(!index.find(QStringLiteral("ghost")));
        QVERIFY(!index.find(QStringLiteral("weblink")));
        QVERIFY(!index.find(QString()));
    }

    void userDirOverridesSystemDir()
    {
        QTemporaryDir user;
        QVERIFY(user.isValid());
        QVERIFY(writeFile(user.filePath(QStringLiteral("org.telegram.desktop.desktop")),
                          "[Desktop Entry]\nType=Application\nName=Telegram User\n"));
        // A hidden user entry deletes the system one with the same id.
        QVERIFY(writeFile(user.filePath(QStringLiteral("slack-desktop.desktop")),
                          "[Desktop Entry]\nType=Application\nName=Slack\nHidden=true\n"));
        DesktopIndex both;
        both.load({user.path(), fixtures()});
        QCOMPARE(both.find(QStringLiteral("telegram"))->name, QStringLiteral("Telegram User"));
        QVERIFY(!both.find(QStringLiteral("Slack")));
        QCOMPARE(both.size(), 9);
    }

    void subdirectoryId()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QVERIFY(QDir(dir.path()).mkpath(QStringLiteral("vendor")));
        QVERIFY(writeFile(dir.filePath(QStringLiteral("vendor/tool.desktop")),
                          "[Desktop Entry]\nType=Application\nName=Vendor Tool\n"));
        DesktopIndex sub;
        sub.load({dir.path()});
        QCOMPARE(sub.find(QStringLiteral("vendor-tool"))->name, QStringLiteral("Vendor Tool"));
    }

    void ambiguousSegmentGivesNothing()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QVERIFY(writeFile(dir.filePath(QStringLiteral("com.a.editor.desktop")), "[Desktop Entry]\nType=Application\nName=A\n"));
        QVERIFY(writeFile(dir.filePath(QStringLiteral("org.b.editor.desktop")), "[Desktop Entry]\nType=Application\nName=B\n"));
        QVERIFY(writeFile(dir.filePath(QStringLiteral("org.c.desktop.desktop")), "[Desktop Entry]\nType=Application\nName=C\n"));
        DesktopIndex idx;
        idx.load({dir.path()});
        QVERIFY(!idx.find(QStringLiteral("editor")));
        QVERIFY(!idx.find(QStringLiteral("desktop"))); // a generic segment never names an app
        QCOMPARE(idx.find(QStringLiteral("org.b.editor"))->name, QStringLiteral("B")); // the full id still does
        QCOMPARE(idx.find(QStringLiteral("c"))->name, QStringLiteral("C"));
    }

    void localeVariants()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QVERIFY(writeFile(dir.filePath(QStringLiteral("app.desktop")),
                          "[Desktop Entry]\nType=Application\nName=App\nName[ru]=Прил\n"));
        for (const char *lang : {"ru", "ru_RU", "ru-RU", "ru_RU.UTF-8", "ru_RU.UTF-8@x"}) {
            DesktopIndex idx;
            idx.load({dir.path()}, QString::fromLatin1(lang));
            QCOMPARE(idx.find(QStringLiteral("app"))->name, QStringLiteral("Прил"));
        }
    }

    void webAppWithEntry()
    {
        const auto info = index.find(QStringLiteral("chrome-web.whatsapp.com__-Default"));
        QVERIFY(info);
        QCOMPARE(info->name, QStringLiteral("WhatsApp Web"));
        QVERIFY(info->webApp);
    }

    void webAppWithoutEntry()
    {
        const auto info = index.find(QStringLiteral("chrome-www.example.org__-Default"));
        QVERIFY(info);
        QCOMPARE(info->name, QStringLiteral("example.org"));
        QVERIFY(info->description.isEmpty());
        QVERIFY(info->icon.isEmpty());
        QVERIFY(info->webApp);
        QVERIFY(!index.find(QStringLiteral("chrome-__-Default")));
        QVERIFY(!index.find(QStringLiteral("unknown-app")));
    }

    void oversizeFileSkipped()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QByteArray big = "[Desktop Entry]\nType=Application\nName=Big\n";
        big += QByteArray(DesktopIndex::kMaxFileSize, '#');
        QVERIFY(writeFile(dir.filePath(QStringLiteral("big.desktop")), big));
        QVERIFY(writeFile(dir.filePath(QStringLiteral("small.desktop")),
                          "[Desktop Entry]\nType=Application\nName=Small\n"));
        DesktopIndex capped;
        capped.load({dir.path()});
        QCOMPARE(capped.size(), 1);
        QVERIFY(!capped.find(QStringLiteral("big")));
        QVERIFY(capped.find(QStringLiteral("small")));
    }

    void symlinkLoopIsHarmless()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QVERIFY(QDir(dir.path()).mkpath(QStringLiteral("a")));
        QVERIFY(QFile::link(dir.path(), dir.filePath(QStringLiteral("a/loop"))));
        QVERIFY(writeFile(dir.filePath(QStringLiteral("a/app.desktop")),
                          "[Desktop Entry]\nType=Application\nName=App\n"));
        DesktopIndex looped;
        looped.load({dir.path()});
        QCOMPARE(looped.size(), 1);
    }

    void systemComponents()
    {
        for (const char *id : {"quickshell", "org.quickshell", "Walker", "elephant", "mako", "waybar",
                               "swayosd", "hyprlauncher", "rasklad-settings", "MAKO", "org.omarchy.screensaver"})
            QVERIFY2(isSystemComponent(QLatin1String(id)), id);
        QVERIFY(!isSystemComponent(QStringLiteral("slack")));
        QVERIFY(!isSystemComponent(QString()));
    }

    void canonicalAndAliases()
    {
        QCOMPARE(canonicalApp(QStringLiteral("chromium-browser")), QStringLiteral("chromium"));
        QCOMPARE(canonicalApp(QStringLiteral("Chromium-Browser")), QStringLiteral("chromium"));
        QCOMPARE(canonicalApp(QStringLiteral("chromium")), QStringLiteral("chromium"));
        QCOMPARE(canonicalApp(QStringLiteral("Slack")), QStringLiteral("Slack"));
        QCOMPARE(appAliases(QStringLiteral("chromium")),
                 QStringList({QStringLiteral("chromium"), QStringLiteral("chromium-browser")}));
        QCOMPARE(appAliases(QStringLiteral("Slack")), QStringList{QStringLiteral("Slack")});
    }
};

QTEST_GUILESS_MAIN(DesktopIndexTests)
#include "desktopindex_tests.moc"
