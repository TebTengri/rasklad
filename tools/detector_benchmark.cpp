// Offline-эксперимент SYS-233: независимая оценка детектора раскладки на
// заранее одобренных предложениях Tatoeba с замороженным протоколом.
//
// Режим оценки (все аргументы, кроме --artifact-dir, обязательны):
//   detector_benchmark --manifest data/sources.json --en <en.dic> --ru <ru.dic>
//       --eval-manifest data/evaluation-002-sources.json --eval-en <eng.tsv>
//       --eval-ru <rus.tsv> --protocol data/experiment-002-protocol.json --json
//       [--artifact-dir <существующий каталог внутри build/>]
//
// Режим пробы ресурсов (разбирается первым, читает только один артефакт):
//   detector_benchmark --probe model|exact --artifact <файл> --iterations <0..1000000> --json
//
// Порядок оценки: протокол сравнивается с замороженным документом целиком;
// затем проверяются паспорт словарей, размеры и SHA-256 словарей и
// уведомлений, паспорт оценки, размеры и SHA-256 TSV и уведомлений. Только
// после этого разбираются данные. Пороги фиксированы протоколом; калибровки
// и подбора на данных оценки нет.
//
// stdout — только агрегированный JSON после полного успеха: счётчики,
// хеши, размеры, времена. Пути, слова, предложения и ID не выводятся.
// При любом отказе stdout пуст. stderr — ошибки без путей и содержимого.
// Сеть и процессы не используются; пользовательский ввод не читается.
// Артефакты пишутся только при явном --artifact-dir.
//
// Коды выхода: 0 — успех; 1 — отказ входных данных; 2 — неверные
// аргументы; 3 — нарушена целостность; 4 — ошибка записи вывода/артефактов.

#include "benchmarkeval.h"
#include "benchmarkio.h"
#include "benchmarkprotocol.h"
#include "detectordataset.h"
#include "detectormanifest.h"
#include "evaluationdata.h"
#include "frontcodedlexicon.h"
#include "layoutconverter.h"
#include "ngramdetector.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QStringList>
#include <QTextStream>

#include <algorithm>
#include <array>
#include <initializer_list>
#include <optional>
#include <utility>
#include <vector>

using namespace rasklad;
using namespace rasklad::benchmark;
namespace ex = rasklad::experiment;

namespace {

constexpr int kExitInput = 1;
constexpr int kExitUsage = 2;
constexpr int kExitIntegrity = 3;
constexpr int kExitOutput = 4;

constexpr int kLatencySampleMax = 2000;
constexpr int kLatencyRepeats = 32;
constexpr qint64 kMaxNoiseAttempts = 100000;
constexpr qint64 kMaxProbeIterations = 1000000;
constexpr qint64 kMaxProbeArtifactBytes = 32LL * 1024 * 1024;
constexpr int kProbeKeyCount = 64;

// Значения эксперимента 001 (docs/results/experiment-001.json) для тех же
// словарей: подтверждают, что используется то же разделение и обучение.
constexpr std::array<const char *, 2> kExpectedFilteredListSha256{{
    "7b9e9cd50e8d4d24b9139b3df28c6a54cc180d54ba127681424978a00d9d977e",
    "46191cbc6df3c2fb5a0ab63f1808b18fdfe773a15813dd3013230091412d2341",
}};
constexpr std::array<qint64, 2> kExpectedAcceptedTypes{{47828, 146229}};
constexpr std::array<qint64, 2> kExpectedTrainTypes{{34025, 97632}};

constexpr char kModelArtifact[] = "detector-model.rskn";
constexpr char kLexiconArtifact[] = "exact-full-lexicons.rskp";
constexpr char kMetadataArtifact[] = "artifact-metadata.json";

void set(QJsonObject &object, const char *key, const QJsonValue &value)
{
    object.insert(QString::fromLatin1(key), value);
}

QJsonValue ratio(qint64 numerator, qint64 denominator)
{
    if (denominator <= 0)
        return QJsonValue(QJsonValue::Null);
    return QJsonValue(double(numerator) / double(denominator));
}

int fail(const QString &message, int code = kExitInput)
{
    QTextStream(stderr) << "detector_benchmark: " << message << '\n';
    return code;
}

bool writeStdout(const QJsonObject &root)
{
    QFile out;
    if (!out.open(stdout, QIODevice::WriteOnly))
        return false;
    const QByteArray bytes = QJsonDocument(root).toJson(QJsonDocument::Indented);
    return out.write(bytes) == bytes.size() && out.flush();
}

QString hexU64(quint64 value)
{
    return QStringLiteral("%1").arg(value, 16, 16, QLatin1Char('0'));
}

void printUsage()
{
    QTextStream(stderr)
        << "usage: detector_benchmark --manifest <sources.json> --en <en.dic> --ru <ru.dic>\n"
           "           --eval-manifest <evaluation-002-sources.json> --eval-en <eng.tsv>\n"
           "           --eval-ru <rus.tsv> --protocol <experiment-002-protocol.json> --json\n"
           "           [--artifact-dir <existing directory inside build/>]\n"
           "       detector_benchmark --probe model|exact --artifact <file>\n"
           "           --iterations <0..1000000> --json\n";
}

int usageError(const char *message)
{
    printUsage();
    return fail(QString::fromLatin1(message), kExitUsage);
}

// ---- Разбор аргументов ----

struct OptionSlot {
    const char *name;
    QString *target;
};

// Общий разбор: --json — флаг, остальные опции — ровно одно непустое значение.
bool parseOptions(const QStringList &args, const std::vector<OptionSlot> &optionSlots, bool &json,
                  QString &error)
{
    json = false;
    for (qsizetype i = 1; i < args.size(); ++i) {
        const QString &arg = args.at(i);
        if (arg == QLatin1String("--json")) {
            if (json) {
                error = QStringLiteral("duplicate option");
                return false;
            }
            json = true;
            continue;
        }
        QString *target = nullptr;
        for (const OptionSlot &option : optionSlots) {
            if (arg == QLatin1String(option.name))
                target = option.target;
        }
        if (!target || i + 1 >= args.size()) {
            error = QStringLiteral("unknown or incomplete option");
            return false;
        }
        if (!target->isEmpty()) {
            error = QStringLiteral("duplicate option");
            return false;
        }
        *target = args.at(++i);
        if (target->isEmpty()) {
            error = QStringLiteral("empty option value");
            return false;
        }
    }
    return true;
}

// ---- Проба ресурсов ----

std::vector<KeySequence> probeKeys()
{
    // Фиксированные синтетические ключи; не слова словаря и не данные оценки.
    std::vector<KeySequence> keys;
    keys.reserve(kProbeKeyCount);
    for (int i = 0; i < kProbeKeyCount; ++i) {
        const int length = 4 + (i * 5) % 13;
        KeySequence k;
        for (int j = 0; j < length; ++j)
            k.append(static_cast<char>((i * 13 + j * 7 + 3) % NgramModel::kLetterKeys));
        keys.push_back(k);
    }
    return keys;
}

int runProbe(const QStringList &args)
{
    QString mode;
    QString artifact;
    QString iterationsText;
    bool json = false;
    QString error;
    if (!parseOptions(args, {{"--probe", &mode}, {"--artifact", &artifact}, {"--iterations", &iterationsText}},
                      json, error)) {
        printUsage();
        return fail(error, kExitUsage);
    }
    if (mode.isEmpty() || artifact.isEmpty() || iterationsText.isEmpty() || !json)
        return usageError("--probe, --artifact, --iterations and --json are required");
    const bool modelMode = mode == QLatin1String("model");
    if (!modelMode && mode != QLatin1String("exact"))
        return usageError("--probe must be model or exact");
    if (iterationsText.size() > 7)
        return usageError("--iterations must be an integer in [0, 1000000]");
    for (const QChar ch : iterationsText) {
        if (ch.unicode() < u'0' || ch.unicode() > u'9')
            return usageError("--iterations must be an integer in [0, 1000000]");
    }
    const qint64 iterations = iterationsText.toLongLong();
    if (iterations < 0 || iterations > kMaxProbeIterations)
        return usageError("--iterations must be an integer in [0, 1000000]");

    const qint64 limit = modelMode ? qint64(NgramModel::kSerializedBytes) : kMaxProbeArtifactBytes;
    QElapsedTimer timer;
    timer.start();
    QByteArray raw;
    if (!readBoundedFile(artifact, limit, raw, error))
        return fail(QStringLiteral("--artifact: ") + error);
    const qint64 readNs = timer.nsecsElapsed();

    std::optional<NgramModel> model;
    std::optional<std::array<FrontCodedLexicon, 2>> lexicons;
    QElapsedTimer parseTimer;
    parseTimer.start();
    if (modelMode)
        model = NgramModel::deserialize(raw);
    else
        lexicons = deserializeLexiconPair(raw);
    const qint64 deserializeNs = parseTimer.nsecsElapsed();
    const qint64 loadNs = timer.nsecsElapsed();
    if (modelMode ? !model.has_value() : !lexicons.has_value())
        return fail(QStringLiteral("--artifact: invalid or unsupported artifact format"));

    const qint64 artifactBytes = raw.size();
    const QString artifactSha256 = ex::sha256Hex(raw);
    // Сырые байты не удерживаются: остаются только загруженные структуры.
    raw.clear();
    raw.squeeze();

    const std::vector<KeySequence> keys = probeKeys();
    quint64 checksum = 1469598103934665603ULL;
    QElapsedTimer loopTimer;
    loopTimer.start();
    for (qint64 it = 0; it < iterations; ++it) {
        for (const KeySequence &k : keys) {
            quint64 value = 0;
            if (modelMode) {
                const TokenScore s = model->score(k);
                value = quint64(quint32(s.raw[0])) ^ (quint64(quint32(s.raw[1])) << 32);
            } else {
                value = (lexicons->at(0).contains(k) ? 1U : 0U) | (lexicons->at(1).contains(k) ? 2U : 0U);
                value += quint64(k.size()) << 8;
            }
            checksum = (checksum ^ value) * 1099511628211ULL;
        }
    }
    const qint64 loopNs = loopTimer.nsecsElapsed();
    const qint64 operations = iterations * qint64(keys.size());

    QJsonObject root;
    set(root, "mode", mode);
    set(root, "artifactBytes", artifactBytes);
    set(root, "artifactSha256", artifactSha256);
    set(root, "readNs", readNs);
    set(root, "deserializeNs", deserializeNs);
    set(root, "loadNs", loadNs);
    set(root, "iterations", iterations);
    set(root, "syntheticKeys", qint64(keys.size()));
    set(root, "operations", operations);
    set(root, "loopNs", loopNs);
    set(root, "nsPerOperation", operations > 0 ? QJsonValue(double(loopNs) / double(operations))
                                               : QJsonValue(QJsonValue::Null));
    set(root, "checksum", hexU64(checksum));
    if (modelMode) {
        set(root, "residentTableBytes", qint64(NgramModel::kWeightBytes));
    } else {
        set(root, "residentStructureBytes",
            lexicons->at(0).residentStructureBytes() + lexicons->at(1).residentStructureBytes());
        set(root, "keyCounts", QJsonArray{lexicons->at(0).keyCount(), lexicons->at(1).keyCount()});
    }
    set(root, "scope",
        QStringLiteral("inference-only probe: loads one serialized artifact, frees the raw file bytes and "
                       "keeps only the loaded structure until exit; no dictionaries, corpora or training "
                       "lists are read. loadNs = read + deserialize; OS page-cache state is unknown. RSS "
                       "is not measured here: measure this process externally."));
    if (!writeStdout(root))
        return fail(QStringLiteral("cannot write stdout"), kExitOutput);
    // model/lexicons освобождаются только после вывода.
    return 0;
}

// ---- Оценка ----

struct Options {
    QString manifest;
    QString en;
    QString ru;
    QString evalManifest;
    QString evalEn;
    QString evalRu;
    QString protocol;
    QString artifactDir;
};

QJsonObject lexiconJson(const FrontCodedLexicon &lexicon, const std::vector<KeySequence> &keys)
{
    qint64 rawKeyBytes = 0;
    for (const KeySequence &k : keys)
        rawKeyBytes += k.size();
    QJsonObject o;
    set(o, "keys", lexicon.keyCount());
    set(o, "blocks", lexicon.blockCount());
    set(o, "blockSize", FrontCodedLexicon::kBlockSize);
    set(o, "payloadBytes", lexicon.payloadBytes());
    set(o, "serializedBytes", lexicon.serializedBytes());
    set(o, "residentStructureBytes", lexicon.residentStructureBytes());
    set(o, "rawCanonicalKeyBytes", rawKeyBytes);
    return o;
}

struct LatencyResult {
    double p50 = 0;
    double p95 = 0;
    double p99 = 0;
    double max = 0;
    double batchedMean = 0;
};

template <typename Call>
LatencyResult measure(std::size_t count, Call call)
{
    LatencyResult result;
    if (count == 0)
        return result;
    for (std::size_t i = 0; i < count; ++i)
        call(i);
    std::vector<double> perToken;
    perToken.reserve(count);
    QElapsedTimer timer;
    for (std::size_t i = 0; i < count; ++i) {
        timer.start();
        for (int r = 0; r < kLatencyRepeats; ++r)
            call(i);
        perToken.push_back(double(timer.nsecsElapsed()) / kLatencyRepeats);
    }
    std::sort(perToken.begin(), perToken.end());
    result.p50 = percentileNearestRank(perToken, 50);
    result.p95 = percentileNearestRank(perToken, 95);
    result.p99 = percentileNearestRank(perToken, 99);
    result.max = perToken.back();
    timer.start();
    for (int r = 0; r < kLatencyRepeats; ++r) {
        for (std::size_t i = 0; i < count; ++i)
            call(i);
    }
    result.batchedMean = double(timer.nsecsElapsed()) / double(count * std::size_t(kLatencyRepeats));
    return result;
}

QJsonObject latencyJson(const LatencyResult &r)
{
    QJsonObject o;
    set(o, "p50Ns", r.p50);
    set(o, "p95Ns", r.p95);
    set(o, "p99Ns", r.p99);
    set(o, "maxNs", r.max);
    set(o, "batchedMeanNs", r.batchedMean);
    return o;
}

struct ChallengeTally {
    qint64 count = 0;
    qint64 supported = 0;
    qint64 eligible = 0;
    qint64 ambiguous = 0;
    std::array<RoleCounts, kMethodCount> methods;
};

int runBenchmark(const Options &opt)
{
    QElapsedTimer totalTimer;
    totalTimer.start();
    QString error;
    const std::array<DetectorLanguage, 2> languages{{DetectorLanguage::En, DetectorLanguage::Ru}};

    // ---- 1. Протокол ----
    QByteArray protocolBytes;
    if (!readBoundedFile(opt.protocol, kMaxProtocolBytes, protocolBytes, error))
        return fail(QStringLiteral("--protocol: ") + error);
    const ProtocolResult protocol = validateProtocol(protocolBytes);
    if (protocol.error != ProtocolError::None) {
        QString message = QStringLiteral("--protocol: ") + protocolErrorText(protocol.error);
        if (!protocol.mismatchedField.isEmpty())
            message += QStringLiteral(" (field: ") + protocol.mismatchedField + QLatin1Char(')');
        return fail(message);
    }

    // ---- 2. Паспорт и файлы словарей ----
    QByteArray manifestBytes;
    if (!readBoundedFile(opt.manifest, ex::kMaxManifestBytes, manifestBytes, error))
        return fail(QStringLiteral("--manifest: ") + error);
    const ex::ManifestParseResult manifestResult = ex::parseExperimentManifest(manifestBytes);
    if (manifestResult.error != ex::ManifestError::None)
        return fail(QStringLiteral("--manifest: ") + ex::manifestErrorText(manifestResult.error));
    const ex::ExperimentManifest &manifest = manifestResult.manifest;

    const std::array<QString, 2> dicPaths{{opt.en, opt.ru}};
    std::array<QByteArray, 2> dicBytes;
    for (const DetectorLanguage language : languages) {
        const std::size_t li = std::size_t(language);
        const QString option = QStringLiteral("--") + QString::fromLatin1(languageName(language));
        if (!readBoundedFile(dicPaths[li], ex::kMaxDicBytes, dicBytes[li], error))
            return fail(option + QStringLiteral(": ") + error);
        const ex::ManifestSource &source = manifest.sources[li];
        if (!ex::bytesMatch(dicBytes[li], source.bytes, source.sha256))
            return fail(option + QStringLiteral(": file size or SHA-256 does not match manifest"));
        if (!verifyNoticeFile(opt.manifest, source.notice, source.noticeSha256, error))
            return fail(option + QStringLiteral(": ") + error);
    }

    // ---- 3. Паспорт и файлы оценки ----
    QByteArray evalManifestBytes;
    if (!readBoundedFile(opt.evalManifest, kMaxEvalManifestBytes, evalManifestBytes, error))
        return fail(QStringLiteral("--eval-manifest: ") + error);
    const EvalManifestResult evalResult = parseEvaluationManifest(evalManifestBytes);
    if (evalResult.error != EvalManifestError::None)
        return fail(QStringLiteral("--eval-manifest: ") + evalManifestErrorText(evalResult.error));
    const EvalManifest &evalManifest = evalResult.manifest;
    const QString projectRoot = QFileInfo(ex::manifestRoot(opt.manifest)).canonicalFilePath();
    if (projectRoot.isEmpty()
        || QFileInfo(ex::manifestRoot(opt.evalManifest)).canonicalFilePath() != projectRoot)
        return fail(QStringLiteral("--eval-manifest: manifests are not in the same repository"));

    const std::array<QString, 2> evalPaths{{opt.evalEn, opt.evalRu}};
    std::array<QByteArray, 2> evalBytes;
    for (const DetectorLanguage language : languages) {
        const std::size_t li = std::size_t(language);
        const QString option = QStringLiteral("--eval-") + QString::fromLatin1(languageName(language));
        if (!readBoundedFile(evalPaths[li], kMaxEvalSourceBytes, evalBytes[li], error))
            return fail(option + QStringLiteral(": ") + error);
        const EvalSource &source = evalManifest.sources[li];
        if (!ex::bytesMatch(evalBytes[li], source.bytes, source.sha256))
            return fail(option + QStringLiteral(": file size or SHA-256 does not match manifest"));
        if (!verifyNoticeFile(opt.evalManifest, source.notice, source.noticeSha256, error))
            return fail(option + QStringLiteral(": ") + error);
    }
    {
        const QSet<QString> distinct{manifest.sources[0].sha256, manifest.sources[1].sha256,
                                     evalManifest.sources[0].sha256, evalManifest.sources[1].sha256};
        if (distinct.size() != 4)
            return fail(QStringLiteral("source leak: an evaluation file duplicates another source"));
    }

    QString artifactDir;
    if (!opt.artifactDir.isEmpty()) {
        artifactDir = resolveArtifactDir(projectRoot, opt.artifactDir, error);
        if (artifactDir.isEmpty())
            return fail(QStringLiteral("--artifact-dir: ") + error);
    }

    QElapsedTimer stageTimer;
    stageTimer.start();

    // ---- 4. Словари, разделение эксперимента 001 ----
    std::array<ex::DicParseResult, 2> parsed;
    std::array<std::vector<KeySequence>, 2> fullKeys;
    std::array<std::vector<KeySequence>, 2> trainKeys;
    std::array<std::vector<ex::SplitEntry>, 2> splitEntries;
    std::array<qint64, 2> canonicalFailures{{0, 0}};
    QJsonObject dictionariesJson;
    for (const DetectorLanguage language : languages) {
        const std::size_t li = std::size_t(language);
        const QString option = QStringLiteral("--") + QString::fromLatin1(languageName(language));
        parsed[li] = ex::parseHunspellDic(dicBytes[li], language);
        dicBytes[li].clear();
        if (parsed[li].error != ex::DicError::None)
            return fail(option + QStringLiteral(": ") + ex::dicErrorText(parsed[li].error));
        const QString filteredSha = ex::sha256Hex(parsed[li].words.join(QLatin1Char('\n')).toUtf8());
        if (filteredSha != QLatin1String(kExpectedFilteredListSha256[li])
            || parsed[li].report.accepted != kExpectedAcceptedTypes[li])
            return fail(option + QStringLiteral(": filtered dictionary differs from experiment 001"),
                        kExitIntegrity);
        for (const QString &word : std::as_const(parsed[li].words)) {
            const CanonicalToken canonical = canonicalizeToken(word, homeLayout(language));
            if (canonical.status != CanonicalStatus::Ok) {
                ++canonicalFailures[li];
                continue;
            }
            const ex::SplitPart part = ex::splitForKeys(canonical.keys);
            fullKeys[li].push_back(canonical.keys);
            if (part == ex::SplitPart::Train)
                trainKeys[li].push_back(canonical.keys);
            splitEntries[li].push_back({word, canonical.keys, part});
        }
        if (qint64(trainKeys[li].size()) != kExpectedTrainTypes[li])
            return fail(option + QStringLiteral(": train split differs from experiment 001"), kExitIntegrity);
        QJsonObject d;
        set(d, "acceptedTypes", parsed[li].report.accepted);
        set(d, "canonicalFailures", canonicalFailures[li]);
        set(d, "fullKeys", qint64(fullKeys[li].size()));
        set(d, "trainKeys", qint64(trainKeys[li].size()));
        set(d, "filteredListSha256", filteredSha);
        set(d, "matchesExperiment001", true);
        set(dictionariesJson, languageName(language), d);
    }
    const ex::LeakReport leaks = ex::countSplitLeaks(splitEntries[0], splitEntries[1]);
    if (!leaks.leakFree())
        return fail(QStringLiteral("experiment-001 split leakage detected"), kExitIntegrity);
    splitEntries = {};
    set(dictionariesJson, "splitLeakFree", true);
    set(dictionariesJson, "processing",
        QStringLiteral("experiment-001 helpers: Hunspell stems, lowercase letter-only 1..32, affixes not "
                       "expanded; full lexicons = all approved dictionary keys; train = splitForKeys == "
                       "Train"));

    std::array<QSet<KeySequence>, 2> fullSets;
    std::array<QSet<KeySequence>, 2> trainSets;
    for (std::size_t li = 0; li < 2; ++li) {
        fullSets[li] = QSet<KeySequence>(fullKeys[li].begin(), fullKeys[li].end());
        trainSets[li] = QSet<KeySequence>(trainKeys[li].begin(), trainKeys[li].end());
        std::sort(fullKeys[li].begin(), fullKeys[li].end());
        fullKeys[li].erase(std::unique(fullKeys[li].begin(), fullKeys[li].end()), fullKeys[li].end());
    }
    qint64 ambiguousKeyCount = 0;
    qint64 ambiguousLen4Count = 0;
    std::vector<KeySequence> ambiguousLen4;
    for (const KeySequence &k : fullKeys[0]) {
        if (fullSets[1].contains(k)) {
            ++ambiguousKeyCount;
            if (k.size() >= kMinDecisionKeys) {
                ++ambiguousLen4Count;
                ambiguousLen4.push_back(k);
            }
        }
    }

    // ---- 5. Модель: алгоритм эксперимента 001 без изменений ----
    QElapsedTimer trainTimer;
    trainTimer.start();
    qsizetype skipped = 0;
    const NgramModel trainedModel = NgramModel::train(trainKeys[0], trainKeys[1], &skipped);
    const qint64 trainingMs = trainTimer.elapsed();
    if (skipped != 0 || !trainedModel.isValid())
        return fail(QStringLiteral("model training failed integrity checks"), kExitIntegrity);
    const QByteArray modelBytes = trainedModel.serialize();
    const std::optional<NgramModel> model = NgramModel::deserialize(modelBytes);
    if (!model || model->serialize() != modelBytes || modelBytes.size() != NgramModel::kSerializedBytes)
        return fail(QStringLiteral("model serialization round trip failed"), kExitIntegrity);

    // ---- 6. Точные словари: front-coded, проверка против QSet ----
    std::array<std::optional<FrontCodedLexicon>, 2> fullBuilt;
    std::array<std::optional<FrontCodedLexicon>, 2> trainBuilt;
    for (std::size_t li = 0; li < 2; ++li) {
        fullBuilt[li] = FrontCodedLexicon::build(fullKeys[li]);
        trainBuilt[li] = FrontCodedLexicon::build(trainKeys[li]);
        if (!fullBuilt[li] || !trainBuilt[li])
            return fail(QStringLiteral("exact lexicon build failed"), kExitIntegrity);
    }
    const QByteArray fullPairBytes = serializeLexiconPair(*fullBuilt[0], *fullBuilt[1]);
    const QByteArray trainPairBytes = serializeLexiconPair(*trainBuilt[0], *trainBuilt[1]);
    std::optional<std::array<FrontCodedLexicon, 2>> fullLex = deserializeLexiconPair(fullPairBytes);
    std::optional<std::array<FrontCodedLexicon, 2>> trainLex = deserializeLexiconPair(trainPairBytes);
    if (!fullLex || !trainLex
        || serializeLexiconPair(fullLex->at(0), fullLex->at(1)) != fullPairBytes
        || serializeLexiconPair(trainLex->at(0), trainLex->at(1)) != trainPairBytes)
        return fail(QStringLiteral("exact lexicon serialization round trip failed"), kExitIntegrity);
    for (std::size_t a = 0; a < 2; ++a) {
        for (const KeySequence &k : fullKeys[a]) {
            for (std::size_t b = 0; b < 2; ++b) {
                if (fullLex->at(b).contains(k) != fullSets[b].contains(k)
                    || trainLex->at(b).contains(k) != trainSets[b].contains(k))
                    return fail(QStringLiteral("exact lexicon membership differs from reference set"),
                                kExitIntegrity);
            }
        }
    }
    const qint64 prepareMs = stageTimer.elapsed();

    FeatureContext context;
    context.model = &*model;
    context.train = {{&trainLex->at(0), &trainLex->at(1)}};
    context.full = {{&fullLex->at(0), &fullLex->at(1)}};
    context.singleD = frozenSingleD();
    context.twoScore = frozenTwoScore();

    // ---- 7. Данные оценки: строгий разбор, выборка, токенизация ----
    stageTimer.start();
    std::array<std::vector<QString>, 2> tokens;
    QJsonObject selectionJson;
    QJsonObject tokenizationJson;
    QSet<qint64> enIds;
    for (const DetectorLanguage language : languages) {
        const std::size_t li = std::size_t(language);
        const QString option = QStringLiteral("--eval-") + QString::fromLatin1(languageName(language));
        TsvParseResult tsv = parseTatoebaTsv(evalBytes[li], QString::fromLatin1(isoLanguageFor(language)));
        evalBytes[li].clear();
        if (tsv.error != TsvError::None)
            return fail(option + QStringLiteral(": ") + tsvErrorText(tsv.error));
        for (const SentenceRecord &record : tsv.records) {
            if (language == DetectorLanguage::En)
                enIds.insert(record.id);
            else if (enIds.contains(record.id))
                return fail(QStringLiteral("evaluation leak: a sentence ID appears in both languages"));
        }
        SelectionResult selection =
            selectSentences(std::move(tsv.records), QByteArray(kHoldoutSeed), kSampleLimitPerLanguage);
        if (selection.report.selected != kSampleLimitPerLanguage)
            return fail(option + QStringLiteral(": fewer unique sentences than the frozen sample size"));
        TokenizeStats stats;
        for (const SentenceRecord &record : selection.selected) {
            std::vector<QString> sentenceTokens = tokenizeSentence(record.text, &stats);
            for (QString &t : sentenceTokens)
                tokens[li].push_back(std::move(t));
        }
        selection.selected.clear();
        if (tokens[li].empty())
            return fail(option + QStringLiteral(": selected sentences produced no tokens"));

        QJsonObject s;
        set(s, "records", selection.report.records);
        set(s, "uniqueTexts", selection.report.uniqueTexts);
        set(s, "duplicateTextRecords", selection.report.duplicateTextRecords);
        set(s, "selected", selection.report.selected);
        set(s, "selectedIdsSha256", selection.report.selectedIdsSha256);
        set(selectionJson, languageName(language), s);
        QJsonObject t;
        set(t, "whitespacePieces", stats.pieces);
        set(t, "emptyAfterTrim", stats.emptyAfterTrim);
        set(t, "trimmedTokens", stats.trimmed);
        set(t, "tokens", qint64(tokens[li].size()));
        set(tokenizationJson, languageName(language), t);
    }
    enIds.clear();

    NaturalCases natural = buildNaturalCases(tokens, context);
    tokens = {};
    const std::vector<EvalCase> &cases = natural.cases;
    for (const NaturalTokenStats &s : natural.stats) {
        if (s.wrongCanonicalMismatches != 0)
            return fail(QStringLiteral("wrong-layout copy changed canonical keys"), kExitIntegrity);
        if (s.wrongCopies == 0)
            return fail(QStringLiteral("no wrong-layout copies could be generated"));
    }
    // Признаки сверяются с эталонными множествами на каждом случае.
    for (const EvalCase &c : cases) {
        const CanonicalToken canonical = canonicalizeToken(c.text, c.layout);
        if (canonical.status != c.status)
            return fail(QStringLiteral("case canonicalization is not deterministic"), kExitIntegrity);
        if (canonical.status != CanonicalStatus::Ok)
            continue;
        const std::size_t obs = std::size_t(observedLanguage(c.layout));
        const std::size_t alt = std::size_t(alternativeLanguage(c.layout));
        if (c.features.observedFull != fullSets[obs].contains(canonical.keys)
            || c.features.alternativeFull != fullSets[alt].contains(canonical.keys)
            || c.features.observedTrain != trainSets[obs].contains(canonical.keys)
            || c.features.alternativeTrain != trainSets[alt].contains(canonical.keys))
            return fail(QStringLiteral("case membership differs from reference set"), kExitIntegrity);
    }

    std::vector<std::size_t> allIndices;
    std::vector<std::size_t> typeIndices;
    allIndices.reserve(cases.size());
    for (std::size_t i = 0; i < cases.size(); ++i) {
        allIndices.push_back(i);
        if (cases[i].typeRepresentative)
            typeIndices.push_back(i);
    }
    std::array<std::vector<Outcome>, kMethodCount> outcomes;
    for (std::size_t m = 0; m < kMethods.size(); ++m) {
        outcomes[m].reserve(cases.size());
        for (const EvalCase &c : cases)
            outcomes[m].push_back(
                classifyOutcome(decideMethod(kMethods[m], c.features), c.wrong, c.ambiguous, c.conversionMatches));
    }

    auto slicesJson = [&](std::size_t m, const std::vector<std::size_t> &indices) {
        SliceCounts overall;
        std::array<SliceCounts, 2> byLanguage;
        std::array<SliceCounts, 2> byLength;
        std::array<SliceCounts, kMembershipCount> byFull;
        std::array<SliceCounts, kMembershipCount> byTrain;
        for (const std::size_t i : indices) {
            const EvalCase &c = cases[i];
            const Outcome o = outcomes[m][i];
            overall.add(c.wrong, c.ambiguous, o);
            byLanguage[std::size_t(c.language)].add(c.wrong, c.ambiguous, o);
            byLength[c.length >= kMinDecisionKeys ? 1 : 0].add(c.wrong, c.ambiguous, o);
            byFull[std::size_t(fullMembership(c.features))].add(c.wrong, c.ambiguous, o);
            byTrain[std::size_t(trainMembership(c.features))].add(c.wrong, c.ambiguous, o);
        }
        QJsonObject o = sliceJson(overall);
        QJsonObject lang;
        set(lang, "en", sliceJson(byLanguage[0]));
        set(lang, "ru", sliceJson(byLanguage[1]));
        set(o, "byIntendedLanguage", lang);
        QJsonObject len;
        set(len, "len1to3", sliceJson(byLength[0]));
        set(len, "len4plus", sliceJson(byLength[1]));
        set(o, "byLength", len);
        QJsonObject full;
        QJsonObject train;
        for (int k = 0; k < kMembershipCount; ++k) {
            set(full, membershipName(Membership(k)), sliceJson(byFull[std::size_t(k)]));
            set(train, membershipName(Membership(k)), sliceJson(byTrain[std::size_t(k)]));
        }
        set(o, "byFullMembership", full);
        set(o, "byTrainMembership", train);
        return std::make_pair(o, overall);
    };

    QJsonObject methodsJson;
    for (std::size_t m = 0; m < kMethods.size(); ++m) {
        const std::pair<QJsonObject, SliceCounts> occurrences = slicesJson(m, allIndices);
        const QJsonObject types = slicesJson(m, typeIndices).first;
        QJsonObject j;
        set(j, "occurrences", occurrences.first);
        set(j, "uniqueTypes", types);
        set(j, "expectedPrecision", expectedPrecisionJson(occurrences.second));
        set(methodsJson, methodName(kMethods[m]), j);
    }

    QJsonObject pairwiseJson;
    const std::array<Method, 2> baselines{{Method::ExactFull, Method::TwoScore}};
    for (std::size_t m = 0; m < kMethods.size(); ++m) {
        QJsonObject j;
        for (const Method baseline : baselines) {
            if (baseline == kMethods[m])
                continue;
            const std::size_t b = std::size_t(baseline);
            QJsonObject p;
            set(p, "occurrences", pairedJson(pairedDelta(outcomes[m], outcomes[b], allIndices)));
            set(p, "uniqueTypes", pairedJson(pairedDelta(outcomes[m], outcomes[b], typeIndices)));
            set(j, baseline == Method::ExactFull ? "vsExactFull" : "vsTwoScore", p);
        }
        set(pairwiseJson, methodName(kMethods[m]), j);
    }
    set(pairwiseJson, "definition",
        QStringLiteral("paired over identical cases: addedRecoveries = method Recovered and baseline not; "
                       "lostRecoveries = reverse; addedUnsafeChanges = method unsafe (false change, wrong "
                       "conversion or ambiguous change) and baseline not; preventedUnsafeChanges = reverse"));

    QJsonObject naturalJson;
    for (const DetectorLanguage language : languages)
        set(naturalJson, languageName(language), naturalStatsJson(natural.stats[std::size_t(language)]));
    set(naturalJson, "caseCount", qint64(cases.size()));
    set(naturalJson, "uniqueTypeCount", qint64(typeIndices.size()));
    const qint64 evaluationMs = stageTimer.elapsed();

    // ---- 8. Испытание, не зависящее от корпуса ----
    stageTimer.start();
    std::vector<ChallengeCase> challenge;
    for (const ChallengeFixture &f : challengeFixtures())
        challenge.push_back(makeChallengeCase(f.category, f.layout, f.text, context));
    const auto excluded = [&fullSets](const KeySequence &k) {
        return fullSets[0].contains(k) || fullSets[1].contains(k);
    };
    qint64 noiseCounter = 0;
    QJsonObject noiseJson;
    for (const SourceLayout layout : {SourceLayout::Us, SourceLayout::Ru}) {
        // Общий бюджет на обе раскладки: счётчик начинается с 0 и растёт на
        // каждую попытку, поэтому остаток = предел − уже сделанные попытки.
        const qint64 remainingAttempts = kMaxNoiseAttempts - noiseCounter;
        const NoiseResult noise = generateNoiseKeys(QByteArray(kNoiseSeed), kNoisePerLayout, kNoiseMinLength,
                                                    kNoiseMaxLength, noiseCounter, remainingAttempts, excluded);
        if (!noise.complete)
            return fail(QStringLiteral("noise generation did not complete within the attempt bound"),
                        kExitIntegrity);
        const QString category = layout == SourceLayout::Us ? QStringLiteral("noiseUs") : QStringLiteral("noiseRu");
        for (const KeySequence &k : noise.keys) {
            const QString text = keysToLayoutText(k, layout);
            const CanonicalToken check = canonicalizeToken(text, layout);
            if (check.status != CanonicalStatus::Ok || check.keys != k)
                return fail(QStringLiteral("noise token does not round-trip"), kExitIntegrity);
            challenge.push_back(makeChallengeCase(category, layout, text, context));
        }
        QJsonObject n;
        set(n, "accepted", qint64(noise.keys.size()));
        set(n, "attempts", noise.attempts);
        set(n, "rejectedDictionaryHits", noise.rejectedExcluded);
        set(n, "rejectedDuplicates", noise.rejectedDuplicate);
        set(noiseJson, layoutName(layout), n);
    }
    for (const KeySequence &k : ambiguousLen4) {
        challenge.push_back(makeChallengeCase(QStringLiteral("ambiguousLen4TypedInUs"), SourceLayout::Us,
                                              keysToLayoutText(k, SourceLayout::Us), context));
        challenge.push_back(makeChallengeCase(QStringLiteral("ambiguousLen4TypedInRu"), SourceLayout::Ru,
                                              keysToLayoutText(k, SourceLayout::Ru), context));
    }
    QStringList categoryOrder;
    QHash<QString, ChallengeTally> tallies;
    std::array<RoleCounts, kMethodCount> challengeTotals;
    for (const ChallengeCase &c : challenge) {
        if (!tallies.contains(c.category))
            categoryOrder.append(c.category);
        ChallengeTally &t = tallies[c.category];
        ++t.count;
        if (c.features.supported)
            ++t.supported;
        if (isDecisionEligible(c.features))
            ++t.eligible;
        if (c.ambiguous)
            ++t.ambiguous;
        for (std::size_t m = 0; m < kMethods.size(); ++m) {
            const Outcome o = classifyOutcome(decideMethod(kMethods[m], c.features), false, c.ambiguous, false);
            t.methods[m].add(o);
            challengeTotals[m].add(o);
        }
    }
    auto challengeMethodsJson = [](const std::array<RoleCounts, kMethodCount> &counts) {
        QJsonObject o;
        for (std::size_t m = 0; m < kMethods.size(); ++m) {
            QJsonObject r;
            set(r, "keep", counts[m].keep);
            set(r, "abstain", counts[m].abstain);
            set(r, "changes", counts[m].changes());
            set(r, "changeRate", ratio(counts[m].changes(), counts[m].count));
            set(o, methodName(kMethods[m]), r);
        }
        return o;
    };
    QJsonObject categoriesJson;
    for (const QString &name : std::as_const(categoryOrder)) {
        const ChallengeTally &t = tallies[name];
        QJsonObject c;
        set(c, "count", t.count);
        set(c, "supported", t.supported);
        set(c, "eligibleLen4Plus", t.eligible);
        set(c, "ambiguousKeys", t.ambiguous);
        set(c, "methods", challengeMethodsJson(t.methods));
        categoriesJson.insert(name, c);
    }
    QJsonObject challengeJson;
    set(challengeJson, "version", kChallengeVersion);
    set(challengeJson, "intent",
        QStringLiteral("every challenge token is labelled Keep; any Correct is an unsafe change. Noise is "
                       "unknown intent excluded from both full lexicons, not proven non-words. Ambiguous "
                       "keys come from the full lexicons (oracle metadata). No heuristic gates are derived "
                       "from these results."));
    set(challengeJson, "categories", categoriesJson);
    set(challengeJson, "allCategories", challengeMethodsJson(challengeTotals));
    set(challengeJson, "noise", noiseJson);
    set(challengeJson, "noiseRule",
        QStringLiteral("attempt i: d = SHA-256(noise_seed || 0x00 || ASCII decimal i), i = 0,1,...; us "
                       "layout first, then ru continues the counter; length = 4 + d[0] mod 13; key j = "
                       "d[1+j] mod 33 (small modulo bias); keys in either full lexicon and duplicates "
                       "are rejected; at most 100000 attempts in total across both layouts (ru receives "
                       "the budget left after us)"));
    set(challengeJson, "ambiguousKeysTotal", ambiguousKeyCount);
    set(challengeJson, "ambiguousKeysLen4Plus", ambiguousLen4Count);
    const qint64 challengeMs = stageTimer.elapsed();

    // ---- 9. Тёплая задержка ----
    stageTimer.start();
    std::vector<std::size_t> sample;
    const std::size_t stride = std::max<std::size_t>(1, cases.size() / std::size_t(kLatencySampleMax));
    for (std::size_t i = 0; i < cases.size() && sample.size() < std::size_t(kLatencySampleMax); i += stride)
        sample.push_back(i);
    for (const std::size_t i : sample) {
        for (const Method method : kMethods) {
            if (decideLive(method, cases[i].text, cases[i].layout, context)
                != decideMethod(method, cases[i].features))
                return fail(QStringLiteral("live decision path differs from evaluated decision"),
                            kExitIntegrity);
        }
    }
    volatile qint64 sink = 0;
    QJsonObject pathsJson;
    for (const Method method : kMethods) {
        const LatencyResult r = measure(sample.size(), [&](std::size_t s) {
            const EvalCase &c = cases[sample[s]];
            sink = sink + int(decideLive(method, c.text, c.layout, context));
        });
        set(pathsJson, methodName(method), latencyJson(r));
    }
    std::vector<std::pair<KeySequence, SourceLayout>> lookupSample;
    for (const std::size_t i : sample) {
        const CanonicalToken canonical = canonicalizeToken(cases[i].text, cases[i].layout);
        if (canonical.status == CanonicalStatus::Ok)
            lookupSample.push_back({canonical.keys, cases[i].layout});
    }
    const LatencyResult frontCoded = measure(lookupSample.size(), [&](std::size_t s) {
        const auto &[keys, layout] = lookupSample[s];
        const bool observed = fullLex->at(std::size_t(observedLanguage(layout))).contains(keys);
        const bool alternative = fullLex->at(std::size_t(alternativeLanguage(layout))).contains(keys);
        sink = sink + int(observed) + 2 * int(alternative);
    });
    const LatencyResult hashSet = measure(lookupSample.size(), [&](std::size_t s) {
        const auto &[keys, layout] = lookupSample[s];
        const bool observed = fullSets[std::size_t(observedLanguage(layout))].contains(keys);
        const bool alternative = fullSets[std::size_t(alternativeLanguage(layout))].contains(keys);
        sink = sink + int(observed) + 2 * int(alternative);
    });
    const LatencyResult modelOnly = measure(lookupSample.size(), [&](std::size_t s) {
        const TokenScore score = model->score(lookupSample[s].first);
        sink = sink + score.raw[0];
    });
    QJsonObject lookupJson;
    set(lookupJson, "frontCodedObservedAndAlternative", latencyJson(frontCoded));
    set(lookupJson, "qsetObservedAndAlternative", latencyJson(hashSet));
    set(lookupJson, "modelScoreOnly", latencyJson(modelOnly));
    set(lookupJson, "sampleKeys", qint64(lookupSample.size()));
    QJsonObject latencyJsonRoot;
    set(latencyJsonRoot, "fullDecisionPaths", pathsJson);
    set(latencyJsonRoot, "precanonicalizedLookups", lookupJson);
    set(latencyJsonRoot, "sampleCases", qint64(sample.size()));
    set(latencyJsonRoot, "repeatsPerCase", kLatencyRepeats);
    set(latencyJsonRoot, "methodology",
        QStringLiteral("warm, single thread. Sample: every k-th natural evaluation case in deterministic "
                       "order, at most 2000 (k = max(1, cases/2000)); live paths are checked to equal the "
                       "evaluated decisions first. One warm-up pass. pNN = nearest-rank percentile over "
                       "cases of the mean of 32 back-to-back calls on that case (QElapsedTimer overhead "
                       "included, amortized over 32 calls); not single-keystroke latency. batchedMeanNs = "
                       "one timer over 32 passes of the whole sample divided by calls. fullDecisionPaths "
                       "include canonicalizeToken (normalization) on every call; precanonicalizedLookups "
                       "exclude it. QSet is an in-memory hash reference, not a compact format."));
    const qint64 latencyMs = stageTimer.elapsed();

    // ---- 10. Размеры ----
    QJsonObject resourcesJson;
    QJsonObject modelJson;
    set(modelJson, "weightBytes", NgramModel::kWeightBytes);
    set(modelJson, "serializedBytes", qint64(modelBytes.size()));
    set(modelJson, "serializedSha256", ex::sha256Hex(modelBytes));
    set(modelJson, "qCompressLevel9Bytes", qint64(qCompress(modelBytes, 9).size()));
    set(modelJson, "trainingTypesEn", qint64(trainKeys[0].size()));
    set(modelJson, "trainingTypesRu", qint64(trainKeys[1].size()));
    set(resourcesJson, "ngramModel", modelJson);
    QJsonObject exactFullJson;
    set(exactFullJson, "en", lexiconJson(fullLex->at(0), fullKeys[0]));
    set(exactFullJson, "ru", lexiconJson(fullLex->at(1), fullKeys[1]));
    set(exactFullJson, "pairSerializedBytes", qint64(fullPairBytes.size()));
    set(exactFullJson, "pairSerializedSha256", ex::sha256Hex(fullPairBytes));
    set(exactFullJson, "pairQCompressLevel9Bytes", qint64(qCompress(fullPairBytes, 9).size()));
    set(resourcesJson, "exactFullFrontCoded", exactFullJson);
    std::array<std::vector<KeySequence>, 2> trainSorted = trainKeys;
    for (auto &v : trainSorted)
        std::sort(v.begin(), v.end());
    QJsonObject exactTrainJson;
    set(exactTrainJson, "en", lexiconJson(trainLex->at(0), trainSorted[0]));
    set(exactTrainJson, "ru", lexiconJson(trainLex->at(1), trainSorted[1]));
    set(exactTrainJson, "pairSerializedBytes", qint64(trainPairBytes.size()));
    set(resourcesJson, "exactTrainFrontCoded", exactTrainJson);
    set(resourcesJson, "boundary",
        QStringLiteral("front-coded sorted canonical keys, 16 keys per block, binary search over decoded "
                       "first keys and exact comparison; a simple experimental structure, NOT an "
                       "optimized industry FST/DAFSA. residentStructureBytes = container payload of "
                       "payload + offsets + first-key directory, excluding allocator overhead; not RSS. "
                       "Process RSS, peak and cold start are measured only by an external monitor of "
                       "separate --probe child processes. qCompress is a storage reference only."));

    // ---- 11. Артефакты (только при явном --artifact-dir) ----
    QJsonObject artifactsJson;
    set(artifactsJson, "written", !artifactDir.isEmpty());
    if (!artifactDir.isEmpty()) {
        QJsonObject meta;
        set(meta, "format", QStringLiteral("rasklad SYS-233 detector_benchmark artifacts v1"));
        set(meta, "scope",
            QStringLiteral("local-experiment-only; release_approved=false; derived from the approved local "
                           "training dictionaries; not approved for distribution"));
        set(meta, "obligations",
            QStringLiteral("keep with the full upstream notices listed below; this is a modified "
                           "derivative; the exact lexicon encodes every filtered dictionary key and is a "
                           "dictionary derivative"));
        set(meta, "contains",
            QStringLiteral("n-gram model tables and canonical key sequences only; no corpus or evaluation "
                           "text"));
        set(meta, "protocolSha256", protocol.sha256);
        set(meta, "trainingManifestSha256", manifest.manifestSha256);
        QJsonObject sources;
        for (const DetectorLanguage language : languages) {
            const ex::ManifestSource &s = manifest.sources[std::size_t(language)];
            QJsonObject j;
            set(j, "sha256", s.sha256);
            set(j, "notice", s.notice);
            set(j, "noticeSha256", s.noticeSha256);
            set(sources, languageName(language), j);
        }
        set(meta, "trainingSources", sources);
        QJsonObject files;
        QJsonObject modelFile;
        set(modelFile, "file", QString::fromLatin1(kModelArtifact));
        set(modelFile, "format", QStringLiteral("RSKN v1 (NgramModel::serialize)"));
        set(modelFile, "bytes", qint64(modelBytes.size()));
        set(modelFile, "sha256", ex::sha256Hex(modelBytes));
        set(files, "model", modelFile);
        QJsonObject lexiconFile;
        set(lexiconFile, "file", QString::fromLatin1(kLexiconArtifact));
        set(lexiconFile, "format", QStringLiteral("RSKP v1 pair of RSKF v1 front-coded lexicons, block size 16"));
        set(lexiconFile, "bytes", qint64(fullPairBytes.size()));
        set(lexiconFile, "sha256", ex::sha256Hex(fullPairBytes));
        set(files, "exactFullLexicons", lexiconFile);
        set(meta, "artifacts", files);
        const QByteArray metaBytes = QJsonDocument(meta).toJson(QJsonDocument::Indented);
        if (!writeArtifactFile(artifactDir, QString::fromLatin1(kModelArtifact), modelBytes, error)
            || !writeArtifactFile(artifactDir, QString::fromLatin1(kLexiconArtifact), fullPairBytes, error)
            || !writeArtifactFile(artifactDir, QString::fromLatin1(kMetadataArtifact), metaBytes, error))
            return fail(QStringLiteral("--artifact-dir: ") + error, kExitOutput);
        set(artifactsJson, "files", files);
        set(artifactsJson, "metadataSha256", ex::sha256Hex(metaBytes));
    }

    // ---- 12. Отчёт ----
    QJsonObject sourcesJson;
    QJsonObject trainingJson;
    set(trainingJson, "manifestSha256", manifest.manifestSha256);
    set(trainingJson, "status", manifest.status);
    set(trainingJson, "scope", manifest.scope);
    set(trainingJson, "upstreamCommit", manifest.commit);
    QJsonObject evaluationJson;
    set(evaluationJson, "manifestSha256", evalManifest.manifestSha256);
    set(evaluationJson, "status", evalManifest.status);
    set(evaluationJson, "scope", evalManifest.scope);
    set(evaluationJson, "releaseApproved", false);
    for (const DetectorLanguage language : languages) {
        const std::size_t li = std::size_t(language);
        QJsonObject t;
        set(t, "sha256", manifest.sources[li].sha256);
        set(t, "bytes", manifest.sources[li].bytes);
        set(t, "noticeSha256", manifest.sources[li].noticeSha256);
        set(trainingJson, languageName(language), t);
        QJsonObject e;
        set(e, "isoLanguage", evalManifest.sources[li].isoLanguage);
        set(e, "sha256", evalManifest.sources[li].sha256);
        set(e, "bytes", evalManifest.sources[li].bytes);
        set(e, "noticeSha256", evalManifest.sources[li].noticeSha256);
        set(evaluationJson, languageName(language), e);
    }
    set(sourcesJson, "training", trainingJson);
    set(sourcesJson, "evaluation", evaluationJson);
    set(sourcesJson, "verified",
        QStringLiteral("protocol, both manifests, all source sizes and SHA-256 and all notice SHA-256 were "
                       "verified before any parsing; all four source hashes are distinct; sentence IDs "
                       "do not overlap across languages"));

    QJsonObject protocolJson;
    set(protocolJson, "sha256", protocol.sha256);
    set(protocolJson, "validation", QStringLiteral("exact deep match with the frozen protocol document"));
    QJsonObject thresholdsJson;
    set(thresholdsJson, "singleDGap", kSingleDGap);
    set(thresholdsJson, "twoScoreGap", kTwoScoreGap);
    set(thresholdsJson, "twoScoreMinAlternative", kTwoScoreMinAlternative);
    set(thresholdsJson, "twoScoreMaxObserved", kTwoScoreMaxObserved);
    set(thresholdsJson, "calibration", QStringLiteral("none; fixed by protocol from experiment 001"));
    set(protocolJson, "thresholds", thresholdsJson);

    QJsonObject hypothesesJson;
    set(hypothesesJson, "H1",
        QStringLiteral("compare singleD/twoScore recovered vs exactTrain/exactFull in methods.*.occurrences."
                       "byFullMembership.neither and byTrainMembership.neither, with unsafe changes"));
    set(hypothesesJson, "H2", QStringLiteral("pairwise.exactFullThenTwoScore.vsExactFull"));
    set(hypothesesJson, "H3",
        QStringLiteral("pairwise.twoScoreConfirmedByExactFull.vsExactFull and its lost recoveries"));
    set(hypothesesJson, "H4",
        QStringLiteral("challenge.*.methods singleD vs twoScore changes, pairwise.singleD.vsTwoScore, and "
                       "natural recoveries"));
    set(hypothesesJson, "verdict",
        QStringLiteral("not computed by this tool; the result report states supported/refuted/inconclusive"));

    QJsonObject timings;
    set(timings, "prepareMs", prepareMs);
    set(timings, "trainingMs", trainingMs);
    set(timings, "evaluationMs", evaluationMs);
    set(timings, "challengeMs", challengeMs);
    set(timings, "latencyMs", latencyMs);
    set(timings, "totalMs", totalTimer.elapsed());

    QJsonObject root;
    set(root, "experiment", QStringLiteral("SYS-233 experiment 002 independent detector evaluation"));
    set(root, "scope",
        QStringLiteral("local evaluation only; aggregate counts, hashes, sizes and timings; no words, "
                       "sentences, IDs or paths; automatic input stays disabled"));
    set(root, "protocol", protocolJson);
    set(root, "sources", sourcesJson);
    set(root, "dictionaries", dictionariesJson);
    set(root, "selection", selectionJson);
    set(root, "tokenization", tokenizationJson);
    set(root, "naturalTokens", naturalJson);
    set(root, "methods", methodsJson);
    set(root, "pairwise", pairwiseJson);
    set(root, "challenge", challengeJson);
    set(root, "latency", latencyJsonRoot);
    set(root, "resources", resourcesJson);
    set(root, "artifacts", artifactsJson);
    set(root, "hypothesisEvidence", hypothesesJson);
    set(root, "definitions",
        QStringLiteral("valid = natural token, intent Keep, native layout; wrong = synthetic copy of a "
                       "native letter-only token typed in the other layout; ambiguous = canonical key in "
                       "both full lexicons, reported only in ambiguousValid/ambiguousWrong and any change "
                       "there is unsafe; length = canonical keys if supported, else UTF-16 length; unique "
                       "types = first occurrence per (language, role, canonical keys) or exact text when "
                       "unsupported; membership is relative to the case's typed layout"));
    set(root, "limitations",
        QStringLiteral("Tatoeba CC0 example sentences selected by the protocol, not an ordinary user input "
                       "distribution; exact lexicons are built from the same dictionary source as training, "
                       "not from the evaluation source, and evaluation words are not forced into them, so "
                       "literal overlap with evaluation tokens is expected and measured, not guaranteed; "
                       "no affix expansion and no optimized FST baseline; repeated words are not "
                       "independent trials and no statistical confidence is claimed; oracle token "
                       "boundaries and case-preserving synthetic errors"));
    set(root, "timings", timings);

    if (!writeStdout(root))
        return fail(QStringLiteral("cannot write stdout"), kExitOutput);
    // sink только предотвращает удаление измеряемых вызовов оптимизатором.
    static_cast<void>(sink);
    return 0;
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    const QStringList args = QCoreApplication::arguments();

    // Режим пробы разбирается первым и не допускает опций оценки.
    if (args.contains(QStringLiteral("--probe")))
        return runProbe(args);

    Options opt;
    bool json = false;
    QString error;
    if (!parseOptions(args,
                      {{"--manifest", &opt.manifest},
                       {"--en", &opt.en},
                       {"--ru", &opt.ru},
                       {"--eval-manifest", &opt.evalManifest},
                       {"--eval-en", &opt.evalEn},
                       {"--eval-ru", &opt.evalRu},
                       {"--protocol", &opt.protocol},
                       {"--artifact-dir", &opt.artifactDir}},
                      json, error)) {
        printUsage();
        return fail(error, kExitUsage);
    }
    if (opt.manifest.isEmpty() || opt.en.isEmpty() || opt.ru.isEmpty() || opt.evalManifest.isEmpty()
        || opt.evalEn.isEmpty() || opt.evalRu.isEmpty() || opt.protocol.isEmpty() || !json)
        return usageError("--manifest, --en, --ru, --eval-manifest, --eval-en, --eval-ru, --protocol and "
                          "--json are required");
    return runBenchmark(opt);
}
