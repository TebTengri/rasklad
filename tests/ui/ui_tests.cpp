// SYS-239: offscreen smoke test of the Qt Quick window. Fictional text only.
// No real desktop capture: QT_QPA_PLATFORM is forced to offscreen and the
// software scene graph renders into memory; screenshots go to the ignored
// build directory. The clipboard is never read.

#include "analysisservice.h"
#include "soundfeedback.h"
#include "clipboardwriter.h"
#include "documentcontroller.h"
#include "themewatcher.h"
#include "uisetup.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QQuickTextDocument>
#include <QTextDocument>
#include <QTemporaryDir>
#include <QTest>
#include <QSignalSpy>
#include <QUrl>

#include <memory>

using namespace rasklad;
using namespace rasklad::app;

// Stands in for EditorGuard when the native history cannot be disabled.
class FakeFailingGuard final : public QObject
{
    Q_OBJECT
public:
    Q_INVOKABLE bool configure(QObject *)
    {
        ++calls;
        return false;
    }
    int calls = 0;
};

namespace {

QStringList g_messages;
QtMessageHandler g_previousHandler = nullptr;

void captureMessages(QtMsgType type, const QMessageLogContext &context, const QString &message)
{
    g_messages.append(message);
    if (g_previousHandler)
        g_previousHandler(type, context, message);
}

class InstantProvider final : public routing::EvidenceSource
{
public:
    [[nodiscard]] std::optional<routing::TokenEvidence> evidence(const routing::EvidenceQuery &) const override
    {
        routing::TokenEvidence e;
        e.full.alternative = true;
        e.model = DecisionAction::Correct;
        return e;
    }
};

bool writeFile(const QString &path, const QByteArray &bytes)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    return file.open(QIODevice::WriteOnly | QIODevice::Truncate) && file.write(bytes) == bytes.size();
}

struct Ui {
    QTemporaryDir dir;
    AnalysisService service;
    std::unique_ptr<DocumentController> controller;
    std::unique_ptr<ThemeWatcher> theme;
    ClipboardWriter clipboard;
    SoundFeedback soundFeedback; // No audio player attached in tests.
    std::unique_ptr<QQmlApplicationEngine> engine;
    std::unique_ptr<LanguageSwitcher> languages;
    QQuickWindow *window = nullptr;

    // language: written to a temporary locale.conf (followed live).
    bool load(const char *language, bool withProvider, QObject *guardOverride = nullptr)
    {
        if (!dir.isValid())
            return false;
        ThemeSources sources;
        sources.themeParent = dir.path();
        sources.themeDirectory = dir.path() + QStringLiteral("/theme");
        sources.localeFiles = {dir.path() + QStringLiteral("/locale.conf")};
        writeFile(sources.themeDirectory + QStringLiteral("/colors.toml"),
                  QByteArray("accent = \"#7aa2f7\"\nbackground = \"#1a1b26\"\nforeground = \"#c0caf5\"\n"));
        setLanguage(language);
        if (withProvider)
            service.start([](profile::ProfileError &) -> std::unique_ptr<routing::EvidenceSource> {
                return std::make_unique<InstantProvider>();
            });
        controller = std::make_unique<DocumentController>(&service);
        theme = std::make_unique<ThemeWatcher>(sources, QString(), false);
        engine = std::make_unique<QQmlApplicationEngine>();
        languages = std::make_unique<LanguageSwitcher>(engine.get(), theme.get());
        QVariantMap properties = mainWindowProperties(controller.get(), theme.get(), &clipboard, withProvider);
        properties.insert(QStringLiteral("soundFeedback"), QVariant::fromValue(&soundFeedback));
        if (guardOverride)
            properties.insert(QStringLiteral("editorGuard"), QVariant::fromValue(guardOverride));
        engine->setInitialProperties(properties);
        engine->load(QUrl(mainQmlUrl()));
        if (engine->rootObjects().isEmpty())
            return false;
        window = qobject_cast<QQuickWindow *>(engine->rootObjects().first());
        return window != nullptr;
    }

    void setLanguage(const char *language)
    {
        writeFile(dir.path() + QStringLiteral("/locale.conf"),
                  QByteArray("LANG=") + QByteArray(language) + QByteArray("_XX.UTF-8\n"));
    }

    template<typename T = QObject>
    T *find(const char *name) const
    {
        const QString objectName = QString::fromLatin1(name);
        if (T *found = window->findChild<T *>(objectName))
            return found;
        return window->contentItem()->findChild<T *>(objectName);
    }

    ~Ui()
    {
        languages.reset();
        engine.reset();
        controller.reset();
        service.shutdown();
    }
};

void click(QQuickWindow *window, QQuickItem *item)
{
    QVERIFY(item);
    QVERIFY(item->isVisible());
    QCoreApplication::processEvents();
    QTest::qWait(60);
    const QPointF center = item->mapToScene(QPointF(item->width() / 2, item->height() / 2));
    QVERIFY(center.x() >= 0 && center.x() < window->width());
    QVERIFY(center.y() >= 0 && center.y() < window->height());
    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, center.toPoint());
}

void screenshot(QQuickWindow *window, const char *name)
{
    const QString directory = QStringLiteral(RASKLAD_SCREENSHOT_DIR);
    QVERIFY2(directory.contains(QStringLiteral("/build/")), "screenshots must stay inside build/");
    QVERIFY(QDir().mkpath(directory));
    QCoreApplication::processEvents();
    QTest::qWait(80);
    const QImage image = window->grabWindow();
    QVERIFY(!image.isNull());
    QVERIFY(image.width() > 0 && image.height() > 0);
    QVERIFY(image.save(directory + QLatin1Char('/') + QString::fromLatin1(name)));
}

// US-layout keys of "Привет мир тест" (no punctuation: ',' is the б key).
const char kFictional[] = "Ghbdtn vbh ntcn";

} // namespace

class UiTests : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void soundDialogUsesFakeOutputOnly()
    {
        Ui ui;
        QVERIFY(ui.load("en", false));
        QSignalSpy requests(&ui.soundFeedback, &SoundFeedback::requested);
        click(ui.window, ui.find<QQuickItem>("soundButton"));
        QTRY_VERIFY(ui.find("soundDialog")->property("visible").toBool());
        click(ui.window, ui.find<QQuickItem>("previewSoundButton"));
        QCOMPARE(requests.count(), 1);
        click(ui.window, ui.find<QQuickItem>("soundEnabled"));
        QVERIFY(!ui.soundFeedback.enabled());
        QVERIFY(!ui.find("previewSoundButton")->property("enabled").toBool());
        screenshot(ui.window, "en-sound.png");
        ui.setLanguage("ru");
        ui.theme->reload();
        QTRY_COMPARE(ui.find("soundDialog")->property("title").toString(), QString::fromUtf8("Звук"));
        screenshot(ui.window, "ru-sound.png");
    }

    void initTestCase()
    {
        const auto blocked = clipboardInterfaceBlacklist(QByteArray("custom_protocol"));
        QVERIFY(blocked.startsWith("custom_protocol,"));
        QVERIFY(blocked.split(',').contains("zwp_primary_selection_device_manager_v1"));
        QVERIFY(blocked.split(',').contains("zwlr_data_control_manager_v1"));
        QVERIFY(blocked.split(',').contains("ext_data_control_manager_v1"));
        g_previousHandler = qInstallMessageHandler(captureMessages);
    }

    void cleanupTestCase()
    {
        qInstallMessageHandler(g_previousHandler);
        for (const QString &message : std::as_const(g_messages)) {
            // No user text in any log line, and no QML script errors.
            QVERIFY(!message.contains(QStringLiteral("Ghbdtn")));
            QVERIFY(!message.contains(QStringLiteral("ghbdtn")));
            QVERIFY(!message.contains(QString::fromUtf8("Привет")));
            QVERIFY2(!message.contains(QStringLiteral("ReferenceError")), message.toUtf8().constData());
            QVERIFY2(!message.contains(QStringLiteral("TypeError")), message.toUtf8().constData());
            QVERIFY2(!message.contains(QStringLiteral("qrc:/")), message.toUtf8().constData());
        }
    }

    void englishWindowFlow()
    {
        Ui ui;
        QVERIFY(ui.load("en", true));
        QVERIFY(QTest::qWaitForWindowExposed(ui.window));
        QTRY_COMPARE(ui.controller->analysisState(), QStringLiteral("ready"));
        QVERIFY(ui.window->width() >= ui.window->minimumWidth());

        QObject *editor = ui.find("sourceEditor");
        QObject *previewArea = ui.find("previewArea");
        QVERIFY(editor);
        QVERIFY(previewArea);
        QCOMPARE(editor->property("textFormat").toInt(), 0 /* TextEdit.PlainText */);
        QCOMPARE(previewArea->property("textFormat").toInt(), 0);

        // Typing goes to the controller.
        QVERIFY(editor->setProperty("text", QString::fromLatin1(kFictional)));
        QTRY_COMPARE(ui.controller->sourceText(), QString::fromLatin1(kFictional));
        screenshot(ui.window, "en-empty-preview.png");

        click(ui.window, ui.find<QQuickItem>("previewButton"));
        QTRY_COMPARE(ui.controller->previewKind(), QStringLiteral("manual"));
        QCOMPARE(previewArea->property("text").toString(), ui.controller->previewText());
        QCOMPARE(ui.controller->sourceText(), QString::fromLatin1(kFictional));
        screenshot(ui.window, "en-preview.png");

        click(ui.window, ui.find<QQuickItem>("applyButton"));
        QTRY_COMPARE(ui.controller->previewKind(), QStringLiteral("none"));
        QCOMPARE(ui.controller->sourceText(), QString::fromUtf8("Привет мир тест"));
        QTRY_COMPARE(editor->property("text").toString(), ui.controller->sourceText());
        QVERIFY(ui.controller->canUndo());

        click(ui.window, ui.find<QQuickItem>("undoButton"));
        QTRY_COMPARE(ui.controller->sourceText(), QString::fromLatin1(kFictional));
        QTRY_COMPARE(editor->property("text").toString(), QString::fromLatin1(kFictional));

        // Oversized edit: rejected visibly, editor reset, nothing truncated.
        QVERIFY(editor->setProperty("text", QString(kMaxInputUtf16 + 1, QLatin1Char('x'))));
        QTRY_COMPARE(editor->property("text").toString(), QString::fromLatin1(kFictional));
        QCOMPARE(ui.controller->sourceText(), QString::fromLatin1(kFictional));
        QCOMPARE(ui.controller->notice(), QStringLiteral("inputTooLong"));
        QObject *notice = ui.find("noticeBar");
        QVERIFY(notice);
        QTRY_VERIFY(notice->property("visible").toBool());
        screenshot(ui.window, "en-rejected.png");

        // Explicit word check at the cursor; the result is only a preview.
        QVERIFY(editor->setProperty("text", QStringLiteral("ghbdtn")));
        QVERIFY(editor->setProperty("cursorPosition", 2));
        click(ui.window, ui.find<QQuickItem>("checkButton"));
        QTRY_COMPARE(ui.controller->checkAction(), QStringLiteral("correct"));
        QCOMPARE(ui.controller->previewKind(), QStringLiteral("check"));
        QCOMPARE(ui.controller->sourceText(), QStringLiteral("ghbdtn"));
        QObject *card = ui.find("checkCard");
        QVERIFY(card);
        QTRY_VERIFY(card->property("visible").toBool());
        screenshot(ui.window, "en-check.png");

        click(ui.window, ui.find<QQuickItem>("copyButton"));
        QTRY_COMPARE(ui.controller->notice(), QStringLiteral("copiedPreview"));

        click(ui.window, ui.find<QQuickItem>("clearButton"));
        QTRY_VERIFY(ui.find("discardDialog")->property("visible").toBool());
        click(ui.window, ui.find<QQuickItem>("discardButton"));
        QTRY_VERIFY(ui.controller->sourceText().isEmpty());
        QTRY_VERIFY(editor->property("text").toString().isEmpty());

        // Narrow layout at the minimum size.
        ui.window->resize(ui.window->minimumWidth(), ui.window->minimumHeight());
        QVERIFY(editor->setProperty("text", QString::fromLatin1(kFictional)));
        click(ui.window, ui.find<QQuickItem>("previewButton"));
        QTRY_COMPARE(ui.controller->previewKind(), QStringLiteral("manual"));
        screenshot(ui.window, "en-narrow.png");
    }

    void keyboardAndControlledUndo()
    {
        Ui ui;
        QVERIFY(ui.load("en", false));
        QVERIFY(QTest::qWaitForWindowExposed(ui.window));
        auto *editor = ui.find<QQuickItem>("sourceEditor");
        QVERIFY(editor);
        editor->forceActiveFocus();
        QTest::keyClick(ui.window, 'Q');
        QTest::keyClick(ui.window, '?');
        QTest::keyClick(ui.window, Qt::Key_Space);
        QTRY_COMPARE(ui.controller->sourceText(), QStringLiteral("Q? "));
        auto *document = qobject_cast<QQuickTextDocument *>(editor->property("textDocument").value<QObject *>());
        QVERIFY(document);
        QVERIFY(!document->textDocument()->isUndoRedoEnabled());
        QVERIFY(!document->textDocument()->isUndoAvailable());
        QVERIFY(ui.window->property("editorReady").toBool());
        QVERIFY(!editor->property("readOnly").toBool());
        // Single-unit typing has no controller history either.
        QVERIFY(!ui.controller->canUndo());

        // Select-all overwrite of a long text with one letter, then Ctrl+Z.
        const QString longText = QString::fromLatin1(kFictional).repeated(8);
        QVERIFY(ui.controller->setSourceText(longText));
        QTRY_COMPARE(editor->property("text").toString(), longText);
        editor->forceActiveFocus();
        QTest::keyClick(ui.window, Qt::Key_A, Qt::ControlModifier);
        QTRY_COMPARE(editor->property("selectedText").toString().size(), longText.size());
        QTest::keyClick(ui.window, 'x');
        QTRY_COMPARE(ui.controller->sourceText(), QStringLiteral("x"));
        QVERIFY(ui.controller->canUndo());
        QTest::keyClick(ui.window, Qt::Key_Z, Qt::ControlModifier);
        QTRY_COMPARE(ui.controller->sourceText(), longText);
        QTRY_COMPARE(editor->property("text").toString(), longText);
        QCOMPARE(ui.controller->notice(), QStringLiteral("undoneEdit"));
        // One level: a second Ctrl+Z changes nothing.
        QTest::keyClick(ui.window, Qt::Key_Z, Qt::ControlModifier);
        QCOMPARE(ui.controller->sourceText(), longText);
        QCOMPARE(ui.controller->notice(), QStringLiteral("undoUnavailable"));
        QVERIFY(!document->textDocument()->isUndoAvailable());

        QVERIFY(ui.controller->setSourceText(QStringLiteral("ghbdtn")));
        QTRY_COMPARE(editor->property("text").toString(), QStringLiteral("ghbdtn"));
        QTest::keyClick(ui.window, Qt::Key_Return, Qt::ControlModifier);
        QTRY_VERIFY(ui.controller->canApply());
        QCOMPARE(ui.controller->sourceText(), QStringLiteral("ghbdtn"));
        // Extra modifiers never Apply.
        QTest::keyClick(ui.window, Qt::Key_Return, Qt::ControlModifier | Qt::AltModifier | Qt::ShiftModifier);
        QCoreApplication::processEvents();
        QVERIFY(ui.controller->sourceText().startsWith(QStringLiteral("ghbdtn")));
        QVERIFY(ui.controller->notice() != QStringLiteral("applied"));
        QVERIFY(ui.controller->setSourceText(QStringLiteral("ghbdtn")));
        QTest::keyClick(ui.window, Qt::Key_Return, Qt::ControlModifier);
        QTRY_VERIFY(ui.controller->canApply());
        QTest::keyClick(ui.window, Qt::Key_Return, Qt::ControlModifier | Qt::ShiftModifier);
        QTRY_COMPARE(ui.controller->sourceText(), QString::fromUtf8("привет"));
        QTest::keyClick(ui.window, Qt::Key_Z, Qt::ControlModifier);
        QTRY_COMPARE(ui.controller->sourceText(), QStringLiteral("ghbdtn"));
        QCOMPARE(ui.controller->notice(), QStringLiteral("undone"));
        QTest::keyClick(ui.window, Qt::Key_F1);
        QTRY_VERIFY(ui.find("helpDialog")->property("visible").toBool());
        QTest::keyClick(ui.window, Qt::Key_Escape);
        QTRY_VERIFY(!ui.find("helpDialog")->property("visible").toBool());
        QTest::keyClick(ui.window, Qt::Key_Q, Qt::ControlModifier);
        QTRY_VERIFY(ui.find("discardDialog")->property("visible").toBool());
        click(ui.window, ui.find<QQuickItem>("cancelDiscardButton"));
        QVERIFY(ui.window->isVisible());
        QCOMPARE(ui.controller->sourceText(), QStringLiteral("ghbdtn"));
        QTest::keyClick(ui.window, Qt::Key_Q, Qt::ControlModifier);
        QTRY_VERIFY(ui.find("discardDialog")->property("visible").toBool());
        click(ui.window, ui.find<QQuickItem>("discardButton"));
        QTRY_VERIFY(!ui.window->isVisible());
        QVERIFY(ui.controller->sourceText().isEmpty());
        QVERIFY(!document->textDocument()->isUndoAvailable());
    }

    void editorGuardRejectsInvalidWrappers()
    {
        EditorGuard guard;
        QVERIFY(!guard.configure(nullptr));
        QObject plain;
        QVERIFY(!guard.configure(&plain));
    }

    void editorGuardFailureBlocksTyping()
    {
        FakeFailingGuard guard;
        Ui ui;
        QVERIFY(ui.load("en", false, &guard));
        QVERIFY(QTest::qWaitForWindowExposed(ui.window));
        // Both editors were attempted; either failure fails closed.
        QCOMPARE(guard.calls, 2);
        QVERIFY(!ui.window->property("editorReady").toBool());
        auto *editor = ui.find<QQuickItem>("sourceEditor");
        QVERIFY(editor);
        QVERIFY(editor->property("readOnly").toBool());
        QObject *error = ui.find("editorError");
        QVERIFY(error);
        QTRY_VERIFY(error->property("visible").toBool());
        editor->forceActiveFocus();
        QTest::keyClick(ui.window, 'Q');
        QTest::keyClick(ui.window, 'w');
        QTest::keyClick(ui.window, Qt::Key_Space);
        QCoreApplication::processEvents();
        QVERIFY(ui.controller->sourceText().isEmpty());
        QVERIFY(editor->property("text").toString().isEmpty());
        screenshot(ui.window, "en-editor-blocked.png");
    }

    void narrowViewportScrolls()
    {
        Ui ui;
        QVERIFY(ui.load("en", false));
        QVERIFY(QTest::qWaitForWindowExposed(ui.window));
        ui.window->resize(560, 480);
        QTRY_COMPARE(ui.window->width(), 560);
        QTRY_COMPARE(ui.window->height(), 480);
        QObject *editor = ui.find("sourceEditor");
        QVERIFY(editor);
        QVERIFY(editor->setProperty("text", QString::fromLatin1(kFictional)));
        QTRY_COMPARE(ui.controller->sourceText(), QString::fromLatin1(kFictional));

        auto *scroll = ui.find<QQuickItem>("documentScroll");
        QVERIFY(scroll);
        auto *flick = scroll; // The outer Flickable exposes the scroll extent directly.
        QVERIFY(flick->property("contentHeight").isValid());
        // Stacked panes do not fit 480 px: the outer view scrolls instead of clipping.
        QTRY_VERIFY(flick->property("contentHeight").toReal() > flick->height());

        // Primary actions are reachable at the top.
        click(ui.window, ui.find<QQuickItem>("previewButton"));
        QTRY_COMPARE(ui.controller->previewKind(), QStringLiteral("manual"));
        click(ui.window, ui.find<QQuickItem>("applyButton"));
        QTRY_COMPARE(ui.controller->sourceText(), QString::fromUtf8("Привет мир тест"));
        screenshot(ui.window, "en-narrow-480.png");

        // The bottom of the content is reachable by scrolling.
        const qreal bottom = flick->property("contentHeight").toReal() - flick->height();
        QVERIFY(flick->setProperty("contentY", bottom));
        QCoreApplication::processEvents();
        auto *footer = ui.find<QQuickItem>("footerNote");
        QVERIFY(footer);
        const QPointF top = footer->mapToScene(QPointF(0, 0));
        const QPointF end = footer->mapToScene(QPointF(0, footer->height()));
        QVERIFY(top.y() >= 0);
        QVERIFY(end.y() <= ui.window->height() + 0.5);
        screenshot(ui.window, "en-narrow-480-bottom.png");

        // Back at the top, Undo still works through its button.
        QVERIFY(flick->setProperty("contentY", 0));
        click(ui.window, ui.find<QQuickItem>("undoButton"));
        QTRY_COMPARE(ui.controller->sourceText(), QString::fromLatin1(kFictional));
    }

    void yellowAccentAndLongText()
    {
        Ui ui;
        QVERIFY(ui.load("ru", false));
        QVERIFY(QTest::qWaitForWindowExposed(ui.window));
        QVERIFY(writeFile(ui.dir.path() + QStringLiteral("/theme/colors.toml"),
                          QByteArray("accent = '#FFD60A'\nbackground = '#1a1b26'\nforeground = '#c0caf5'\n")));
        ui.theme->reload();
        auto *button = ui.find<QQuickItem>("previewButton");
        QVERIFY(button);
        QTRY_COMPARE(button->property("accentInk").value<QColor>(), QColor(QStringLiteral("#000000")));
        QVERIFY(ui.controller->setSourceText(QString::fromLatin1(kFictional).repeated(30)));
        click(ui.window, button);
        QTRY_VERIFY(ui.controller->canApply());
        screenshot(ui.window, "ru-yellow-long.png");
    }

    void russianWindowAndLiveLanguageSwitch()
    {
        Ui ui;
        QVERIFY(ui.load("ru", false));
        QVERIFY(QTest::qWaitForWindowExposed(ui.window));
        QCOMPARE(ui.controller->analysisState(), QStringLiteral("disabled"));
        QVERIFY(ui.languages->russianInstalled());
        QObject *previewButton = ui.find("previewButton");
        QVERIFY(previewButton);
        QTRY_COMPARE(previewButton->property("text").toString(), QString::fromUtf8("Предпросмотр"));
        QObject *editor = ui.find("sourceEditor");
        QVERIFY(editor);
        QVERIFY(editor->setProperty("text", QString::fromLatin1(kFictional)));
        click(ui.window, qobject_cast<QQuickItem *>(previewButton));
        QTRY_COMPARE(ui.controller->previewKind(), QStringLiteral("manual"));
        screenshot(ui.window, "ru-preview.png");
        ui.window->resize(ui.window->minimumWidth(), ui.window->minimumHeight());
        QCOMPARE(ui.window->minimumHeight(), 480);
        screenshot(ui.window, "ru-narrow.png");

        // Check word is disabled without the local profile; manual still works.
        QObject *check = ui.find("checkButton");
        QVERIFY(check);
        QVERIFY(!check->property("enabled").toBool());

        // Desktop language changes to English: the UI follows without restart.
        ui.setLanguage("en");
        ui.theme->reload();
        QVERIFY(!ui.languages->russianInstalled());
        QTRY_COMPARE(previewButton->property("text").toString(), QStringLiteral("Preview"));
        screenshot(ui.window, "en-after-switch.png");
    }
};

int main(int argc, char *argv[])
{
    // Never touch the real desktop: render offscreen with the software backend.
    qputenv("QT_QPA_PLATFORM", "offscreen");
    // The desktop may export gtk3 as the platform theme. Loading that plugin
    // would contact the real desktop even with an offscreen window surface.
    qputenv("QT_QPA_PLATFORMTHEME", "generic");
    qputenv("QT_QUICK_BACKEND", "software");
    prepareQuickEnvironment();
    QGuiApplication app(argc, argv);
    UiTests tests;
    return QTest::qExec(&tests, argc, argv);
}

#include "ui_tests.moc"
