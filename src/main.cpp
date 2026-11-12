// rasklad manual-preview window (SYS-239).
//
// Own Qt Quick window: the user types or pastes text into the app's plain-text
// editor, picks the direction, previews and explicitly applies or copies.
// No global input hook, keystroke replay, background clipboard reading, network, child
// process, application IPC endpoint, settings, history or logging of user text.

#include "analysisservice.h"
#include "appoptions.h"
#include "clipboardwriter.h"
#include "documentcontroller.h"
#include "localprofile.h"
#include "themewatcher.h"
#include "uisetup.h"

#include <QGuiApplication>
#include <QClipboard>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQmlApplicationEngine>
#include <QStringList>
#include <QTextStream>
#include <QUrl>

#include <cstdio>
#include <memory>

using namespace rasklad;
using namespace rasklad::app;

namespace {

constexpr int kExitUsage = 2;
constexpr int kExitUi = 1;

int printAndExit(const QString &text, bool error, int code)
{
    QTextStream stream(error ? stderr : stdout);
    stream << text;
    stream.flush();
    return code;
}

bool wantsInfoOnly(int argc, char *argv[])
{
    for (int i = 1; i < argc; ++i) {
        const QByteArray arg(argv[i]);
        if (arg == "--help" || arg == "-h" || arg == "--version")
            return true;
    }
    return false;
}

} // namespace

int main(int argc, char *argv[])
{
    // --help/--version work without a display: parse before QGuiApplication.
    if (wantsInfoOnly(argc, argv)) {
        QStringList args;
        for (int i = 0; i < argc; ++i)
            args << QString::fromLocal8Bit(argv[i]);
        const AppOptionsResult parsed = parseAppOptions(args);
        if (!parsed.ok)
            return printAndExit(QStringLiteral("rasklad: ") + parsed.error + QLatin1Char('\n') + usageText(), true,
                                kExitUsage);
        if (parsed.options.help)
            return printAndExit(usageText(), false, 0);
        return printAndExit(versionText(), false, 0);
    }

    prepareQuickEnvironment();
    QGuiApplication app(argc, argv);
    QGuiApplication::setApplicationName(QStringLiteral("rasklad"));
    QGuiApplication::setApplicationVersion(QString::fromLatin1(kAppVersion));
    QGuiApplication::setDesktopFileName(QStringLiteral("rasklad"));

    // Qt-specific arguments (-platform …) were consumed by QGuiApplication.
    const AppOptionsResult parsed = parseAppOptions(QCoreApplication::arguments());
    if (!parsed.ok)
        return printAndExit(QStringLiteral("rasklad: ") + parsed.error + QLatin1Char('\n') + usageText(), true,
                            kExitUsage);
    const AppOptions &options = parsed.options;

    // A failed backend restriction must never silently expose selected text.
    const bool selection = QGuiApplication::clipboard()->supportsSelection();
    if (options.selfCheck) {
        const QJsonObject report{{QStringLiteral("platform"), QGuiApplication::platformName()},
                                 {QStringLiteral("primarySelectionSupported"), selection},
                                 {QStringLiteral("windowCreated"), false},
                                 {QStringLiteral("profileLoaded"), false}};
        return printAndExit(QString::fromUtf8(QJsonDocument(report).toJson(QJsonDocument::Compact))
                                + QLatin1Char('\n'), false, selection ? kExitUi : 0);
    }
    if (selection)
        return printAndExit(QStringLiteral("rasklad: automatic primary selection is not disabled on this "
                                           "platform; use the Wayland backend.\n"), true, kExitUi);

    AnalysisService analysis;
    QObject::connect(&app, &QCoreApplication::aboutToQuit, &analysis, &AnalysisService::shutdown);
    if (!options.localProfileRoot.isEmpty()) {
        const QString root = options.localProfileRoot;
        analysis.start([root](profile::ProfileError &error) -> std::unique_ptr<routing::EvidenceSource> {
            return profile::LocalEvidenceProvider::load(root, profile::productionPins(), error);
        });
    }

    DocumentController controller(&analysis);
    ThemeWatcher theme(defaultThemeSources(), options.language, true);
    ClipboardWriter clipboard;

    QQmlApplicationEngine engine;
    LanguageSwitcher languages(&engine, &theme);
    engine.setInitialProperties(
        mainWindowProperties(&controller, &theme, &clipboard, !options.localProfileRoot.isEmpty()));
    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreationFailed, &app, []() { QCoreApplication::exit(kExitUi); },
        Qt::QueuedConnection);
    engine.load(QUrl(mainQmlUrl()));
    if (engine.rootObjects().isEmpty())
        return kExitUi;

    const int code = app.exec();
    analysis.shutdown();
    return code;
}
