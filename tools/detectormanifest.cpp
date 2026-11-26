#include "detectormanifest.h"

#include "detectordataset.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QStringList>

#include <cmath>

namespace rasklad::experiment {
namespace {

bool isLowerHex(const QString &text, qsizetype length)
{
    if (text.size() != length)
        return false;
    for (const QChar ch : text) {
        const char16_t c = ch.unicode();
        if (!((c >= u'0' && c <= u'9') || (c >= u'a' && c <= u'f')))
            return false;
    }
    return true;
}

bool isPathChar(char16_t c)
{
    return (c >= u'a' && c <= u'z') || (c >= u'A' && c <= u'Z') || (c >= u'0' && c <= u'9')
        || c == u'.' || c == u'_' || c == u'-' || c == u'/';
}

// Строка без приведения типов: число или null в JSON — отказ.
bool readString(const QJsonObject &object, const char *key, QString &out)
{
    const QJsonValue value = object.value(QLatin1String(key));
    if (!value.isString())
        return false;
    out = value.toString();
    return true;
}

// Целое без дробной части в [minimum, maximum].
bool readInteger(const QJsonObject &object, const char *key, qint64 minimum, qint64 maximum,
                 qint64 &out)
{
    const QJsonValue value = object.value(QLatin1String(key));
    if (!value.isDouble())
        return false;
    const double d = value.toDouble();
    if (!std::isfinite(d) || std::floor(d) != d || d < double(minimum) || d > double(maximum))
        return false;
    out = qint64(d);
    return true;
}

ManifestError parseSource(const QJsonValue &value, ExperimentManifest &manifest,
                          std::array<bool, 2> &seen)
{
    if (!value.isObject())
        return ManifestError::BadSources;
    const QJsonObject object = value.toObject();
    QString language;
    if (!readString(object, "language", language))
        return ManifestError::BadSources;
    std::size_t index = 0;
    if (language == QLatin1String("en"))
        index = std::size_t(DetectorLanguage::En);
    else if (language == QLatin1String("ru"))
        index = std::size_t(DetectorLanguage::Ru);
    else
        return ManifestError::BadSources;
    if (seen[index])
        return ManifestError::BadSources;
    seen[index] = true;

    ManifestSource &source = manifest.sources[index];
    if (!readString(object, "sha256", source.sha256) || !isLowerHex(source.sha256, 64))
        return ManifestError::BadSourceField;
    if (!readString(object, "notice_sha256", source.noticeSha256)
        || !isLowerHex(source.noticeSha256, 64))
        return ManifestError::BadSourceField;
    if (!readInteger(object, "bytes", 1, kMaxDicBytes, source.bytes))
        return ManifestError::BadSourceField;
    if (!readString(object, "notice", source.notice))
        return ManifestError::BadSourceField;
    if (!isSafeNoticePath(source.notice))
        return ManifestError::BadNoticePath;
    return ManifestError::None;
}

} // namespace

QString manifestErrorText(ManifestError error)
{
    switch (error) {
    case ManifestError::None:
        return QStringLiteral("ok");
    case ManifestError::TooLarge:
        return QStringLiteral("manifest exceeds size limit");
    case ManifestError::InvalidJson:
        return QStringLiteral("manifest is not a JSON object");
    case ManifestError::SchemaVersion:
        return QStringLiteral("manifest schema_version is not 1");
    case ManifestError::StatusNotApproved:
        return QStringLiteral("manifest status is not approved-local-experiment");
    case ManifestError::ScopeMismatch:
        return QStringLiteral("manifest scope is not local-experiment-only");
    case ManifestError::ReleaseApproved:
        return QStringLiteral("manifest release_approved must be boolean false");
    case ManifestError::BadCommit:
        return QStringLiteral("manifest commit is not a 40-digit lowercase hex id");
    case ManifestError::BadSources:
        return QStringLiteral("manifest training_sources must be exactly en and ru");
    case ManifestError::BadSourceField:
        return QStringLiteral("manifest source sha256/bytes/notice fields are invalid");
    case ManifestError::BadNoticePath:
        return QStringLiteral("manifest notice path is not a safe relative path");
    }
    return QStringLiteral("unknown manifest error");
}

ManifestParseResult parseExperimentManifest(const QByteArray &bytes)
{
    ManifestParseResult result;
    if (bytes.size() > kMaxManifestBytes) {
        result.error = ManifestError::TooLarge;
        return result;
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(bytes, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        result.error = ManifestError::InvalidJson;
        return result;
    }
    const QJsonObject root = document.object();
    ExperimentManifest &manifest = result.manifest;

    qint64 schema = 0;
    if (!readInteger(root, "schema_version", 1, 1, schema)) {
        result.error = ManifestError::SchemaVersion;
        return result;
    }
    if (!readString(root, "status", manifest.status)
        || manifest.status != QLatin1String(kRequiredStatus)) {
        result.error = ManifestError::StatusNotApproved;
        return result;
    }
    if (!readString(root, "scope", manifest.scope) || manifest.scope != QLatin1String(kRequiredScope)) {
        result.error = ManifestError::ScopeMismatch;
        return result;
    }
    const QJsonValue release = root.value(QLatin1String("release_approved"));
    if (!release.isBool() || release.toBool()) {
        result.error = ManifestError::ReleaseApproved;
        return result;
    }
    if (!readString(root, "commit", manifest.commit) || !isLowerHex(manifest.commit, 40)) {
        result.error = ManifestError::BadCommit;
        return result;
    }

    const QJsonValue sourcesValue = root.value(QLatin1String("training_sources"));
    if (!sourcesValue.isArray()) {
        result.error = ManifestError::BadSources;
        return result;
    }
    const QJsonArray sources = sourcesValue.toArray();
    if (sources.size() != 2) {
        result.error = ManifestError::BadSources;
        return result;
    }
    std::array<bool, 2> seen{{false, false}};
    for (qsizetype i = 0; i < sources.size(); ++i) {
        const ManifestError error = parseSource(sources.at(i), manifest, seen);
        if (error != ManifestError::None) {
            result.error = error;
            return result;
        }
    }
    if (!seen[0] || !seen[1]) {
        result.error = ManifestError::BadSources;
        return result;
    }

    manifest.manifestSha256 = sha256Hex(bytes);
    return result;
}

bool isSafeNoticePath(const QString &relative)
{
    if (relative.isEmpty() || relative.size() > kMaxNoticePathChars)
        return false;
    if (!relative.startsWith(QLatin1String(kNoticePrefix)))
        return false;
    for (const QChar ch : relative) {
        if (!isPathChar(ch.unicode()))
            return false;
    }
    const QStringList segments = relative.split(QLatin1Char('/'));
    for (const QString &segment : segments) {
        if (segment.isEmpty() || segment == QLatin1String(".") || segment == QLatin1String(".."))
            return false;
    }
    return true;
}

QString manifestRoot(const QString &manifestFilePath)
{
    if (manifestFilePath.isEmpty())
        return {};
    const QString manifestDir = QFileInfo(manifestFilePath).absolutePath();
    return QDir::cleanPath(manifestDir + QStringLiteral("/.."));
}

QString resolveNoticePath(const QString &manifestFilePath, const QString &relative)
{
    if (!isSafeNoticePath(relative))
        return {};
    const QString root = manifestRoot(manifestFilePath);
    if (root.isEmpty())
        return {};
    const QString prefix = root.endsWith(QLatin1Char('/')) ? root : root + QLatin1Char('/');
    const QString joined = QDir::cleanPath(prefix + relative);
    if (!joined.startsWith(prefix) || joined.size() <= prefix.size())
        return {};
    return joined;
}

bool bytesMatch(const QByteArray &data, qint64 expectedBytes, const QString &expectedSha256)
{
    return qint64(data.size()) == expectedBytes && isLowerHex(expectedSha256, 64)
        && sha256Hex(data) == expectedSha256;
}

QString sha256Hex(const QByteArray &data)
{
    return QString::fromLatin1(QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex());
}

} // namespace rasklad::experiment
