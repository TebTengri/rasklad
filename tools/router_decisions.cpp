// Developer-only CLI опыта 006 (SYS-238): действия восьми замороженных
// router-политик для каждой строки одного шарда признаков. Не GUI, не служба,
// не устанавливается и не входит в runtime приложения.
//
//   router_decisions --protocol data/experiment-006-protocol.json
//       --manifest data/evaluation-006-sources.json --shard <0..31>
//
// Все три аргумента обязательны, каждый ровно один раз; других нет. Порядок:
//   1. Канонические пути; корни репозитория обоих файлов совпадают; это
//      именно data/experiment-006-protocol.json и data/evaluation-006-sources.json
//      без symlink-компонентов.
//   2. Протокол 006: <= 32768 байт, точный SHA-256 (первым), затем поля.
//   3. Паспорт оценки: строгий разбор всех шардов и notices, protocol_sha256,
//      SHA-256 всех notices.
//   4. Выбранный шард: индекс есть в паспорте; путь
//      build/experiment-006-data/features-NN.tsv под настоящим build/ без
//      symlink-компонентов; размер <= 8 MiB, байты и SHA-256 из паспорта;
//      затем неизменённый context::parseFeatures с rows из паспорта.
//   5. Только затем — решения для каждой строки шарда.
// Остальные шарды, исходные документы, labels, модель, словари и Hunspell не
// открываются. Происхождение membership и token-действий подтверждает
// подготовка признаков отдельно.
//
// stdout — только после полного успеха, одной записью: по строке на вход
// шарда в исходном порядке, без заголовка:
//   ordinal<TAB><U|R|-><TAB><8 кодов K/C/A/S><LF>
// ordinal — локальный номер строки в шарде (0..rows-1); коды — в порядке
// protocol.policies. Слов, префиксов, ID, путей, номера шарда и хешей нет;
// stderr — только категория ошибки. Логирования, сети, процессов и записи
// файлов нет.
//
// Коды выхода: 0 — успех; 1 — отказ входных данных (в том числе шард вне
// паспорта); 2 — неверные аргументы; 3 — нарушена целостность; 4 — ошибка
// записи stdout.

#include "benchmarkio.h"
#include "contextpolicy.h"
#include "detectormanifest.h"
#include "routerpolicy.h"

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QStringList>
#include <QTextStream>

#include <utility>
#include <vector>

using namespace rasklad;
using namespace rasklad::router;
namespace ex = rasklad::experiment;

namespace {

constexpr int kExitInput = 1;
constexpr int kExitUsage = 2;
constexpr int kExitIntegrity = 3;
constexpr int kExitOutput = 4;

int fail(const QString &message, int code)
{
    QTextStream(stderr) << "router_decisions: " << message << '\n';
    return code;
}

int usageError(const QString &message)
{
    QTextStream(stderr) << "usage: router_decisions --protocol <experiment-006-protocol.json>"
                           " --manifest <evaluation-006-sources.json> --shard <0..31>\n";
    return fail(message, kExitUsage);
}

// Канонический путь файла и канонический корень репозитория (родитель его
// каталога). Корень по исходному аргументу и по каноническому пути совпадает.
bool resolveRepositoryFile(const QString &argument, QString &canonical, QString &root)
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
    if (!info.exists() || !info.isFile() || info.isSymLink() || info.canonicalFilePath() != joined
        || !benchmark::isStrictlyInside(joined, root))
        return {};
    return joined;
}

// Шард: <root>/build и <root>/build/experiment-006-data — настоящие каталоги,
// файл — обычный, строго внутри них; канонический путь равен склеенному.
QString resolveShard(const QString &root, int index, const QString &relative)
{
    if (relative.isEmpty() || relative != shardFeaturesPath(index))
        return {};
    const QString buildPath = root + QStringLiteral("/build");
    const QFileInfo buildInfo(buildPath);
    if (buildInfo.isSymLink() || !buildInfo.isDir() || buildInfo.canonicalFilePath() != buildPath)
        return {};
    const QString dataPath = root + QLatin1Char('/') + QString::fromLatin1(kShardDirectory);
    const QFileInfo dataInfo(dataPath);
    if (dataInfo.isSymLink() || !dataInfo.isDir() || dataInfo.canonicalFilePath() != dataPath
        || !benchmark::isStrictlyInside(dataPath, buildPath))
        return {};
    const QString joined = root + QLatin1Char('/') + relative;
    const QFileInfo info(joined);
    if (!info.exists() || !info.isFile() || info.isSymLink() || info.canonicalFilePath() != joined
        || !benchmark::isStrictlyInside(joined, dataPath))
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
    if (!resolveRepositoryFile(args.protocol, protocolPath, root))
        return fail(QStringLiteral("--protocol: not a regular file inside a repository"), kExitInput);
    if (!resolveRepositoryFile(args.manifest, manifestPath, manifestRootPath))
        return fail(QStringLiteral("--manifest: not a regular file inside a repository"), kExitInput);
    if (manifestRootPath != root)
        return fail(QStringLiteral("protocol and manifest are not in the same repository"), kExitInput);
    if (protocolPath != resolveExact(root, kProtocolPath))
        return fail(QStringLiteral("--protocol: not the repository experiment-006 protocol file"), kExitInput);
    if (manifestPath != resolveExact(root, kManifestPath))
        return fail(QStringLiteral("--manifest: not the repository experiment-006 evaluation manifest"),
                    kExitInput);

    // ---- 2. Протокол 006: точный хеш первым ----
    QByteArray protocolBytes;
    if (!benchmark::readBoundedFile(protocolPath, kMaxProtocolBytes, protocolBytes, error)) {
        const bool tooLarge = error == QLatin1String("file exceeds size limit");
        return fail(QStringLiteral("--protocol: ") + error, tooLarge ? kExitIntegrity : kExitInput);
    }
    const ProtocolError protocolError = validateProtocol006(protocolBytes);
    if (protocolError != ProtocolError::None)
        return fail(QStringLiteral("--protocol: ") + protocolErrorText(protocolError), kExitIntegrity);

    // ---- 3. Паспорт оценки и все notices ----
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

    // ---- 4. Выбранный шард ----
    if (args.shard < 0 || std::size_t(args.shard) >= manifest.shards.size())
        return fail(QStringLiteral("--shard: index is not listed in the evaluation manifest"), kExitInput);
    const ShardEntry &shard = manifest.shards[std::size_t(args.shard)];
    QByteArray shardBytes;
    {
        const QString shardPath = resolveShard(root, args.shard, shard.path);
        if (shardPath.isEmpty())
            return fail(QStringLiteral("shard: missing, not a regular file or reached through a symbolic link"),
                        kExitInput);
        if (!benchmark::readBoundedFile(shardPath, kMaxShardBytes, shardBytes, error))
            return fail(QStringLiteral("shard: ") + error, kExitInput);
        if (!ex::bytesMatch(shardBytes, shard.bytes, shard.sha256))
            return fail(QStringLiteral("shard: size or SHA-256 does not match the manifest"), kExitIntegrity);
        if (ex::sha256Hex(shardBytes) == QLatin1String(kProtocolSha256))
            return fail(QStringLiteral("shard: file duplicates the protocol"), kExitIntegrity);
    }
    context::FeatureResult features = context::parseFeatures(shardBytes, shard.rows);
    shardBytes.clear();
    shardBytes.squeeze();
    if (features.error != context::FeatureError::None)
        return fail(QStringLiteral("shard: ") + context::featureErrorText(features.error), kExitInput);
    const std::vector<FeatureRow> rows = std::move(features.rows);

    // ---- 5. Решения и единственная запись ----
    const QByteArray output = router::renderOutput(rows);
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
