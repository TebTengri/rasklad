#include "appoptions.h"

namespace rasklad::app {

AppOptionsResult parseAppOptions(const QStringList &args)
{
    AppOptionsResult result;
    auto failWith = [&result](const char *message) {
        result.ok = false;
        result.error = QString::fromLatin1(message);
        result.options = AppOptions();
        return result;
    };
    bool seenLanguage = false;
    bool seenProfile = false;
    for (qsizetype i = 1; i < args.size(); ++i) {
        const QString &arg = args.at(i);
        if (arg == QLatin1String("--help") || arg == QLatin1String("-h")) {
            result.options.help = true;
            continue;
        }
        if (arg == QLatin1String("--version")) {
            result.options.version = true;
            continue;
        }
        if (arg == QLatin1String("--self-check")) {
            result.options.selfCheck = true;
            continue;
        }
        const bool language = arg == QLatin1String("--language");
        const bool profile = arg == QLatin1String("--local-profile");
        if (!language && !profile)
            return failWith("unknown argument");
        bool &seen = language ? seenLanguage : seenProfile;
        if (seen)
            return failWith("duplicate option");
        seen = true;
        if (i + 1 >= args.size())
            return failWith("option without value");
        const QString value = args.at(++i);
        if (value.isEmpty() || value.startsWith(QLatin1Char('-')))
            return failWith("empty or option-like value");
        if (language) {
            if (value != QLatin1String("en") && value != QLatin1String("ru"))
                return failWith("--language must be en or ru");
            result.options.language = value;
        } else {
            if (value.size() > 4096 || value.contains(QChar(u'\0')))
                return failWith("--local-profile path is not acceptable");
            result.options.localProfileRoot = value;
        }
    }
    result.ok = true;
    return result;
}

QString usageText()
{
    return QStringLiteral(
        "Usage: rasklad [--language en|ru] [--local-profile <project-root>]\n"
        "       rasklad --help | --version\n"
        "\n"
        "Manual EN/RU keyboard-layout preview in the app's own window.\n"
        "Type or paste text, choose the direction, press Preview and then Apply\n"
        "or Copy. No background clipboard or other-window reading occurs, nothing\n"
        "is saved, and no global input hook is installed.\n"
        "\n"
        "  --language en|ru         Fixed UI language (default: follow the desktop).\n"
        "  --local-profile <root>   Developer option: enable the optional word check\n"
        "                           from verified local experiment data under\n"
        "                           <root>/data and <root>/build. Not enabled by default.\n"
        "  -h, --help               Show this help.\n"
        "  --version                Show the version.\n"
        "  --self-check             JSON platform privacy check, no window or text.\n");
}

QString versionText()
{
    return QStringLiteral("rasklad ") + QString::fromLatin1(kAppVersion) + QLatin1Char('\n');
}

} // namespace rasklad::app
