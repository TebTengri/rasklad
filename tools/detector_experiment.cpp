// Offline-эксперимент SYS-232: обучение, калибровка и проверка n-граммного
// scorer'а ошибочной раскладки на словарных типах слов.
//
//   detector_experiment --manifest data/sources.json --en <en.dic> --ru <ru.dic> --json
//
// До разбора словарей проверяется манифест: схема, статус, scope, запрет
// выпуска, точный размер и SHA-256 переданных файлов, SHA-256 уведомлений.
// При любом несоответствии программа завершается с ошибкой, не разбирая
// словари и не выводя их содержимое.
//
// stdout — только агрегированный JSON: счётчики, хеши, пороги, размеры и
// времена. Пути, исходные или ошибочно распознанные слова не выводятся.
// stderr — ошибки без содержимого файлов. Программа читает только явно
// переданные файлы и уведомления из манифеста, ничего не пишет на диск,
// не использует сеть, не запускает процессы. Модель сериализуется только в
// памяти для проверки формата и размеров.
//
// Коды выхода: 0 — успех, 1 — отказ входных данных/манифеста,
// 2 — неверные аргументы, 3 — нарушена целостность разделения или модели.

#include "detectordataset.h"
#include "detectormanifest.h"
#include "layoutconverter.h"
#include "ngramdetector.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QTextStream>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <functional>
#include <initializer_list>
#include <optional>
#include <utility>
#include <vector>

using namespace rasklad;
using namespace rasklad::experiment;

namespace {

constexpr int kLatencySampleMax = 2000;
constexpr int kLatencyRepeats = 32;
constexpr int kExitInput = 1;
constexpr int kExitUsage = 2;
constexpr int kExitIntegrity = 3;

void set(QJsonObject &object, const char *key, const QJsonValue &value)
{
    object.insert(QString::fromLatin1(key), value);
}

QJsonValue ratio(qint64 numerator, qint64 denominator)
{
    if (denominator == 0)
        return QJsonValue(QJsonValue::Null);
    return QJsonValue(double(numerator) / double(denominator));
}

int fail(const QString &message, int code = kExitInput)
{
    QTextStream(stderr) << "detector_experiment: " << message << '\n';
    return code;
}

// Чтение обычного файла с ограничением размера. Ошибка чтения, превышение
// предела или изменение размера во время чтения — отказ. Сообщение не
// содержит ни пути, ни данных файла.
bool readLimited(const QString &path, qint64 limit, QByteArray &out, QString &error)
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

// Уведомление ищется только внутри корня репозитория (родителя каталога
// манифеста); canonical-путь защищает от выхода через symlink.
bool verifyNotice(const QString &manifestPath, const ManifestSource &source, QString &error)
{
    const QString resolved = resolveNoticePath(manifestPath, source.notice);
    if (resolved.isEmpty()) {
        error = QStringLiteral("notice path is not inside the repository");
        return false;
    }
    const QString rootCanonical = QFileInfo(manifestRoot(manifestPath)).canonicalFilePath();
    const QString noticeCanonical = QFileInfo(resolved).canonicalFilePath();
    if (rootCanonical.isEmpty() || noticeCanonical.isEmpty()
        || !noticeCanonical.startsWith(rootCanonical + QLatin1Char('/'))) {
        error = QStringLiteral("notice file is missing or outside the repository");
        return false;
    }
    QByteArray bytes;
    if (!readLimited(noticeCanonical, kMaxNoticeBytes, bytes, error)) {
        error = QStringLiteral("notice: ") + error;
        return false;
    }
    if (sha256Hex(bytes) != source.noticeSha256) {
        error = QStringLiteral("notice SHA-256 does not match manifest");
        return false;
    }
    return true;
}

SourceLayout layoutOf(DetectorLanguage language)
{
    return language == DetectorLanguage::En ? SourceLayout::Us : SourceLayout::Ru;
}

SourceLayout otherLayout(SourceLayout layout)
{
    return layout == SourceLayout::Us ? SourceLayout::Ru : SourceLayout::Us;
}

LayoutDirection correctionDirection(SourceLayout source)
{
    return source == SourceLayout::Us ? LayoutDirection::UsToRu : LayoutDirection::RuToUs;
}

const char *languageName(DetectorLanguage language)
{
    return language == DetectorLanguage::En ? "en" : "ru";
}

// Слово словаря после фильтрации и канонизации.
struct Entry {
    QString word;
    KeySequence keys;
    SplitPart part = SplitPart::Train;
    bool ambiguous = false;
};

struct LanguageData {
    DicParseResult parsed;
    std::vector<Entry> entries;
    qint64 canonicalFailures = 0;
};

// Токен оценки, созданный после разделения: исходное слово (valid) или его
// копия, набранная в другой раскладке (wrong). Раскладка указана явно.
struct EvalToken {
    QString text;
    QString intended;
    SourceLayout layout = SourceLayout::Us;
    bool wrong = false;
    bool ambiguous = false;
    CanonicalToken canonical;
    TokenScore score;
};

using Policy = std::function<DecisionAction(const EvalToken &)>;

struct Metrics {
    qint64 validCount = 0;
    qint64 validKeep = 0;
    qint64 validAbstain = 0;
    qint64 falseCorrections = 0;
    qint64 wrongCount = 0;
    qint64 recovered = 0;
    qint64 wrongCorrections = 0;
    qint64 wrongKeep = 0;
    qint64 wrongAbstain = 0;
    qint64 unsupported = 0;

    void add(const EvalToken &token, DecisionAction action)
    {
        if (token.canonical.status != CanonicalStatus::Ok)
            ++unsupported;
        if (!token.wrong) {
            ++validCount;
            if (action == DecisionAction::Keep)
                ++validKeep;
            else if (action == DecisionAction::Abstain)
                ++validAbstain;
            else
                ++falseCorrections;
            return;
        }
        ++wrongCount;
        if (action == DecisionAction::Keep) {
            ++wrongKeep;
        } else if (action == DecisionAction::Abstain) {
            ++wrongAbstain;
        } else {
            // Проверяем фактический результат преобразования, а не только знак решения.
            const QString corrected = convertLayout(token.text, correctionDirection(token.layout));
            if (corrected == token.intended)
                ++recovered;
            else
                ++wrongCorrections;
        }
    }

    QJsonObject toJson() const
    {
        QJsonObject o;
        set(o, "validCount", validCount);
        set(o, "validKeep", validKeep);
        set(o, "validAbstain", validAbstain);
        set(o, "falseCorrections", falseCorrections);
        set(o, "wrongCount", wrongCount);
        set(o, "recovered", recovered);
        set(o, "wrongCorrections", wrongCorrections);
        set(o, "wrongKeep", wrongKeep);
        set(o, "wrongAbstain", wrongAbstain);
        set(o, "unsupported", unsupported);
        set(o, "precision", ratio(recovered, recovered + falseCorrections + wrongCorrections));
        set(o, "correctionRecall", ratio(recovered, wrongCount));
        set(o, "abstainRate", ratio(validAbstain + wrongAbstain, validCount + wrongCount));
        return o;
    }
};

// Токены с ключом, который есть в обоих словарях. Любое Correct на них —
// небезопасное изменение (safety negative), восстановлением не считается.
struct AmbiguousMetrics {
    qint64 count = 0;
    qint64 keep = 0;
    qint64 abstain = 0;
    qint64 changed = 0;
    qint64 len4plusCount = 0;
    qint64 len4plusChanged = 0;

    void add(DecisionAction action, bool len4plus)
    {
        ++count;
        if (len4plus)
            ++len4plusCount;
        if (action == DecisionAction::Keep) {
            ++keep;
        } else if (action == DecisionAction::Abstain) {
            ++abstain;
        } else {
            ++changed;
            if (len4plus)
                ++len4plusChanged;
        }
    }

    QJsonObject toJson() const
    {
        QJsonObject o;
        set(o, "count", count);
        set(o, "keep", keep);
        set(o, "abstain", abstain);
        set(o, "changed", changed);
        set(o, "len4plusCount", len4plusCount);
        set(o, "len4plusChanged", len4plusChanged);
        return o;
    }
};

int lengthOf(const EvalToken &token)
{
    return token.canonical.status == CanonicalStatus::Ok ? int(token.canonical.keys.size())
                                                         : int(token.text.size());
}

QJsonObject evaluate(const std::vector<EvalToken> &tokens, const Policy &policy)
{
    Metrics overall;
    Metrics typedInUs;
    Metrics typedInRu;
    Metrics shortTokens;
    Metrics longTokens;
    AmbiguousMetrics ambiguous;
    for (const EvalToken &token : tokens) {
        const DecisionAction action = policy(token);
        const bool len4plus = lengthOf(token) >= kMinDecisionKeys;
        if (token.ambiguous) {
            ambiguous.add(action, len4plus);
            continue;
        }
        overall.add(token, action);
        (token.layout == SourceLayout::Us ? typedInUs : typedInRu).add(token, action);
        (len4plus ? longTokens : shortTokens).add(token, action);
    }

    QJsonObject o = overall.toJson();
    // Безопасность в первую очередь: изменения неоднозначных токенов видны
    // рядом с ложными исправлениями, а не только во вложенном объекте.
    set(o, "ambiguousChanged", ambiguous.changed);
    set(o, "unsafeChangesIncludingAmbiguous",
        overall.falseCorrections + overall.wrongCorrections + ambiguous.changed);
    set(o, "ambiguousSafetyNegatives", ambiguous.toJson());
    QJsonObject byLayout;
    set(byLayout, "typedInUs", typedInUs.toJson());
    set(byLayout, "typedInRu", typedInRu.toJson());
    set(byLayout, "note",
        QStringLiteral("typedInUs: valid = EN words, wrong = RU words typed in us; "
                       "typedInRu: valid = RU words, wrong = EN words typed in ru; ambiguous tokens "
                       "are reported only in ambiguousSafetyNegatives"));
    set(o, "bySourceLayout", byLayout);
    QJsonObject byLength;
    set(byLength, "len1to3", shortTokens.toJson());
    set(byLength, "len4plus", longTokens.toJson());
    set(o, "byLength", byLength);
    return o;
}

// Точное членство в словаре. Тот же порог длины, что у n-граммных политик:
// токены короче kMinDecisionKeys воздерживаются, сравнение честное.
DecisionAction exactDecision(const EvalToken &token,
                             const std::array<QSet<KeySequence>, 2> &dictionary)
{
    if (token.canonical.status != CanonicalStatus::Ok
        || token.canonical.keys.size() < kMinDecisionKeys)
        return DecisionAction::Abstain;
    const auto &observed = dictionary[std::size_t(observedLanguage(token.layout))];
    const auto &alternative = dictionary[std::size_t(alternativeLanguage(token.layout))];
    if (observed.contains(token.canonical.keys))
        return DecisionAction::Keep;
    if (alternative.contains(token.canonical.keys))
        return DecisionAction::Correct;
    return DecisionAction::Abstain;
}

// Счётчики подбора порогов. Неоднозначные токены входят как safety
// negatives: их Correct — ложное исправление, а не восстановление.
struct GridCounts {
    qint64 falseCorrections = 0;
    qint64 ambiguousCorrections = 0;
    qint64 recovered = 0;
};

template <typename Decide>
GridCounts countGrid(const std::vector<const EvalToken *> &tokens, Decide decide)
{
    GridCounts counts;
    for (const EvalToken *token : tokens) {
        if (decide(*token) != DecisionAction::Correct)
            continue;
        if (token->ambiguous) {
            ++counts.ambiguousCorrections;
            ++counts.falseCorrections;
        } else if (token->wrong) {
            ++counts.recovered;
        } else {
            ++counts.falseCorrections;
        }
    }
    return counts;
}

QJsonObject gridCountsJson(const GridCounts &c)
{
    QJsonObject o;
    set(o, "falseCorrectionsIncludingAmbiguous", c.falseCorrections);
    set(o, "ambiguousCorrections", c.ambiguousCorrections);
    set(o, "recovered", c.recovered);
    return o;
}

double percentile(const std::vector<double> &sorted, double p)
{
    if (sorted.empty())
        return 0.0;
    // Nearest-rank.
    const auto rank = std::size_t(std::ceil(p / 100.0 * double(sorted.size())));
    return sorted[std::min(sorted.size() - 1, rank == 0 ? 0 : rank - 1)];
}

QJsonObject dictionarySizes(const QStringList &words)
{
    QByteArray joined = words.join(QLatin1Char('\n')).toUtf8();
    if (!joined.isEmpty())
        joined.append('\n');
    QJsonObject o;
    set(o, "types", qint64(words.size()));
    set(o, "filteredUtf8Bytes", qint64(joined.size()));
    set(o, "qCompressLevel9Bytes", qint64(qCompress(joined, 9).size()));
    return o;
}

void printUsage()
{
    QTextStream(stderr)
        << "usage: detector_experiment --manifest <sources.json> --en <en.dic> --ru <ru.dic> --json\n";
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    QString manifestPath;
    QString enPath;
    QString ruPath;
    bool json = false;
    const QStringList args = QCoreApplication::arguments();
    for (qsizetype i = 1; i < args.size(); ++i) {
        const QString &arg = args.at(i);
        if (arg == QLatin1String("--json")) {
            json = true;
            continue;
        }
        QString *target = nullptr;
        if (arg == QLatin1String("--en"))
            target = &enPath;
        else if (arg == QLatin1String("--ru"))
            target = &ruPath;
        else if (arg == QLatin1String("--manifest"))
            target = &manifestPath;
        if (!target || i + 1 >= args.size()) {
            printUsage();
            return fail(QStringLiteral("unknown or incomplete option"), kExitUsage);
        }
        if (!target->isEmpty()) {
            printUsage();
            return fail(QStringLiteral("duplicate option"), kExitUsage);
        }
        *target = args.at(++i);
        if (target->isEmpty()) {
            printUsage();
            return fail(QStringLiteral("empty option value"), kExitUsage);
        }
    }
    if (manifestPath.isEmpty() || enPath.isEmpty() || ruPath.isEmpty() || !json) {
        printUsage();
        return fail(QStringLiteral("--manifest, --en, --ru and --json are required"), kExitUsage);
    }

    QElapsedTimer prepareTimer;
    prepareTimer.start();

    // ---- Манифест: до разбора словарей и обучения ----
    QByteArray manifestBytes;
    QString readError;
    if (!readLimited(manifestPath, kMaxManifestBytes, manifestBytes, readError))
        return fail(QStringLiteral("--manifest: ") + readError);
    const ManifestParseResult manifestResult = parseExperimentManifest(manifestBytes);
    if (manifestResult.error != ManifestError::None)
        return fail(QStringLiteral("--manifest: ") + manifestErrorText(manifestResult.error));
    const ExperimentManifest &manifest = manifestResult.manifest;

    const std::array<DetectorLanguage, 2> languages{{DetectorLanguage::En, DetectorLanguage::Ru}};
    const std::array<QString, 2> paths{{enPath, ruPath}};
    std::array<QByteArray, 2> raw;
    for (const DetectorLanguage language : languages) {
        const std::size_t li = std::size_t(language);
        const QString option = QStringLiteral("--") + QString::fromLatin1(languageName(language));
        QString error;
        if (!readLimited(paths[li], kMaxDicBytes, raw[li], error))
            return fail(option + QStringLiteral(": ") + error);
        const ManifestSource &source = manifest.sources[li];
        if (!bytesMatch(raw[li], source.bytes, source.sha256)) {
            raw[li].clear();
            return fail(option + QStringLiteral(": file size or SHA-256 does not match manifest"));
        }
        if (!verifyNotice(manifestPath, source, error))
            return fail(option + QStringLiteral(": ") + error);
    }

    QJsonObject manifestJson;
    set(manifestJson, "sha256", manifest.manifestSha256);
    set(manifestJson, "schemaVersion", 1);
    set(manifestJson, "status", manifest.status);
    set(manifestJson, "scope", manifest.scope);
    set(manifestJson, "releaseApproved", false);
    set(manifestJson, "upstreamCommit", manifest.commit);
    set(manifestJson, "sourcesVerified", true);
    set(manifestJson, "noticesVerified", true);
    set(manifestJson, "note",
        QStringLiteral("source bytes and SHA-256 and notice SHA-256 matched the manifest before any "
                       "parsing or training; results are limited to the manifest scope"));

    // ---- Загрузка и фильтрация ----
    std::array<LanguageData, 2> data;
    QJsonObject dataJson;
    for (const DetectorLanguage language : languages) {
        const std::size_t li = std::size_t(language);
        const QString option = QStringLiteral("--") + QString::fromLatin1(languageName(language));
        LanguageData &ld = data[li];
        ld.parsed = parseHunspellDic(raw[li], language);
        if (ld.parsed.error != DicError::None)
            return fail(option + QStringLiteral(": ") + dicErrorText(ld.parsed.error));

        for (const QString &word : std::as_const(ld.parsed.words)) {
            const CanonicalToken canonical = canonicalizeToken(word, layoutOf(language));
            if (canonical.status != CanonicalStatus::Ok) {
                ++ld.canonicalFailures;
                continue;
            }
            ld.entries.push_back({word, canonical.keys, splitForKeys(canonical.keys), false});
        }

        const DicFilterReport &r = ld.parsed.report;
        QJsonObject j;
        set(j, "fileSha256", sha256Hex(raw[li]));
        set(j, "rawBytes", r.rawBytes);
        set(j, "bomStripped", r.bomStripped);
        set(j, "headerCount", r.headerCount);
        set(j, "entryLines", r.entryLines);
        set(j, "emptyLines", r.emptyLines);
        set(j, "crlfLines", r.crlfLines);
        set(j, "rejectedNotLetters", r.rejectedNotLetters);
        set(j, "rejectedLength", r.rejectedLength);
        set(j, "rejectedMultiword", r.rejectedMultiword);
        set(j, "duplicatesAfterLowercase", r.duplicates);
        set(j, "acceptedTypes", r.accepted);
        set(j, "canonicalFailures", ld.canonicalFailures);
        set(j, "filteredListSha256",
            sha256Hex(ld.parsed.words.join(QLatin1Char('\n')).toUtf8()));
        set(dataJson, languageName(language), j);
        raw[li].clear();
    }
    set(dataJson, "processing",
        QStringLiteral("Hunspell .dic stems only: count header must equal record count, /flags "
                       "stripped, affixes NOT expanded, .aff and corpus frequencies NOT used; each "
                       "lowercase type counted once"));

    // ---- Неоднозначность и разделение ----
    std::array<QSet<KeySequence>, 2> fullKeys;
    for (const DetectorLanguage language : languages) {
        for (const Entry &e : data[std::size_t(language)].entries)
            fullKeys[std::size_t(language)].insert(e.keys);
    }
    for (LanguageData &ld : data) {
        for (Entry &e : ld.entries)
            e.ambiguous = fullKeys[0].contains(e.keys) && fullKeys[1].contains(e.keys);
    }

    std::array<std::vector<SplitEntry>, 2> splitEntries;
    std::array<std::array<QSet<KeySequence>, 3>, 2> keysBySplit;
    std::array<std::array<qint64, 3>, 2> typesBySplit{};
    std::array<std::array<qint64, 3>, 2> ambiguousBySplit{};
    std::vector<KeySequence> trainSequences[2];
    for (const DetectorLanguage language : languages) {
        const std::size_t li = std::size_t(language);
        for (const Entry &e : data[li].entries) {
            const std::size_t pi = std::size_t(e.part);
            splitEntries[li].push_back({e.word, e.keys, e.part});
            keysBySplit[li][pi].insert(e.keys);
            ++typesBySplit[li][pi];
            if (e.ambiguous)
                ++ambiguousBySplit[li][pi];
            if (e.part == SplitPart::Train)
                trainSequences[li].push_back(e.keys);
        }
    }

    const LeakReport leaks = countSplitLeaks(splitEntries[0], splitEntries[1]);
    if (!leaks.leakFree()) {
        return fail(QStringLiteral("split leakage detected (keys %1, words %2, wrong-layout mappings %3)")
                        .arg(leaks.canonicalKeyLeaks)
                        .arg(leaks.wordLeaks)
                        .arg(leaks.wrongLayoutMappingLeaks),
                    kExitIntegrity);
    }

    QJsonObject splitsJson;
    for (std::size_t pi = 0; pi < 3; ++pi) {
        QJsonObject s;
        set(s, "en", typesBySplit[0][pi]);
        set(s, "ru", typesBySplit[1][pi]);
        set(s, "enAmbiguous", ambiguousBySplit[0][pi]);
        set(s, "ruAmbiguous", ambiguousBySplit[1][pi]);
        set(splitsJson, splitName(SplitPart(pi)), s);
    }
    QJsonObject leaksJson;
    set(leaksJson, "canonicalKeyLeaks", leaks.canonicalKeyLeaks);
    set(leaksJson, "wordLeaks", leaks.wordLeaks);
    set(leaksJson, "wrongLayoutMappingLeaks", leaks.wrongLayoutMappingLeaks);
    set(leaksJson, "leakFree", true);
    set(leaksJson, "policy", QStringLiteral("any leak aborts the run with exit code 3"));
    set(splitsJson, "leaks", leaksJson);
    set(splitsJson, "seed", QString::fromLatin1(kSplitSeed));
    set(splitsJson, "rule",
        QStringLiteral("SHA-256(seed || 0x00 || first 3 canonical keys), first 8 bytes big-endian mod "
                       "100: [0,70) train, [70,85) calibration, [85,100) test; split happens before "
                       "wrong-layout augmentation; one group is in one split for both languages"));
    set(splitsJson, "groupingLimitation",
        QStringLiteral("prefix-3 grouping is a proxy for morphological families, not proven lemma "
                       "disjointness"));

    qint64 ambiguousKeys = 0;
    for (const KeySequence &k : std::as_const(fullKeys[0])) {
        if (fullKeys[1].contains(k))
            ++ambiguousKeys;
    }
    QJsonObject ambiguityJson;
    set(ambiguityJson, "ambiguousKeyCount", ambiguousKeys);
    set(ambiguityJson, "calibrationRole",
        QStringLiteral("ambiguous tokens are SAFETY NEGATIVES: in threshold selection any Correct on "
                       "an ambiguous token counts as a false correction and never as recovered; they "
                       "are never forced positives"));
    set(ambiguityJson, "evaluationRole",
        QStringLiteral("reported per method as ambiguousChanged, unsafeChangesIncludingAmbiguous and "
                       "ambiguousSafetyNegatives; excluded from valid/wrong precision and recall"));
    set(ambiguityJson, "oracleLimitation",
        QStringLiteral("labels come from membership in the FULL source dictionaries, i.e. oracle "
                       "metadata a deployed detector does not have. Because split groups are "
                       "key-prefix based, an ambiguous key lies in one split for both languages, so "
                       "the label is also derivable from that split alone; ambiguity against words "
                       "absent from both dictionaries is not detected"));
    set(ambiguityJson, "training", QStringLiteral("ambiguous train types are kept in both languages"));

    const qint64 prepareMs = prepareTimer.elapsed();

    // ---- Обучение ----
    QElapsedTimer trainTimer;
    trainTimer.start();
    qsizetype skipped = 0;
    const NgramModel model = NgramModel::train(trainSequences[0], trainSequences[1], &skipped);
    const qint64 trainMs = trainTimer.elapsed();
    if (skipped != 0)
        return fail(QStringLiteral("training skipped %1 invalid sequences").arg(qint64(skipped)),
                    kExitIntegrity);
    if (!model.isValid())
        return fail(QStringLiteral("trained model is invalid"), kExitIntegrity);

    // Сериализация только в памяти: проверка формата и размеров.
    const QByteArray serialized = model.serialize();
    const std::optional<NgramModel> restored = NgramModel::deserialize(serialized);
    const bool roundTrip = serialized.size() == NgramModel::kSerializedBytes && restored.has_value()
        && restored->table(DetectorLanguage::En) == model.table(DetectorLanguage::En)
        && restored->table(DetectorLanguage::Ru) == model.table(DetectorLanguage::Ru)
        && restored->serialize() == serialized;
    if (!roundTrip)
        return fail(QStringLiteral("model serialization round trip failed"), kExitIntegrity);

    QJsonObject modelJson;
    set(modelJson, "formula",
        QStringLiteral("For language L and keys k1..kn, sequence BOS BOS k1..kn EOS. For each predicted "
                       "symbol c with context (a,b): Q_L = sum over orders of w_n * round(8 * ln P_n), "
                       "P_1(c) = (N(c)+0.5)/(N+35*0.5), P_2(c|b) = (N(b,c)+0.5)/(N(b)+35*0.5), "
                       "P_3(c|a,b) = (N(a,b,c)+0.5)/(N(a,b)+35*0.5), each clamped to [-127,0]; "
                       "w = (1,1,1). S_L = sum Q_L / (8 * (n+1)). S_L is a weighted log-score, "
                       "not a calibrated probability."));
    set(modelJson, "alphabet", NgramModel::kAlphabet);
    set(modelJson, "alpha", NgramModel::kAlpha);
    set(modelJson, "scale", NgramModel::kScale);
    set(modelJson, "formatVersion", qint64(NgramModel::kFormatVersion));
    set(modelJson, "accumulator", QStringLiteral("int32, worst case 33*3*127"));
    set(modelJson, "trainingTypesEn", qint64(trainSequences[0].size()));
    set(modelJson, "trainingTypesRu", qint64(trainSequences[1].size()));
    set(modelJson, "skippedTrainingSequences", qint64(skipped));
    set(modelJson, "serializationRoundTrip", roundTrip);
    set(modelJson, "artifact", QStringLiteral("serialized in memory only; no model file is written"));
    set(modelJson, "shortTokenPolicy",
        QStringLiteral("tokens with fewer than 4 keys always abstain in n-gram and exact policies"));

    // ---- Токены оценки (после разделения) ----
    auto buildTokens = [&](SplitPart part) {
        std::vector<EvalToken> tokens;
        for (const DetectorLanguage language : languages) {
            for (const Entry &e : data[std::size_t(language)].entries) {
                if (e.part != part)
                    continue;
                const SourceLayout home = layoutOf(language);
                const SourceLayout away = otherLayout(home);
                EvalToken valid;
                valid.text = e.word;
                valid.intended = e.word;
                valid.layout = home;
                valid.ambiguous = e.ambiguous;
                EvalToken wrong = valid;
                wrong.text = convertLayout(e.word, home == SourceLayout::Us ? LayoutDirection::UsToRu
                                                                            : LayoutDirection::RuToUs);
                wrong.layout = away;
                wrong.wrong = true;
                for (EvalToken *t : {&valid, &wrong}) {
                    t->canonical = canonicalizeToken(t->text, t->layout);
                    t->score = model.score(t->canonical.keys);
                }
                tokens.push_back(std::move(valid));
                tokens.push_back(std::move(wrong));
            }
        }
        return tokens;
    };

    // ---- Калибровка ----
    QElapsedTimer calibrationTimer;
    calibrationTimer.start();
    const std::vector<EvalToken> calibration = buildTokens(SplitPart::Calibration);
    // Все токены длины >= 4, включая неоднозначные (как safety negatives).
    std::vector<const EvalToken *> eligible;
    qint64 eligibleAmbiguous = 0;
    for (const EvalToken &t : calibration) {
        if (t.score.ok && t.score.keyCount >= kMinDecisionKeys) {
            eligible.push_back(&t);
            if (t.ambiguous)
                ++eligibleAmbiguous;
        }
    }

    // Порядок перебора — от строгих порогов к мягким; при равном числе
    // восстановленных токенов остаётся первый (более строгий) вариант.
    SingleDThresholds bestSingle;
    GridCounts bestSingleCounts;
    int singleGrid = 0;
    for (int i = 32; i >= 0; --i) {
        const SingleDThresholds t{true, 0.25 * i};
        ++singleGrid;
        const GridCounts c = countGrid(eligible, [&](const EvalToken &tok) {
            return decideSingleD(tok.score, tok.layout, t).action;
        });
        if (c.falseCorrections == 0 && c.recovered > bestSingleCounts.recovered) {
            bestSingle = t;
            bestSingleCounts = c;
        }
    }

    TwoScoreThresholds bestTwo;
    GridCounts bestTwoCounts;
    int twoGrid = 0;
    for (int g = 8; g >= 0; --g) {
        for (int minAlt = -2; minAlt >= -24; --minAlt) {
            for (int maxObs = -24; maxObs <= -2; ++maxObs) {
                const TwoScoreThresholds t{true, double(minAlt), double(maxObs), 0.5 * g};
                ++twoGrid;
                const GridCounts c = countGrid(eligible, [&](const EvalToken &tok) {
                    return decideTwoScore(tok.score, tok.layout, t).action;
                });
                if (c.falseCorrections == 0 && c.recovered > bestTwoCounts.recovered) {
                    bestTwo = t;
                    bestTwoCounts = c;
                }
            }
        }
    }

    const Policy singlePolicy = [&](const EvalToken &t) {
        return decideSingleD(t.score, t.layout, bestSingle).action;
    };
    const Policy twoPolicy = [&](const EvalToken &t) {
        return decideTwoScore(t.score, t.layout, bestTwo).action;
    };

    QJsonObject calibrationJson;
    set(calibrationJson, "selectionRule",
        QStringLiteral("maximize recovered non-ambiguous wrong-layout tokens subject to zero false "
                       "corrections on calibration tokens, where any Correct on an ambiguous token "
                       "is a false correction; ties keep the stricter thresholds; if no setting "
                       "recovers anything, the policy is disabled (always abstain). Test split is not "
                       "used. Thresholds are experiment outputs, not production-safe values."));
    set(calibrationJson, "eligibleTokens", qint64(eligible.size()));
    set(calibrationJson, "eligibleAmbiguousSafetyNegatives", eligibleAmbiguous);
    QJsonObject singleJson;
    set(singleJson, "grid", QStringLiteral("gap = 0.25*i, i = 32..0"));
    set(singleJson, "gridSize", singleGrid);
    set(singleJson, "enabled", bestSingle.enabled);
    set(singleJson, "gap", bestSingle.enabled ? QJsonValue(bestSingle.gap) : QJsonValue());
    set(singleJson, "selectedGridCounts", gridCountsJson(bestSingleCounts));
    set(singleJson, "calibrationCounts", evaluate(calibration, singlePolicy));
    set(calibrationJson, "singleD", singleJson);
    QJsonObject twoJson;
    set(twoJson, "grid",
        QStringLiteral("gap = 0.5*g, g = 8..0; minAlternative = -2..-24; maxObserved = -24..-2 (step 1)"));
    set(twoJson, "gridSize", twoGrid);
    set(twoJson, "enabled", bestTwo.enabled);
    set(twoJson, "minAlternative", bestTwo.enabled ? QJsonValue(bestTwo.minAlternative) : QJsonValue());
    set(twoJson, "maxObserved", bestTwo.enabled ? QJsonValue(bestTwo.maxObserved) : QJsonValue());
    set(twoJson, "gap", bestTwo.enabled ? QJsonValue(bestTwo.gap) : QJsonValue());
    set(twoJson, "selectedGridCounts", gridCountsJson(bestTwoCounts));
    set(twoJson, "calibrationCounts", evaluate(calibration, twoPolicy));
    set(calibrationJson, "twoScore", twoJson);
    const qint64 calibrationMs = calibrationTimer.elapsed();

    // ---- Test: пороги зафиксированы выше ----
    QElapsedTimer testTimer;
    testTimer.start();
    const std::vector<EvalToken> test = buildTokens(SplitPart::Test);

    std::array<QSet<KeySequence>, 2> trainKeys;
    for (std::size_t li = 0; li < 2; ++li)
        trainKeys[li] = keysBySplit[li][std::size_t(SplitPart::Train)];

    QJsonObject methods;
    set(methods, "alwaysKeep", evaluate(test, [](const EvalToken &) { return DecisionAction::Keep; }));
    QJsonObject trainExact = evaluate(test, [&](const EvalToken &t) { return exactDecision(t, trainKeys); });
    set(trainExact, "note",
        QStringLiteral("exact membership in train-split dictionaries only, same >=4-key gate; splits "
                       "are key-disjoint, so test coverage is expected to be (near) zero by "
                       "construction"));
    set(methods, "trainOnlyExact", trainExact);
    QJsonObject fullExact = evaluate(test, [&](const EvalToken &t) { return exactDecision(t, fullKeys); });
    set(fullExact, "note",
        QStringLiteral("reference only: membership in the FULL source dictionaries, which contain the "
                       "test words, same >=4-key gate; advantaged on a dictionary-derived holdout and "
                       "does not show generalization to unseen words. QSet in memory, not an "
                       "optimized FST/DAFSA"));
    set(methods, "fullReferenceExact", fullExact);
    set(methods, "singleD", evaluate(test, singlePolicy));
    set(methods, "twoScore", evaluate(test, twoPolicy));

    QJsonObject testJson;
    set(testJson, "tokenCount", qint64(test.size()));
    set(testJson, "methods", methods);
    set(testJson, "evaluationMix",
        QStringLiteral("balanced synthetic error mix: every test word type yields one valid token and "
                       "one wrong-layout copy with explicit source layout; precision here is NOT "
                       "deployment precision and depends on the artificial 1:1 ratio"));
    set(testJson, "metricDefinitions",
        QStringLiteral("falseCorrections = Correct on non-ambiguous valid; recovered = Correct on "
                       "non-ambiguous wrong whose conversion equals the intended word; "
                       "wrongCorrections = Correct on wrong with different output; ambiguousChanged = "
                       "Correct on any ambiguous token (safety negative); "
                       "unsafeChangesIncludingAmbiguous = falseCorrections + wrongCorrections + "
                       "ambiguousChanged; precision = recovered/(recovered+falseCorrections+"
                       "wrongCorrections), null if denominator is 0; correctionRecall = "
                       "recovered/wrongCount"));
    const qint64 testMs = testTimer.elapsed();

    // ---- Задержка: тёплый прогон на ограниченной детерминированной выборке ----
    std::vector<const EvalToken *> sample;
    const std::size_t stride = std::max<std::size_t>(1, test.size() / kLatencySampleMax);
    for (std::size_t i = 0; i < test.size() && sample.size() < std::size_t(kLatencySampleMax); i += stride)
        sample.push_back(&test[i]);

    volatile qint64 sink = 0;
    auto runOnce = [&](const EvalToken &t) {
        const CanonicalToken c = canonicalizeToken(t.text, t.layout);
        const TokenScore s = model.score(c.keys);
        const DetectorDecision d = decideTwoScore(s, t.layout, bestTwo);
        sink = sink + s.raw[0] + int(d.action);
    };
    for (const EvalToken *t : sample)
        runOnce(*t);
    std::vector<double> perCallNs;
    perCallNs.reserve(sample.size());
    QElapsedTimer callTimer;
    for (const EvalToken *t : sample) {
        callTimer.start();
        for (int r = 0; r < kLatencyRepeats; ++r)
            runOnce(*t);
        perCallNs.push_back(double(callTimer.nsecsElapsed()) / kLatencyRepeats);
    }
    std::sort(perCallNs.begin(), perCallNs.end());

    QJsonObject latency;
    set(latency, "path",
        QStringLiteral("canonicalizeToken + NgramModel::score + decideTwoScore with calibrated thresholds"));
    set(latency, "twoScoreEnabled", bestTwo.enabled);
    set(latency, "sampleRule",
        QStringLiteral("every k-th test token in deterministic order, at most 2000; one warm-up pass"));
    set(latency, "sampleTokens", qint64(sample.size()));
    set(latency, "repeatsPerToken", kLatencyRepeats);
    set(latency, "p50Ns", percentile(perCallNs, 50));
    set(latency, "p95Ns", percentile(perCallNs, 95));
    set(latency, "p99Ns", percentile(perCallNs, 99));
    set(latency, "maxNs", perCallNs.empty() ? 0.0 : perCallNs.back());
    set(latency, "note",
        QStringLiteral("warm, single thread, mean of repeats per token, timer overhead included; "
                       "cold start and RSS are not measured"));

    QJsonObject timings;
    set(timings, "prepareMs", prepareMs);
    set(timings, "trainingMs", trainMs);
    set(timings, "calibrationMs", calibrationMs);
    set(timings, "testMs", testMs);
    set(timings, "warmLatency", latency);

    // ---- Размеры ----
    QJsonObject sizes;
    set(sizes, "weightBytes", NgramModel::kWeightBytes);
    set(sizes, "serializedModelBytes", qint64(serialized.size()));
    set(sizes, "serializedModelQCompressBytes", qint64(qCompress(serialized, 9).size()));
    QJsonObject reference;
    for (const DetectorLanguage language : languages) {
        const std::size_t li = std::size_t(language);
        QStringList trainWords;
        for (const Entry &e : data[li].entries) {
            if (e.part == SplitPart::Train)
                trainWords.append(e.word);
        }
        QJsonObject r;
        set(r, "rawFileBytes", data[li].parsed.report.rawBytes);
        set(r, "full", dictionarySizes(data[li].parsed.words));
        set(r, "trainOnly", dictionarySizes(trainWords));
        set(reference, languageName(language), r);
    }
    set(reference, "boundary",
        QStringLiteral("filteredUtf8Bytes = sorted lowercase types joined by newline; qCompress is a "
                       "storage reference only, not an inference structure and not RSS"));
    set(sizes, "referenceDictionaries", reference);

    QJsonObject root;
    set(root, "experiment", QStringLiteral("SYS-232 offline n-gram wrong-layout detector"));
    set(root, "scope",
        QStringLiteral("local experiment over dictionary word types; not production quality; release "
                       "of data and weights not approved; no artifacts written by this tool"));
    set(root, "manifest", manifestJson);
    set(root, "normalizationVersion", QString::fromLatin1(kNormalizationVersion));
    set(root, "data", dataJson);
    set(root, "splits", splitsJson);
    set(root, "ambiguity", ambiguityJson);
    set(root, "model", modelJson);
    set(root, "calibration", calibrationJson);
    set(root, "test", testJson);
    set(root, "sizes", sizes);
    set(root, "timings", timings);

    QFile out;
    if (!out.open(stdout, QIODevice::WriteOnly))
        return fail(QStringLiteral("cannot write stdout"));
    const QByteArray bytes = QJsonDocument(root).toJson(QJsonDocument::Indented);
    if (out.write(bytes) != bytes.size() || !out.flush())
        return fail(QStringLiteral("cannot write stdout"));
    return 0;
}
