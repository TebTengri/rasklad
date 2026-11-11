#include "benchmarkio.h"

#include "detectormanifest.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

#include <utility>

namespace rasklad::benchmark {
namespace {

bool isSafeFileName(const QString &name)
{
    if (name.isEmpty() || name.size() > 64 || name == QLatin1String(".") || name == QLatin1String(".."))
        return false;
    for (const QChar ch : name) {
        const char16_t c = ch.unicode();
        const bool ok = (c >= u'a' && c <= u'z') || (c >= u'A' && c <= u'Z') || (c >= u'0' && c <= u'9')
            || c == u'.' || c == u'_' || c == u'-';
        if (!ok)
            return false;
    }
    return true;
}

} // namespace

bool readBoundedFile(const QString &path, qint64 limit, QByteArray &out, QString &error)
{
    out.clear();
    const QFileInfo info(path);
    if (!info.exists() || !info.isFile()) {
        error = QStringLiteral("not a regular file");
        return false;
    }
    if (info.size() > limit) {
        error = QStringLiteral("file exceeds size limit");
        return false;
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        error = QStringLiteral("cannot open file");
        return false;
    }
    QByteArray bytes = file.read(limit + 1);
    if (file.error() != QFileDevice::NoError) {
        error = QStringLiteral("read error");
        return false;
    }
    if (bytes.size() > limit) {
        error = QStringLiteral("file exceeds size limit");
        return false;
    }
    if (qint64(bytes.size()) != info.size()) {
        error = QStringLiteral("file size changed while reading");
        return false;
    }
    out = std::move(bytes);
    return true;
}

bool verifyNoticeFile(const QString &manifestPath, const QString &relativeNotice,
                      const QString &expectedSha256, QString &error)
{
    const QString resolved = experiment::resolveNoticePath(manifestPath, relativeNotice);
    if (resolved.isEmpty()) {
        error = QStringLiteral("notice path is not inside the repository");
        return false;
    }
    const QString rootCanonical = QFileInfo(experiment::manifestRoot(manifestPath)).canonicalFilePath();
    const QString noticeCanonical = QFileInfo(resolved).canonicalFilePath();
    if (rootCanonical.isEmpty() || noticeCanonical.isEmpty()
        || !isStrictlyInside(noticeCanonical, rootCanonical)) {
        error = QStringLiteral("notice file is missing or outside the repository");
        return false;
    }
    QByteArray bytes;
    if (!readBoundedFile(noticeCanonical, experiment::kMaxNoticeBytes, bytes, error)) {
        error = QStringLiteral("notice: ") + error;
        return false;
    }
    if (experiment::sha256Hex(bytes) != expectedSha256) {
        error = QStringLiteral("notice SHA-256 does not match manifest");
        return false;
    }
    return true;
}

bool isStrictlyInside(const QString &canonicalChild, const QString &canonicalParent)
{
    if (canonicalChild.isEmpty() || canonicalParent.isEmpty())
        return false;
    const QString prefix = canonicalParent.endsWith(QLatin1Char('/')) ? canonicalParent
                                                                      : canonicalParent + QLatin1Char('/');
    return canonicalChild.size() > prefix.size() && canonicalChild.startsWith(prefix);
}

QString resolveArtifactDir(const QString &projectRoot, const QString &requested, QString &error)
{
    const QString rootCanonical = QFileInfo(projectRoot).canonicalFilePath();
    if (rootCanonical.isEmpty()) {
        error = QStringLiteral("project root is not available");
        return {};
    }
    const QFileInfo buildInfo(rootCanonical + QStringLiteral("/build"));
    if (buildInfo.isSymLink() || !buildInfo.isDir()) {
        error = QStringLiteral("project build directory is missing or a symbolic link");
        return {};
    }
    const QString buildCanonical = buildInfo.canonicalFilePath();
    if (buildCanonical != rootCanonical + QStringLiteral("/build")) {
        error = QStringLiteral("project build directory resolves outside the project");
        return {};
    }
    const QFileInfo requestedInfo(requested);
    if (!requestedInfo.exists() || !requestedInfo.isDir()) {
        error = QStringLiteral("artifact directory does not exist");
        return {};
    }
    const QString requestedCanonical = requestedInfo.canonicalFilePath();
    if (!isStrictlyInside(requestedCanonical, buildCanonical)) {
        error = QStringLiteral("artifact directory is not strictly inside the project build directory");
        return {};
    }
    return requestedCanonical;
}

bool writeArtifactFile(const QString &artifactDir, const QString &fileName, const QByteArray &bytes,
                       QString &error)
{
    if (!isSafeFileName(fileName)) {
        error = QStringLiteral("unsafe artifact file name");
        return false;
    }
    const QFileInfo dirInfo(artifactDir);
    if (!dirInfo.isDir() || dirInfo.canonicalFilePath() != artifactDir) {
        error = QStringLiteral("artifact directory changed or is not canonical");
        return false;
    }
    const QString target = artifactDir + QLatin1Char('/') + fileName;
    const QFileInfo targetInfo(target);
    if (targetInfo.isSymLink() || (targetInfo.exists() && !targetInfo.isFile())) {
        error = QStringLiteral("artifact target exists and is not a regular file");
        return false;
    }
    QSaveFile file(target);
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly)) {
        error = QStringLiteral("cannot open artifact for writing");
        return false;
    }
    if (file.write(bytes) != bytes.size()) {
        file.cancelWriting();
        error = QStringLiteral("cannot write artifact");
        return false;
    }
    if (!file.commit()) {
        error = QStringLiteral("cannot commit artifact");
        return false;
    }
    return true;
}

} // namespace rasklad::benchmark
