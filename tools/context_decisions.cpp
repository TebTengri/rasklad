// Developer-only CLI опыта 005 (SYS-237): действия шести замороженных
// политик для каждой строки синтетических признаков. Не GUI, не служба, не
// устанавливается и не входит в runtime приложения.
//
//   context_decisions --protocol data/experiment-005-protocol.json
//       --manifest data/evaluation-005-sources.json
//
// Оба аргумента обязательны, каждый ровно один раз; других нет. Порядок:
//   1. Канонические пути; корни репозитория обоих файлов совпадают; это
//      именно data/experiment-005-protocol.json и data/evaluation-005-sources.json
//      без symlink-компонентов.
//   2. Протокол 005: <= 32768 байт, точный SHA-256 (первым), затем поля.
//   3. Паспорт оценки: строгий разбор, protocol_sha256, все notices.
//   4. TSV признаков: путь build/experiment-005-data/features.tsv без
//      symlink-выхода, размер, SHA-256, затем строгая проверка всех строк.
//   5. Только затем — решения для каждой строки.
// Корпус, модель, словари и Hunspell не загружаются; описание labels в
// паспорте допускается, но файлы labels не открываются. Происхождение
// membership и token-действий подтверждает подготовка признаков отдельно.
//
// stdout — только после полного успеха, одной записью: по строке на вход в
// исходном порядке, без заголовка:
//   ordinal<TAB><U|R|-><TAB><6 кодов K/C/A/S><LF>
// в порядке protocol.policies. Слов, префиксов, ID, путей и хешей нет; stderr
// — только категория ошибки. Логирования, сети, процессов и записи файлов нет.
//
// Коды выхода: 0 — успех; 1 — отказ входных данных; 2 — неверные аргументы;
// 3 — нарушена целостность; 4 — ошибка записи stdout.

#include "benchmarkio.h"
#include "contextpolicy.h"
#include "detectormanifest.h"

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QStringList>
#include <QTextStream>

#include <utility>
#include <vector>

using namespace rasklad;
using namespace rasklad::context;
namespace ex = rasklad::experiment;

namespace {

constexpr int kExitInput = 1;
constexpr int kExitUsage = 2;
constexpr int kExitIntegrity = 3;
constexpr int kExitOutput = 4;

int fail(const QString &message, int code)
{
    QTextStream(stderr) << "context_decisions: " << message << '\n';
    return code;
}

int usageError(const QString &message)
{
    QTextStream(stderr) << "usage: context_decisions --protocol <experiment-005-protocol.json>"
                           " --manifest <evaluation-005-sources.json>\n";
    return fail(message, kExitUsage);
}

// Канонический путь файла и канонический корень репозитория (родитель его
// каталога). Корень по исходному аргументу и по каноническому пути совпадает.
bool resolveManifest(const QString &argument, QString &canonical, QString &root)
{
    const QFileInfo info(argument);
    if (!info.exists() || !info.isFile())
        return false;
    canonical = info.canonicalFilePath();
    if (canonical.isEmpty())
        return false;
    root = QFileInfo(ex::manifestRoot(canonical)).canonicalFilePath();
    const QString argumentRoot = QFileInfo(ex::manifestRoot(argument)).canonicalFilePath();
    return !root.isEmpty() && root == argumentRoot && benchmark::isStrictlyInside(canonical, root);
}

// <root>/<relative> — обычный файл; ни один компонент не symlink.
QString resolveExact(const QString &root, const char *relative)
{
    const QString joined = root + QLatin1Char('/') + QString::fromLatin1(relative);
    const QFileInfo info(joined);
    if (!info.exists() || !info.isFile() || info.canonicalFilePath() != joined
        || !benchmark::isStrictlyInside(joined, root))
        return {};
    return joined;
}

// Признаки: <root>/build — настоящий каталог, файл строго внутри build/,
// путь после разрешения symlink равен склеенному.
QString resolveFeatures(const QString &root, const QString &relative)
{
    if (relative != QLatin1String(kFeaturesPath))
        return {};
    const QString buildPath = root + QStringLiteral("/build");
    const QFileInfo buildInfo(buildPath);
    if (buildInfo.isSymLink() || !buildInfo.isDir() || buildInfo.canonicalFilePath() != buildPath)
        return {};
    const QString joined = root + QLatin1Char('/') + relative;
    const QFileInfo info(joined);
    if (!info.exists() || !info.isFile() || info.isSymLink() || info.canonicalFilePath() != joined
        || !benchmark::isStrictlyInside(joined, buildPath))
        return {};
    return joined;
}

bool writeStdoutOnce(const QByteArray &bytes)
{
    QFile out;
    if (!out.open(stdout, QIODevice::WriteOnly))
        return false;
    return out.write(bytes) == bytes.size() && out.flush();
}

int run(const Arguments &args)
{
    QString error;

    // ---- 1. Пути и корни ----
    QString protocolPath;
    QString manifestPath;
    QString root;
    QString manifestRootPath;
    if (!resolveManifest(args.protocol, protocolPath, root))
        return fail(QStringLiteral("--protocol: not a regular file inside a repository"), kExitInput);
    if (!resolveManifest(args.manifest, manifestPath, manifestRootPath))
        return fail(QStringLiteral("--manifest: not a regular file inside a repository"), kExitInput);
    if (manifestRootPath != root)
        return fail(QStringLiteral("protocol and manifest are not in the same repository"), kExitInput);
    if (protocolPath != resolveExact(root, kProtocolPath))
        return fail(QStringLiteral("--protocol: not the repository experiment-005 protocol file"), kExitInput);
    if (manifestPath != resolveExact(root, kManifestPath))
        return fail(QStringLiteral("--manifest: not the repository experiment-005 evaluation manifest"),
                    kExitInput);

    // ---- 2. Протокол 005: точный хеш первым ----
    QByteArray protocolBytes;
    if (!benchmark::readBoundedFile(protocolPath, kMaxProtocolBytes, protocolBytes, error)) {
        const bool tooLarge = error == QLatin1String("file exceeds size limit");
        return fail(QStringLiteral("--protocol: ") + error, tooLarge ? kExitIntegrity : kExitInput);
    }
    const ProtocolError protocolError = validateProtocol005(protocolBytes);
    if (protocolError != ProtocolError::None)
        return fail(QStringLiteral("--protocol: ") + protocolErrorText(protocolError), kExitIntegrity);

    // ---- 3. Паспорт оценки и notices ----
    QByteArray manifestBytes;
    if (!benchmark::readBoundedFile(manifestPath, kMaxManifestBytes, manifestBytes, error))
        return fail(QStringLiteral("--manifest: ") + error, kExitInput);
    const ManifestResult parsed = parseManifest(manifestBytes);
    if (parsed.error != ManifestError::None)
        return fail(QStringLiteral("--manifest: ") + manifestErrorText(parsed.error),
                    parsed.error == ManifestError::ProtocolMismatch ? kExitIntegrity : kExitInput);
    const EvaluationManifest &manifest = parsed.manifest;
    for (const ManifestNotice &n : manifest.notices) {
        if (!benchmark::verifyNoticeFile(manifestPath, n.notice, n.sha256, error))
            return fail(QStringLiteral("--manifest: ") + error, kExitIntegrity);
    }

    // ---- 4. TSV признаков ----
    QByteArray featureBytes;
    {
        const QString featuresPath = resolveFeatures(root, manifest.featuresPath);
        if (featuresPath.isEmpty())
            return fail(QStringLiteral("features: missing, not a regular file or reached through a symbolic link"),
                        kExitInput);
        if (!benchmark::readBoundedFile(featuresPath, kMaxFeatureBytes, featureBytes, error))
            return fail(QStringLiteral("features: ") + error, kExitInput);
        if (!ex::bytesMatch(featureBytes, manifest.featuresBytes, manifest.featuresSha256))
            return fail(QStringLiteral("features: size or SHA-256 does not match the manifest"), kExitIntegrity);
        if (ex::sha256Hex(featureBytes) == QLatin1String(kProtocolSha256))
            return fail(QStringLiteral("features: file duplicates the protocol"), kExitIntegrity);
    }
    FeatureResult features = parseFeatures(featureBytes, manifest.featuresRows);
    featureBytes.clear();
    featureBytes.squeeze();
    if (features.error != FeatureError::None)
        return fail(QStringLiteral("features: ") + featureErrorText(features.error), kExitInput);
    const std::vector<FeatureRow> rows = std::move(features.rows);

    // ---- 5. Решения и единственная запись ----
    const QByteArray output = renderOutput(rows);
    if (output.isEmpty() || qint64(output.size()) >= kMaxOutputBytes)
        return fail(QStringLiteral("output exceeds its bound"), kExitIntegrity);
    if (!writeStdoutOnce(output))
        return fail(QStringLiteral("cannot write stdout"), kExitOutput);
    return 0;
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    const ArgumentsResult parsed = parseArguments(QCoreApplication::arguments());
    if (parsed.error != ArgumentsError::None)
        return usageError(argumentsErrorText(parsed.error));
    return run(parsed.arguments);
}
