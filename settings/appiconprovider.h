// SPDX-License-Identifier: MIT
// SYS-254: application icons for the settings list, as "image://appicon/<Icon=>".
// The value comes from a .desktop entry: an icon theme name, or an absolute path
// to an image under a system or user icon directory. Anything else gives no image,
// and the list shows a letter instead. Reads image files only; nothing is written.
#pragma once

#include <QDir>
#include <QIcon>
#include <QPixmap>
#include <QQuickImageProvider>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QUrl>

#include <algorithm>

namespace rasklad::settings {

class AppIconProvider final : public QQuickImageProvider
{
public:
    AppIconProvider() : QQuickImageProvider(QQuickImageProvider::Pixmap) {}

    QPixmap requestPixmap(const QString &id, QSize *size, const QSize &requested) override
    {
        const int side = requested.isValid() ? std::clamp(std::max(requested.width(), requested.height()), 16, 128) : 32;
        if (size) *size = QSize(side, side);
        const QIcon icon = lookup(QUrl::fromPercentEncoding(id.toUtf8()));
        if (icon.isNull()) return {};
        return icon.pixmap(side, side);
    }

private:
    static QIcon lookup(const QString &value)
    {
        static const QRegularExpression name(QStringLiteral("^[A-Za-z0-9._+-]{1,128}$"));
        if (name.match(value).hasMatch()) return QIcon::fromTheme(value);
        if (!QDir::isAbsolutePath(value)) return {};
        const QString clean = QDir::cleanPath(value);
        QStringList roots{QStringLiteral("/usr/share/icons/"), QStringLiteral("/usr/share/pixmaps/")};
        for (const QString &d : QStandardPaths::standardLocations(QStandardPaths::GenericDataLocation))
            roots << QDir::cleanPath(d) + QStringLiteral("/icons/");
        for (const QString &root : roots)
            if (clean.startsWith(root)) return QIcon(clean);
        return {};
    }
};

} // namespace rasklad::settings
