// SPDX-License-Identifier: MIT
#include "licenses.h"

#include <QFile>
#include <QFileInfo>

namespace rasklad::settings {

QString licenseFileName(const QString &key)
{
    if (key == QLatin1String("rasklad")) return QStringLiteral("LICENSE");
    if (key == QLatin1String("en")) return QStringLiteral("en_US.README.txt");
    if (key == QLatin1String("ru")) return QStringLiteral("ru_RU.README.txt");
    if (key == QLatin1String("data")) return QStringLiteral("DATA-NOTICE.txt");
    if (key == QLatin1String("ukacd")) return QStringLiteral("UKACD-readme.txt");
    return {};
}

QString readLicense(const QString &dir, const QString &key)
{
    const QString name = licenseFileName(key);
    if (name.isEmpty()) return {};
    const QString path = dir + QLatin1Char('/') + name;
    const QFileInfo fi(path);
    if (fi.isSymLink() || !fi.isFile() || fi.size() > kMaxLicenseBytes) return {};
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return {};
    const QByteArray data = f.read(kMaxLicenseBytes + 1);
    if (data.size() > kMaxLicenseBytes) return {};
    // For display only: the UKACD document has CRLF line ends and form feeds.
    QString text = QString::fromUtf8(data);
    text.replace(QLatin1String("\r\n"), QLatin1String("\n"));
    text.replace(QLatin1Char('\f'), QLatin1Char('\n'));
    return text;
}

} // namespace rasklad::settings
