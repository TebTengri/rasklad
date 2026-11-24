// SPDX-License-Identifier: MIT
// SYS-256: license texts for the settings window's «Лицензии» page. Only the fixed
// file names below, only from the package's license directory, bounded reads.
#pragma once

#include <QString>

namespace rasklad::settings {

inline constexpr char kLicenseDir[] = "/usr/share/licenses/rasklad-fcitx5";
inline constexpr qint64 kMaxLicenseBytes = 64 * 1024;

// "rasklad", "en", "ru", "data" or "ukacd" -> file name; empty for anything else.
QString licenseFileName(const QString &key);
// The text of that file in dir, or empty if the key is unknown, the file is missing,
// is not a regular file (a symlink counts as not), or is larger than the limit.
QString readLicense(const QString &dir, const QString &key);

} // namespace rasklad::settings
