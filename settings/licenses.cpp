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
    return QString::fromUtf8(data);
}

} // namespace rasklad::settings
