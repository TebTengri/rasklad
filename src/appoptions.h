#pragma once

// Command-line options of the rasklad window (SYS-239).
//
// Accepted: --help/-h, --version, --language en|ru (fixed UI language, for
// smoke tests), --local-profile <project-root> (developer flag that enables
// the verified local experiment profile). There is deliberately no option
// that carries user text. Unknown, duplicate, empty or option-like values
// are errors. Pure: no I/O.

#include <QString>
#include <QStringList>

namespace rasklad::app {

inline constexpr char kAppVersion[] = "0.1.0-dev";

struct AppOptions {
    bool help = false;
    bool version = false;
    bool selfCheck = false;
    // Empty: follow the desktop language.
    QString language;
    // Empty: manual conversion only.
    QString localProfileRoot;
};

struct AppOptionsResult {
    bool ok = false;
    // Fixed English category text without the offending value.
    QString error;
    AppOptions options;
};

// args[0] is the program name.
[[nodiscard]] AppOptionsResult parseAppOptions(const QStringList &args);

[[nodiscard]] QString usageText();
[[nodiscard]] QString versionText();

} // namespace rasklad::app
