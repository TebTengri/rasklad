// Offline-эксперимент SYS-235 (опыт 003): морфологический контроль Hunspell
// на новом блоке той же выборки Tatoeba с замороженным протоколом.
//
// Режим оценки (все аргументы обязательны):
//   morphology_benchmark --manifest data/sources.json --en <en.dic> --ru <ru.dic>
//       --eval-manifest data/evaluation-002-sources.json --eval-en <eng.tsv>
//       --eval-ru <rus.tsv> --protocol data/experiment-003-protocol.json
//       --morph-manifest data/morphology-003-sources.json --json
//
// Режим пробы ресурсов (разбирается первым; читает только паспорт
// морфологии, уведомления, его .aff/.dic и загруженную библиотеку):
//   morphology_benchmark --probe hunspell|control --morph-manifest <path>
//       --iterations <0..1000000> --json
//
// Порядок оценки: протокол 003 целиком; паспорт словарей и .dic; паспорт
// оценки и TSV; паспорт морфологии, уведомления, загруженная библиотека
// Hunspell (dladdr, размер, SHA-256, версия), .dic/.aff/контрольные .aff
// (размер, SHA-256, UTF-8, контроль = удаление строк PFX/SFX из исходника).
// Только после этого строятся модель и словари опыта 002, создаются движки
// Hunspell и разбираются предложения. Порогов, правил и исключений,
// подобранных на новом блоке, нет.
//
// stdout — только агрегированный JSON после полного успеха. Пути, слова,
// предложения и ID не выводятся. При отказе stdout пуст; stderr — ошибки без
// путей и содержимого. Сеть, процессы и /proc не используются; файлы не
// пишутся.
//
// Коды выхода: 0 — успех; 1 — отказ входных данных; 2 — неверные
// аргументы; 3 — нарушена целостность; 4 — ошибка записи stdout.

#include "benchmarkeval.h"
#include "benchmarkio.h"
#include "benchmarkprotocol.h"
#include "detectordataset.h"
#include "detectormanifest.h"
#include "evaluationdata.h"
#include "frontcodedlexicon.h"
#include "hunspellengine.h"
#include "layoutconverter.h"
#include "morphologydata.h"
#include "morphologyeval.h"
#include "ngramdetector.h"

#include <QCoreApplication>
#include <QCryptographicHash>
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
#include <memory>
#include <optional>
#include <utility>
#include <vector>

using namespace rasklad;
using namespace rasklad::benchmark;
using namespace rasklad::morphology;
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
constexpr int kProbeTokenCount = 64;

// Те же значения, что проверяет detector_benchmark (эксперимент 001).
constexpr std::array<const char *, 2> kExpectedFilteredListSha256{{
    "7b9e9cd50e8d4d24b9139b3df28c6a54cc180d54ba127681424978a00d9d977e",
    "46191cbc6df3c2fb5a0ab63f1808b18fdfe773a15813dd3013230091412d2341",
}};
constexpr std::array<qint64, 2> kExpectedAcceptedTypes{{47828, 146229}};
constexpr std::array<qint64, 2> kExpectedTrainTypes{{34025, 97632}};

const std::array<DetectorLanguage, 2> kLanguages{{DetectorLanguage::En, DetectorLanguage::Ru}};

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
    QTextStream(stderr) << "morphology_benchmark: " << message << '\n';
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
        << "usage: morphology_benchmark --manifest <sources.json> --en <en.dic> --ru <ru.dic>\n"
           "           --eval-manifest <evaluation-002-sources.json> --eval-en <eng.tsv>\n"
           "           --eval-ru <rus.tsv> --protocol <experiment-003-protocol.json>\n"
           "           --morph-manifest <morphology-003-sources.json> --json\n"
           "       morphology_benchmark --probe hunspell|control --morph-manifest <path>\n"
           "           --iterations <0..1000000> --json\n";
}

int usageError(const char *message)
{
    printUsage();
    return fail(QString::fromLatin1(message), kExitUsage);
}

struct OptionSlot {
    const char *name;
    QString *target;
};

// --json — флаг, остальные опции — ровно одно непустое значение.
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

// ---- Проверка морфологических входов ----

struct LanguageFiles {
    QString dic;
    QString aff;
    QString control;
    AffControlResult derivation;
};

struct VerifiedMorphology {
    MorphManifest manifest;
    QString projectRoot;
    LoadedLibrary library;
    std::array<LanguageFiles, 2> files;
};

struct Failure {
    QString message;
    int code = kExitInput;
};

// Потоковая проверка обычного файла: размер <= limit и равен ref.bytes,
// SHA-256 равен ref.sha256, весь файл — строгий UTF-8. Читается блоками по
// 64 KiB, чтобы проба ресурсов не держала весь .dic в памяти; байты
// сохраняются только при keep != nullptr (.aff до 1 MiB).
bool streamVerify(const QString &canonical, qint64 limit, const FileRef &ref, QByteArray *keep, QString &error)
{
    if (keep)
        keep->clear();
    const QFileInfo info(canonical);
    if (!info.exists() || !info.isFile()) {
        error = QStringLiteral("not a regular file");
        return false;
    }
    if (info.size() > limit) {
        error = QStringLiteral("file exceeds size limit");
        return false;
    }
    if (info.size() != ref.bytes) {
        error = QStringLiteral("file size or SHA-256 does not match morphology manifest");
        return false;
    }
    QFile file(canonical);
    if (!file.open(QIODevice::ReadOnly)) {
        error = QStringLiteral("cannot open file");
        return false;
    }
    QCryptographicHash hash(QCryptographicHash::Sha256);
    QByteArray carry;
    qint64 total = 0;
    bool utf8 = true;
    for (;;) {
        const QByteArray chunk = file.read(64 * 1024);
        if (file.error() != QFileDevice::NoError) {
            error = QStringLiteral("read error");
            return false;
        }
        if (chunk.isEmpty())
            break;
        total += chunk.size();
        if (total > ref.bytes) {
            error = QStringLiteral("file size changed while reading");
            return false;
        }
        hash.addData(chunk);
        if (keep)
            keep->append(chunk);
        // Последовательность UTF-8 на границе блока переносится в следующий.
        const QByteArray buffer = carry + chunk;
        qsizetype cut = buffer.size();
        qsizetype i = cut - 1;
        int continuation = 0;
        while (i >= 0 && continuation < 3 && (uchar(buffer.at(i)) & 0xC0U) == 0x80U) {
            --i;
            ++continuation;
        }
        if (i >= 0 && uchar(buffer.at(i)) >= 0xC0U)
            cut = i;
        utf8 = utf8 && isStrictUtf8(buffer.left(cut));
        carry = buffer.mid(cut);
    }
    utf8 = utf8 && isStrictUtf8(carry);
    if (total != ref.bytes || QString::fromLatin1(hash.result().toHex()) != ref.sha256) {
        error = QStringLiteral("file size or SHA-256 does not match morphology manifest");
        return false;
    }
    if (!utf8) {
        error = QStringLiteral("file is not strict UTF-8");
        return false;
    }
    return true;
}

bool readVerified(const QString &projectRoot, const FileRef &ref, qint64 limit, const QString &label,
                  QString &canonical, QByteArray *keep, Failure &failure)
{
    QString error;
    canonical = resolveProjectFile(projectRoot, ref.path, error);
    if (canonical.isEmpty() || !streamVerify(canonical, limit, ref, keep, error)) {
        failure = {label + QStringLiteral(": ") + error, kExitInput};
        return false;
    }
    return true;
}

// Паспорт, уведомления, загруженная библиотека, исходные .aff/.dic и, при
// includeControl, контрольные .aff, сверенные с детерминированным удалением
// строк PFX/SFX. Ничего не передаётся в Hunspell.
bool verifyMorphology(const QString &manifestPath, bool includeControl, VerifiedMorphology &out,
                      Failure &failure)
{
    QString error;
    QByteArray manifestBytes;
    if (!readBoundedFile(manifestPath, kMaxMorphManifestBytes, manifestBytes, error)) {
        failure = {QStringLiteral("--morph-manifest: ") + error, kExitInput};
        return false;
    }
    const MorphManifestResult parsed = parseMorphManifest(manifestBytes);
    if (parsed.error != MorphManifestError::None) {
        failure = {QStringLiteral("--morph-manifest: ") + morphManifestErrorText(parsed.error), kExitInput};
        return false;
    }
    out.manifest = parsed.manifest;
    out.projectRoot = QFileInfo(ex::manifestRoot(manifestPath)).canonicalFilePath();
    if (out.projectRoot.isEmpty()) {
        failure = {QStringLiteral("--morph-manifest: repository root is not available"), kExitInput};
        return false;
    }
    std::vector<NoticeRef> notices{out.manifest.libraryNotice};
    notices.insert(notices.end(), out.manifest.additionalLibraryNotices.begin(),
                   out.manifest.additionalLibraryNotices.end());
    for (const MorphLanguage &l : out.manifest.languages)
        notices.push_back(l.notice);
    for (const NoticeRef &n : notices) {
        if (!verifyNoticeFile(manifestPath, n.notice, n.sha256, error)) {
            failure = {QStringLiteral("--morph-manifest: ") + error, kExitInput};
            return false;
        }
    }
    if (!verifyLoadedHunspell(out.manifest.library.path, out.manifest.library.sha256, out.manifest.library.bytes,
                              out.manifest.hunspellVersion, out.library, error)) {
        failure = {QStringLiteral("--morph-manifest: ") + error, kExitIntegrity};
        return false;
    }
    for (const DetectorLanguage language : kLanguages) {
        const std::size_t li = std::size_t(language);
        const MorphLanguage &l = out.manifest.languages[li];
        const QString label = QStringLiteral("morphology ") + QString::fromLatin1(languageName(language));
        LanguageFiles &files = out.files[li];
        QByteArray affBytes;
        if (!readVerified(out.projectRoot, l.dic, kMaxMorphDicBytes, label + QStringLiteral(" dic"), files.dic,
                          nullptr, failure)
            || !readVerified(out.projectRoot, l.aff, kMaxAffBytes, label + QStringLiteral(" aff"), files.aff,
                             &affBytes, failure))
            return false;
        if (!includeControl)
            continue;
        QByteArray controlBytes;
        if (!readVerified(out.projectRoot, l.controlAff, kMaxAffBytes, label + QStringLiteral(" control aff"),
                          files.control, &controlBytes, failure))
            return false;
        AffControlResult derived = removePfxSfxRows(affBytes);
        if (derived.error != AffControlError::None) {
            failure = {label + QStringLiteral(" aff: ") + affControlErrorText(derived.error), kExitInput};
            return false;
        }
        if (derived.bytes != controlBytes) {
            failure = {label + QStringLiteral(" control aff is not the PFX/SFX-row removal of the source aff"),
                       kExitIntegrity};
            return false;
        }
        derived.bytes.clear();
        files.derivation = std::move(derived);
    }
    return true;
}

// Повторная проверка после Hunspell_create: файлы не изменились, пока
// библиотека читала их по пути.
bool recheckFiles(const VerifiedMorphology &v, bool includeControl, Failure &failure)
{
    QString error;
    for (std::size_t li = 0; li < 2; ++li) {
        const MorphLanguage &l = v.manifest.languages[li];
        const std::array<std::pair<const QString *, const FileRef *>, 3> files{
            {{&v.files[li].dic, &l.dic}, {&v.files[li].aff, &l.aff}, {&v.files[li].control, &l.controlAff}}};
        for (std::size_t f = 0; f < files.size(); ++f) {
            if (f == 2 && !includeControl)
                continue;
            const qint64 limit = f == 0 ? kMaxMorphDicBytes : kMaxAffBytes;
            if (!streamVerify(*files[f].first, limit, *files[f].second, nullptr, error)) {
                failure = {QStringLiteral("morphology file changed while Hunspell was loading it"), kExitIntegrity};
                return false;
            }
        }
    }
    return true;
}

struct EnginePair {
    std::array<std::unique_ptr<HunspellEngine>, 2> engines;
    [[nodiscard]] SpellPair spell() const { return SpellPair{{{engines[0].get(), engines[1].get()}}}; }
};

bool createPair(const VerifiedMorphology &v, bool control, EnginePair &out, Failure &failure)
{
    for (std::size_t li = 0; li < 2; ++li) {
        HunspellEngine::Error error = HunspellEngine::Error::None;
        out.engines[li] = HunspellEngine::create(control ? v.files[li].control : v.files[li].aff, v.files[li].dic,
                                                 error);
        if (!out.engines[li]) {
            failure = {QStringLiteral("Hunspell ") + QString::fromLatin1(languageName(DetectorLanguage(li)))
                           + QStringLiteral(": ") + hunspellEngineErrorText(error),
                       kExitIntegrity};
            return false;
        }
    }
    return true;
}

QJsonObject dependencyJson(const VerifiedMorphology &v, bool includeControl)
{
    QJsonObject o;
    set(o, "hunspellVersionHeader", compiledHunspellVersion());
    set(o, "hunspellVersionLibrary", runtimeHunspellVersion());
    set(o, "libraryBytes", v.library.bytes);
    set(o, "librarySha256", v.library.sha256);
    set(o, "libraryNoticeSha256", v.manifest.libraryNotice.sha256);
    set(o, "additionalNotices", qint64(v.manifest.additionalLibraryNotices.size()));
    set(o, "morphologyManifestSha256", v.manifest.manifestSha256);
    set(o, "linkage",
        QStringLiteral("installed system shared library, dynamically linked; verified by dladdr(&Hunspell_create) "
                       "canonical path, size and SHA-256 before any Hunspell_create; not redistributed, "
                       "not part of the application bundle"));
    qint64 fullBytes = 0;
    qint64 controlBytes = 0;
    QJsonObject languages;
    for (const DetectorLanguage language : kLanguages) {
        const std::size_t li = std::size_t(language);
        const MorphLanguage &l = v.manifest.languages[li];
        QJsonObject j;
        set(j, "dicBytes", l.dic.bytes);
        set(j, "dicSha256", l.dic.sha256);
        set(j, "affBytes", l.aff.bytes);
        set(j, "affSha256", l.aff.sha256);
        set(j, "noticeSha256", l.notice.sha256);
        fullBytes += l.dic.bytes + l.aff.bytes;
        if (includeControl) {
            const AffControlResult &d = v.files[li].derivation;
            set(j, "controlAffBytes", l.controlAff.bytes);
            set(j, "controlAffSha256", l.controlAff.sha256);
            set(j, "affRows", d.rows);
            set(j, "removedPfxRows", d.removedPfxRows);
            set(j, "removedSfxRows", d.removedSfxRows);
            set(j, "keptRows", d.keptRows);
            set(j, "controlVerified",
                QStringLiteral("control aff bytes equal deterministic removal of exact PFX/SFX rows"));
            controlBytes += l.dic.bytes + l.controlAff.bytes;
        }
        set(languages, languageName(language), j);
    }
    set(o, "languages", languages);
    set(o, "fullPairAffPlusDicBytes", fullBytes);
    if (includeControl)
        set(o, "controlPairAffPlusDicBytes", controlBytes);
    return o;
}

// ---- Проба ресурсов ----

std::vector<std::pair<QString, SourceLayout>> probeTokens()
{
    // Фиксированные синтетические строки клавиш; не слова словаря и не данные
    // оценки. Чётные — us (ASCII), нечётные — ru (кириллица).
    std::vector<std::pair<QString, SourceLayout>> tokens;
    tokens.reserve(kProbeTokenCount);
    for (int i = 0; i < kProbeTokenCount; ++i) {
        const int length = 4 + (i * 5) % 13;
        KeySequence k;
        for (int j = 0; j < length; ++j)
            k.append(static_cast<char>((i * 13 + j * 7 + 3) % NgramModel::kLetterKeys));
        const SourceLayout layout = i % 2 == 0 ? SourceLayout::Us : SourceLayout::Ru;
        tokens.push_back({keysToLayoutText(k, layout), layout});
    }
    return tokens;
}

int runProbe(const QStringList &args)
{
    QString mode;
    QString manifestPath;
    QString iterationsText;
    bool json = false;
    QString error;
    if (!parseOptions(args,
                      {{"--probe", &mode}, {"--morph-manifest", &manifestPath}, {"--iterations", &iterationsText}},
                      json, error)) {
        printUsage();
        return fail(error, kExitUsage);
    }
    if (mode.isEmpty() || manifestPath.isEmpty() || iterationsText.isEmpty() || !json)
        return usageError("--probe, --morph-manifest, --iterations and --json are required");
    const bool control = mode == QLatin1String("control");
    if (!control && mode != QLatin1String("hunspell"))
        return usageError("--probe must be hunspell or control");
    if (iterationsText.size() > 7)
        return usageError("--iterations must be an integer in [0, 1000000]");
    for (const QChar ch : iterationsText) {
        if (ch.unicode() < u'0' || ch.unicode() > u'9')
            return usageError("--iterations must be an integer in [0, 1000000]");
    }
    const qint64 iterations = iterationsText.toLongLong();
    if (iterations < 0 || iterations > kMaxProbeIterations)
        return usageError("--iterations must be an integer in [0, 1000000]");

    QElapsedTimer timer;
    timer.start();
    VerifiedMorphology verified;
    Failure failure;
    if (!verifyMorphology(manifestPath, control, verified, failure))
        return fail(failure.message, failure.code);
    const qint64 verifyNs = timer.nsecsElapsed();

    QElapsedTimer loadTimer;
    loadTimer.start();
    EnginePair pair;
    if (!createPair(verified, control, pair, failure))
        return fail(failure.message, failure.code);
    const qint64 loadNs = loadTimer.nsecsElapsed();
    loadTimer.start();
    if (!recheckFiles(verified, control, failure))
        return fail(failure.message, failure.code);
    const qint64 recheckNs = loadTimer.nsecsElapsed();

    const SpellPair spell = pair.spell();
    const std::vector<std::pair<QString, SourceLayout>> tokens = probeTokens();
    // Альтернативный текст готовится заранее: цикл измеряет только прямые
    // вызовы Hunspell_spell без кэша.
    std::vector<std::array<std::pair<const SpellChecker *, QString>, 2>> calls;
    calls.reserve(tokens.size());
    for (const auto &[text, layout] : tokens) {
        if (!isQueryableToken(text))
            return fail(QStringLiteral("synthetic probe token is not queryable"), kExitIntegrity);
        calls.push_back({{{spell.engines[std::size_t(observedLanguage(layout))], text},
                          {spell.engines[std::size_t(alternativeLanguage(layout))],
                           convertLayout(text, correctionDirection(layout))}}});
    }
    quint64 checksum = 1469598103934665603ULL;
    QElapsedTimer loopTimer;
    loopTimer.start();
    for (qint64 it = 0; it < iterations; ++it) {
        for (const auto &call : calls) {
            const quint64 value = (call[0].first->accepts(call[0].second) ? 1U : 0U)
                | (call[1].first->accepts(call[1].second) ? 2U : 0U);
            checksum = (checksum ^ value) * 1099511628211ULL;
        }
    }
    const qint64 loopNs = loopTimer.nsecsElapsed();
    const qint64 spellCalls = iterations * qint64(calls.size()) * 2;

    QJsonObject root;
    set(root, "mode", mode);
    set(root, "engines", control ? QStringLiteral("controlPairNoPfxSfx") : QStringLiteral("fullPair"));
    set(root, "dependency", dependencyJson(verified, control));
    set(root, "verifyNs", verifyNs);
    set(root, "loadNs", loadNs);
    set(root, "recheckNs", recheckNs);
    set(root, "iterations", iterations);
    set(root, "syntheticTokens", qint64(tokens.size()));
    set(root, "spellCalls", spellCalls);
    set(root, "loopNs", loopNs);
    set(root, "nsPerSpellCall",
        spellCalls > 0 ? QJsonValue(double(loopNs) / double(spellCalls)) : QJsonValue(QJsonValue::Null));
    set(root, "checksum", hexU64(checksum));
    set(root, "scope",
        QStringLiteral("resource probe: reads only the morphology manifest, its notices, the loaded Hunspell "
                       "library and the listed aff/dic files (control mode also reads the source aff to verify "
                       "the control derivation); no corpus, model or training lists. loadNs = two Hunspell_create "
                       "calls; the engines stay alive until exit. 64 fixed synthetic key strings (us ASCII / ru "
                       "Cyrillic), explicitly not evaluation data; each iteration makes two direct uncached "
                       "Hunspell_spell calls per token. OS page-cache state is unknown. RSS is not measured here: "
                       "measure this child process externally. No expansion of all word forms."));
    if (!writeStdout(root))
        return fail(QStringLiteral("cannot write stdout"), kExitOutput);
    // Движки освобождаются только после вывода.
    return 0;
}

// ---- Базовая линия опыта 002 ----

struct Baseline {
    QJsonObject dictionariesJson;
    std::array<std::vector<KeySequence>, 2> fullKeys;
    std::array<std::vector<KeySequence>, 2> trainKeys;
    std::array<QSet<KeySequence>, 2> fullSets;
    std::array<QSet<KeySequence>, 2> trainSets;
    std::vector<KeySequence> ambiguousLen4;
    qint64 ambiguousKeyCount = 0;
    std::optional<NgramModel> model;
    QByteArray modelBytes;
    std::optional<std::array<FrontCodedLexicon, 2>> fullLex;
    std::optional<std::array<FrontCodedLexicon, 2>> trainLex;
    QByteArray fullPairBytes;
    qint64 trainingMs = 0;
};

// Те же шаги, что в detector_benchmark: helper'ы опыта 001, сверка с
// опубликованными значениями, обучение, front-coded словари и сверка
// членства с QSet. Дополнительно SHA-256 модели равен протоколу 003.
bool buildBaseline(std::array<QByteArray, 2> &dicBytes, Baseline &b, Failure &failure)
{
    std::array<std::vector<ex::SplitEntry>, 2> splitEntries;
    for (const DetectorLanguage language : kLanguages) {
        const std::size_t li = std::size_t(language);
        const QString option = QStringLiteral("--") + QString::fromLatin1(languageName(language));
        const ex::DicParseResult parsed = ex::parseHunspellDic(dicBytes[li], language);
        dicBytes[li].clear();
        if (parsed.error != ex::DicError::None) {
            failure = {option + QStringLiteral(": ") + ex::dicErrorText(parsed.error), kExitInput};
            return false;
        }
        const QString filteredSha = ex::sha256Hex(parsed.words.join(QLatin1Char('\n')).toUtf8());
        if (filteredSha != QLatin1String(kExpectedFilteredListSha256[li])
            || parsed.report.accepted != kExpectedAcceptedTypes[li]) {
            failure = {option + QStringLiteral(": filtered dictionary differs from experiment 001"), kExitIntegrity};
            return false;
        }
        qint64 canonicalFailures = 0;
        for (const QString &word : parsed.words) {
            const CanonicalToken canonical = canonicalizeToken(word, homeLayout(language));
            if (canonical.status != CanonicalStatus::Ok) {
                ++canonicalFailures;
                continue;
            }
            const ex::SplitPart part = ex::splitForKeys(canonical.keys);
            b.fullKeys[li].push_back(canonical.keys);
            if (part == ex::SplitPart::Train)
                b.trainKeys[li].push_back(canonical.keys);
            splitEntries[li].push_back({word, canonical.keys, part});
        }
        if (qint64(b.trainKeys[li].size()) != kExpectedTrainTypes[li]) {
            failure = {option + QStringLiteral(": train split differs from experiment 001"), kExitIntegrity};
            return false;
        }
        QJsonObject d;
        set(d, "acceptedTypes", parsed.report.accepted);
        set(d, "canonicalFailures", canonicalFailures);
        set(d, "fullKeys", qint64(b.fullKeys[li].size()));
        set(d, "trainKeys", qint64(b.trainKeys[li].size()));
        set(d, "filteredListSha256", filteredSha);
        set(d, "matchesExperiment001", true);
        set(b.dictionariesJson, languageName(language), d);
    }
    if (!ex::countSplitLeaks(splitEntries[0], splitEntries[1]).leakFree()) {
        failure = {QStringLiteral("experiment-001 split leakage detected"), kExitIntegrity};
        return false;
    }
    splitEntries = {};
    set(b.dictionariesJson, "splitLeakFree", true);
    set(b.dictionariesJson, "processing",
        QStringLiteral("unchanged experiment-002 stem baseline: Hunspell .dic stems via experiment-001 helpers, "
                       "lowercase letter-only 1..32, affixes not expanded; stemExact = all approved stem keys"));

    for (std::size_t li = 0; li < 2; ++li) {
        b.fullSets[li] = QSet<KeySequence>(b.fullKeys[li].begin(), b.fullKeys[li].end());
        b.trainSets[li] = QSet<KeySequence>(b.trainKeys[li].begin(), b.trainKeys[li].end());
        std::sort(b.fullKeys[li].begin(), b.fullKeys[li].end());
        b.fullKeys[li].erase(std::unique(b.fullKeys[li].begin(), b.fullKeys[li].end()), b.fullKeys[li].end());
    }
    for (const KeySequence &k : b.fullKeys[0]) {
        if (b.fullSets[1].contains(k)) {
            ++b.ambiguousKeyCount;
            if (k.size() >= kMinDecisionKeys)
                b.ambiguousLen4.push_back(k);
        }
    }

    QElapsedTimer trainTimer;
    trainTimer.start();
    qsizetype skipped = 0;
    const NgramModel trained = NgramModel::train(b.trainKeys[0], b.trainKeys[1], &skipped);
    b.trainingMs = trainTimer.elapsed();
    if (skipped != 0 || !trained.isValid()) {
        failure = {QStringLiteral("model training failed integrity checks"), kExitIntegrity};
        return false;
    }
    b.modelBytes = trained.serialize();
    b.model = NgramModel::deserialize(b.modelBytes);
    if (!b.model || b.model->serialize() != b.modelBytes || b.modelBytes.size() != NgramModel::kSerializedBytes) {
        failure = {QStringLiteral("model serialization round trip failed"), kExitIntegrity};
        return false;
    }
    if (ex::sha256Hex(b.modelBytes) != QLatin1String(kModelSha256)) {
        failure = {QStringLiteral("model SHA-256 differs from the frozen protocol"), kExitIntegrity};
        return false;
    }

    std::array<std::optional<FrontCodedLexicon>, 2> fullBuilt;
    std::array<std::optional<FrontCodedLexicon>, 2> trainBuilt;
    for (std::size_t li = 0; li < 2; ++li) {
        fullBuilt[li] = FrontCodedLexicon::build(b.fullKeys[li]);
        trainBuilt[li] = FrontCodedLexicon::build(b.trainKeys[li]);
        if (!fullBuilt[li] || !trainBuilt[li]) {
            failure = {QStringLiteral("exact lexicon build failed"), kExitIntegrity};
            return false;
        }
    }
    b.fullPairBytes = serializeLexiconPair(*fullBuilt[0], *fullBuilt[1]);
    const QByteArray trainPairBytes = serializeLexiconPair(*trainBuilt[0], *trainBuilt[1]);
    b.fullLex = deserializeLexiconPair(b.fullPairBytes);
    b.trainLex = deserializeLexiconPair(trainPairBytes);
    if (!b.fullLex || !b.trainLex) {
        failure = {QStringLiteral("exact lexicon serialization round trip failed"), kExitIntegrity};
        return false;
    }
    for (std::size_t a = 0; a < 2; ++a) {
        for (const KeySequence &k : b.fullKeys[a]) {
            for (std::size_t c = 0; c < 2; ++c) {
                if (b.fullLex->at(c).contains(k) != b.fullSets[c].contains(k)
                    || b.trainLex->at(c).contains(k) != b.trainSets[c].contains(k)) {
                    failure = {QStringLiteral("exact lexicon membership differs from reference set"),
                               kExitIntegrity};
                    return false;
                }
            }
        }
    }
    return true;
}

// ---- Измерение задержки ----

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
    std::vector<double> perCase;
    perCase.reserve(count);
    QElapsedTimer timer;
    for (std::size_t i = 0; i < count; ++i) {
        timer.start();
        for (int r = 0; r < kLatencyRepeats; ++r)
            call(i);
        perCase.push_back(double(timer.nsecsElapsed()) / kLatencyRepeats);
    }
    std::sort(perCase.begin(), perCase.end());
    result.p50 = percentileNearestRank(perCase, 50);
    result.p95 = percentileNearestRank(perCase, 95);
    result.p99 = percentileNearestRank(perCase, 99);
    result.max = perCase.back();
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

// ---- Счётчики опыта 003 ----

// Членство случая по трём словарным наборам: stem (опыт 002), control, full.
struct AcceptanceTally {
    qint64 eligible = 0;
    qint64 stem = 0;
    qint64 control = 0;
    qint64 full = 0;
    qint64 fullNotControl = 0;
    qint64 controlNotFull = 0;
    qint64 fullNotStem = 0;
    qint64 stemNotFull = 0;
    qint64 none = 0;

    void add(bool s, bool c, bool f)
    {
        ++eligible;
        stem += s;
        control += c;
        full += f;
        fullNotControl += f && !c;
        controlNotFull += c && !f;
        fullNotStem += f && !s;
        stemNotFull += s && !f;
        none += !s && !c && !f;
    }

    [[nodiscard]] QJsonObject json() const
    {
        QJsonObject o;
        set(o, "eligibleLen4Plus", eligible);
        set(o, "stemHits", stem);
        set(o, "controlHits", control);
        set(o, "fullHits", full);
        set(o, "stemRate", ratio(stem, eligible));
        set(o, "controlRate", ratio(control, eligible));
        set(o, "fullRate", ratio(full, eligible));
        set(o, "fullGainsOverControl", fullNotControl);
        set(o, "fullLossesVsControl", controlNotFull);
        set(o, "fullGainsOverStem", fullNotStem);
        set(o, "fullLossesVsStem", stemNotFull);
        set(o, "noneHit", none);
        return o;
    }
};

struct AcceptanceSet {
    // [язык]: valid observed (принятие правильного слова), wrong alternative
    // (кандидат восстановления), wrong observed (ложное принятие ошибочного текста).
    std::array<AcceptanceTally, 2> validObserved;
    std::array<AcceptanceTally, 2> wrongAlternative;
    std::array<AcceptanceTally, 2> wrongObserved;

    [[nodiscard]] QJsonObject json() const
    {
        QJsonObject o;
        for (const DetectorLanguage language : kLanguages) {
            const std::size_t li = std::size_t(language);
            QJsonObject j;
            set(j, "validObservedAccepted", validObserved[li].json());
            set(j, "wrongAlternativeAccepted", wrongAlternative[li].json());
            set(j, "wrongObservedAccepted", wrongObserved[li].json());
            set(o, languageName(language), j);
        }
        return o;
    }
};

struct AmbiguityTally {
    qint64 cases = 0;
    qint64 stem = 0;
    qint64 control = 0;
    qint64 full = 0;
    qint64 shared = 0;
    qint64 sharedNotStem = 0;
    qint64 sharedNotStemControlBoth = 0;
    qint64 sharedNotStemFullBoth = 0;
    qint64 fullBothNotControlBoth = 0;
    qint64 controlBothNotFullBoth = 0;

    void add(const MorphFeatures &f)
    {
        const bool s = stemAmbiguous(f);
        const bool c = controlAmbiguous(f);
        const bool u = fullAmbiguous(f);
        ++cases;
        stem += s;
        control += c;
        full += u;
        shared += s || c || u;
        sharedNotStem += !s && (c || u);
        sharedNotStemControlBoth += !s && c;
        sharedNotStemFullBoth += !s && u;
        fullBothNotControlBoth += u && !c;
        controlBothNotFullBoth += c && !u;
    }

    [[nodiscard]] QJsonObject json() const
    {
        QJsonObject o;
        set(o, "cases", cases);
        set(o, "stemBoth", stem);
        set(o, "controlBoth", control);
        set(o, "fullBoth", full);
        set(o, "sharedLabel", shared);
        set(o, "addedBeyondStem", sharedNotStem);
        set(o, "addedBeyondStemByControl", sharedNotStemControlBoth);
        set(o, "addedBeyondStemByFull", sharedNotStemFullBoth);
        set(o, "fullBothNotControlBoth", fullBothNotControlBoth);
        set(o, "controlBothNotFullBoth", controlBothNotFullBoth);
        return o;
    }
};

struct RawChanges {
    qint64 validIntentCases = 0;
    qint64 validIntentChanges = 0;
    qint64 wrongIntentCases = 0;
    qint64 wrongIntentChanges = 0;
    qint64 wrongIntentChangesRoundTrip = 0;

    [[nodiscard]] QJsonObject json() const
    {
        QJsonObject o;
        set(o, "validIntentCases", validIntentCases);
        set(o, "validIntentChangesIgnoringAmbiguity", validIntentChanges);
        set(o, "wrongIntentCases", wrongIntentCases);
        set(o, "wrongIntentChangesIgnoringAmbiguity", wrongIntentChanges);
        set(o, "wrongIntentChangesWithExactRoundTrip", wrongIntentChangesRoundTrip);
        return o;
    }
};

struct ChallengeTally {
    qint64 count = 0;
    qint64 supported = 0;
    qint64 eligible = 0;
    AmbiguityTally ambiguity;
    std::array<RoleCounts, kMorphMethodCount> methods;
};

// ---- Оценка ----

struct Options {
    QString manifest;
    QString en;
    QString ru;
    QString evalManifest;
    QString evalEn;
    QString evalRu;
    QString protocol;
    QString morphManifest;
};

struct MorphCase {
    MorphFeatures features;
    bool shared = false;
    bool exactRepresentative = false;
};

int runBenchmark(const Options &opt)
{
    QElapsedTimer totalTimer;
    totalTimer.start();
    QString error;
    Failure failure;

    // ---- 1. Протокол 003 ----
    QByteArray protocolBytes;
    if (!readBoundedFile(opt.protocol, kMaxProtocolBytes, protocolBytes, error))
        return fail(QStringLiteral("--protocol: ") + error);
    const ProtocolResult protocol = validateProtocol003(protocolBytes);
    if (protocol.error != ProtocolError::None) {
        QString message = QStringLiteral("--protocol: ") + protocolErrorText(protocol.error);
        if (!protocol.mismatchedField.isEmpty())
            message += QStringLiteral(" (field: ") + protocol.mismatchedField + QLatin1Char(')');
        return fail(message);
    }

    // ---- 2. Паспорт и файлы словарей опыта 001 ----
    QByteArray manifestBytes;
    if (!readBoundedFile(opt.manifest, ex::kMaxManifestBytes, manifestBytes, error))
        return fail(QStringLiteral("--manifest: ") + error);
    const ex::ManifestParseResult manifestResult = ex::parseExperimentManifest(manifestBytes);
    if (manifestResult.error != ex::ManifestError::None)
        return fail(QStringLiteral("--manifest: ") + ex::manifestErrorText(manifestResult.error));
    const ex::ExperimentManifest &manifest = manifestResult.manifest;
    const std::array<QString, 2> dicPaths{{opt.en, opt.ru}};
    std::array<QByteArray, 2> dicBytes;
    for (const DetectorLanguage language : kLanguages) {
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
    const QString projectRoot = QFileInfo(ex::manifestRoot(opt.manifest)).canonicalFilePath();
    if (projectRoot.isEmpty())
        return fail(QStringLiteral("--manifest: repository root is not available"));

    // ---- 3. Паспорт и файлы оценки (тот же snapshot, что в 002) ----
    QByteArray evalManifestBytes;
    if (!readBoundedFile(opt.evalManifest, kMaxEvalManifestBytes, evalManifestBytes, error))
        return fail(QStringLiteral("--eval-manifest: ") + error);
    const EvalManifestResult evalResult = parseEvaluationManifest(evalManifestBytes);
    if (evalResult.error != EvalManifestError::None)
        return fail(QStringLiteral("--eval-manifest: ") + evalManifestErrorText(evalResult.error));
    const EvalManifest &evalManifest = evalResult.manifest;
    if (QFileInfo(ex::manifestRoot(opt.evalManifest)).canonicalFilePath() != projectRoot)
        return fail(QStringLiteral("--eval-manifest: manifests are not in the same repository"));
    const std::array<QString, 2> evalPaths{{opt.evalEn, opt.evalRu}};
    std::array<QByteArray, 2> evalBytes;
    for (const DetectorLanguage language : kLanguages) {
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

    // ---- 4. Морфология: паспорт, библиотека, .aff/.dic, контроль ----
    VerifiedMorphology morph;
    if (!verifyMorphology(opt.morphManifest, true, morph, failure))
        return fail(failure.message, failure.code);
    if (morph.projectRoot != projectRoot)
        return fail(QStringLiteral("--morph-manifest: manifests are not in the same repository"));
    for (std::size_t li = 0; li < 2; ++li) {
        const FileRef &dic = morph.manifest.languages[li].dic;
        if (dic.sha256 != manifest.sources[li].sha256 || dic.bytes != manifest.sources[li].bytes)
            return fail(QStringLiteral("--morph-manifest: dic is not the approved experiment-001 dictionary"));
        const QString &affSha = morph.manifest.languages[li].aff.sha256;
        const QString &controlSha = morph.manifest.languages[li].controlAff.sha256;
        for (const QString *other : {&manifest.sources[0].sha256, &manifest.sources[1].sha256,
                                     &evalManifest.sources[0].sha256, &evalManifest.sources[1].sha256}) {
            if (affSha == *other || controlSha == *other)
                return fail(QStringLiteral("--morph-manifest: an aff file duplicates another source"));
        }
    }
    // Все проверки источников выполнены; дальше — разбор и движки.

    QElapsedTimer stageTimer;
    stageTimer.start();

    // ---- 5. Базовая линия опыта 002 без изменений ----
    Baseline baseline;
    if (!buildBaseline(dicBytes, baseline, failure))
        return fail(failure.message, failure.code);
    FeatureContext context;
    context.model = &*baseline.model;
    context.train = {{&baseline.trainLex->at(0), &baseline.trainLex->at(1)}};
    context.full = {{&baseline.fullLex->at(0), &baseline.fullLex->at(1)}};
    context.singleD = frozenSingleD();
    context.twoScore = frozenTwoScore();
    const qint64 baselineMs = stageTimer.elapsed();

    // ---- 6. Движки Hunspell: full и control noPfxSfx ----
    stageTimer.start();
    EnginePair fullEngines;
    EnginePair controlEngines;
    if (!createPair(morph, false, fullEngines, failure) || !createPair(morph, true, controlEngines, failure))
        return fail(failure.message, failure.code);
    if (!recheckFiles(morph, true, failure))
        return fail(failure.message, failure.code);
    const SpellPair fullSpell = fullEngines.spell();
    const SpellPair controlSpell = controlEngines.spell();
    const qint64 engineLoadMs = stageTimer.elapsed();

    // ---- 7. Выборка: первые 10000 по рангу, новый блок [5000, 10000) ----
    stageTimer.start();
    std::array<std::vector<QString>, 2> tokens;
    std::array<QSet<QByteArray>, 2> oldTokenTypes;
    QJsonObject selectionJson;
    QJsonObject tokenizationJson;
    QSet<qint64> enIds;
    for (const DetectorLanguage language : kLanguages) {
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
            selectSentences(std::move(tsv.records), QByteArray(kHoldoutSeed), kSelectionTake);
        if (selection.report.selected != kSelectionTake)
            return fail(option + QStringLiteral(": fewer unique sentences than the frozen selection size"));

        QByteArray oldIds;
        QByteArray newIds;
        QSet<qint64> oldIdSet;
        QSet<QString> oldTexts;
        for (int i = 0; i < kHoldoutOffset; ++i) {
            const SentenceRecord &r = selection.selected[std::size_t(i)];
            oldIds.append(QByteArray::number(r.id));
            oldIds.append('\n');
            oldIdSet.insert(r.id);
            oldTexts.insert(r.text);
        }
        qint64 idOverlap = 0;
        qint64 textOverlap = 0;
        for (int i = kHoldoutOffset; i < kSelectionTake; ++i) {
            const SentenceRecord &r = selection.selected[std::size_t(i)];
            newIds.append(QByteArray::number(r.id));
            newIds.append('\n');
            idOverlap += oldIdSet.contains(r.id);
            textOverlap += oldTexts.contains(r.text);
        }
        const QString oldSha = ex::sha256Hex(oldIds);
        if (oldSha != QLatin1String(kOldDiagnosticIdsSha256[li]))
            return fail(option + QStringLiteral(": reserved block differs from the experiment-002 selection"),
                        kExitIntegrity);
        if (idOverlap != 0 || textOverlap != 0)
            return fail(option + QStringLiteral(": new block overlaps the reserved block"), kExitIntegrity);
        oldIdSet.clear();
        oldTexts.clear();

        // Старый блок только для диагностики пересечения типов; не оценивается.
        qint64 oldTokens = 0;
        for (int i = 0; i < kHoldoutOffset; ++i) {
            for (const QString &t : tokenizeSentence(selection.selected[std::size_t(i)].text)) {
                oldTokenTypes[li].insert(exactCaseTypeKey(language, false, t));
                ++oldTokens;
            }
        }
        TokenizeStats stats;
        for (int i = kHoldoutOffset; i < kSelectionTake; ++i) {
            std::vector<QString> sentenceTokens = tokenizeSentence(selection.selected[std::size_t(i)].text, &stats);
            for (QString &t : sentenceTokens)
                tokens[li].push_back(std::move(t));
        }
        selection.selected.clear();
        if (tokens[li].empty())
            return fail(option + QStringLiteral(": new block produced no tokens"));

        QJsonObject s;
        set(s, "records", selection.report.records);
        set(s, "uniqueTexts", selection.report.uniqueTexts);
        set(s, "duplicateTextRecords", selection.report.duplicateTextRecords);
        set(s, "selectedFirstRanks", selection.report.selected);
        set(s, "firstRanksIdsSha256", selection.report.selectedIdsSha256);
        QJsonObject oldBlock;
        set(oldBlock, "ranks", QStringLiteral("0..4999"));
        set(oldBlock, "count", kHoldoutOffset);
        set(oldBlock, "idsSha256", oldSha);
        set(oldBlock, "matchesExperiment002Selection", true);
        set(oldBlock, "use", QStringLiteral("reserved diagnostic set; not evaluated; only type overlap counts"));
        set(oldBlock, "tokens", oldTokens);
        set(s, "reservedOldBlock", oldBlock);
        QJsonObject newBlock;
        set(newBlock, "ranks", QStringLiteral("5000..9999"));
        set(newBlock, "count", kHoldoutCount);
        set(newBlock, "idsSha256", ex::sha256Hex(newIds));
        set(newBlock, "idOverlapWithReserved", idOverlap);
        set(newBlock, "exactTextOverlapWithReserved", textOverlap);
        set(s, "newBlock", newBlock);
        set(selectionJson, languageName(language), s);
        QJsonObject t;
        set(t, "whitespacePieces", stats.pieces);
        set(t, "emptyAfterTrim", stats.emptyAfterTrim);
        set(t, "trimmedTokens", stats.trimmed);
        set(t, "tokens", qint64(tokens[li].size()));
        set(tokenizationJson, languageName(language), t);
    }
    enIds.clear();
    set(selectionJson, "independence",
        QStringLiteral("new block of the same pinned Tatoeba snapshot, seed, dedup and rank as experiment 002: "
                       "sentence IDs and exact texts are disjoint from the reserved block, but source, authors, "
                       "templates and vocabulary are shared and correlated; not new-domain independence"));

    // ---- 8. Естественные случаи и признаки ----
    NaturalCases natural = buildNaturalCases(tokens, context);
    tokens = {};
    const std::vector<EvalCase> &cases = natural.cases;
    for (const NaturalTokenStats &s : natural.stats) {
        if (s.wrongCanonicalMismatches != 0)
            return fail(QStringLiteral("wrong-layout copy changed canonical keys"), kExitIntegrity);
        if (s.wrongCopies == 0)
            return fail(QStringLiteral("no wrong-layout copies could be generated"));
    }
    qint64 canonicalTypes = 0;
    for (const EvalCase &c : cases) {
        canonicalTypes += c.typeRepresentative;
        const CanonicalToken canonical = canonicalizeToken(c.text, c.layout);
        if (canonical.status != c.status)
            return fail(QStringLiteral("case canonicalization is not deterministic"), kExitIntegrity);
        if (canonical.status != CanonicalStatus::Ok)
            continue;
        const std::size_t obs = std::size_t(observedLanguage(c.layout));
        const std::size_t alt = std::size_t(alternativeLanguage(c.layout));
        if (c.features.observedFull != baseline.fullSets[obs].contains(canonical.keys)
            || c.features.alternativeFull != baseline.fullSets[alt].contains(canonical.keys)
            || c.features.observedTrain != baseline.trainSets[obs].contains(canonical.keys)
            || c.features.alternativeTrain != baseline.trainSets[alt].contains(canonical.keys))
            return fail(QStringLiteral("case membership differs from reference set"), kExitIntegrity);
    }

    // Кэш признаков Hunspell по (раскладка, точный текст): только для
    // скорости оценки. Задержка ниже измеряется без кэша.
    QHash<QString, quint8> morphCache;
    qint64 cacheLookups = 0;
    qint64 hunspellQueries = 0;
    auto cachedMorph = [&](const QString &text, SourceLayout layout, const CaseFeatures &base,
                           MorphFeatures &out) {
        out.base = base;
        out.control = PairMembership();
        out.full = PairMembership();
        if (!isDecisionEligible(base))
            return;
        ++cacheLookups;
        const QString key = QChar(layout == SourceLayout::Ru ? u'r' : u'u') + text;
        auto it = morphCache.constFind(key);
        if (it == morphCache.constEnd()) {
            const PairMembership c = queryPair(controlSpell, text, layout, base);
            const PairMembership f = queryPair(fullSpell, text, layout, base);
            hunspellQueries += 4;
            const quint8 bits = quint8((c.observed ? 1 : 0) | (c.alternative ? 2 : 0) | (f.observed ? 4 : 0)
                                       | (f.alternative ? 8 : 0));
            it = morphCache.insert(key, bits);
        }
        const quint8 bits = it.value();
        out.control = {(bits & 1) != 0, (bits & 2) != 0};
        out.full = {(bits & 4) != 0, (bits & 8) != 0};
    };

    std::vector<MorphCase> morphCases(cases.size());
    QSet<QByteArray> exactTypeKeys;
    std::vector<std::size_t> allIndices;
    std::vector<std::size_t> typeIndices;
    allIndices.reserve(cases.size());
    for (std::size_t i = 0; i < cases.size(); ++i) {
        const EvalCase &c = cases[i];
        MorphCase &m = morphCases[i];
        cachedMorph(c.text, c.layout, c.features, m.features);
        m.shared = sharedAmbiguous(m.features);
        if (stemAmbiguous(m.features) != c.ambiguous)
            return fail(QStringLiteral("stem ambiguity label differs from experiment-002 helper"), kExitIntegrity);
        const QByteArray typeKey = exactCaseTypeKey(c.language, c.wrong, c.text);
        if (!exactTypeKeys.contains(typeKey)) {
            exactTypeKeys.insert(typeKey);
            m.exactRepresentative = true;
            typeIndices.push_back(i);
        }
        allIndices.push_back(i);
    }
    exactTypeKeys.clear();

    std::array<std::vector<Outcome>, kMorphMethodCount> outcomes;
    std::array<std::vector<Outcome>, kMorphMethodCount> legacyOutcomes;
    for (std::size_t m = 0; m < kMorphMethods.size(); ++m) {
        outcomes[m].reserve(cases.size());
        legacyOutcomes[m].reserve(cases.size());
        for (std::size_t i = 0; i < cases.size(); ++i) {
            const EvalCase &c = cases[i];
            const DecisionAction action = decideMorph(kMorphMethods[m], morphCases[i].features);
            outcomes[m].push_back(classifyOutcome(action, c.wrong, morphCases[i].shared, c.conversionMatches));
            legacyOutcomes[m].push_back(classifyOutcome(action, c.wrong, c.ambiguous, c.conversionMatches));
        }
    }

    auto slicesJson = [&](const std::vector<Outcome> &o, const std::vector<std::size_t> &indices,
                          bool sharedLabel) {
        SliceCounts overall;
        std::array<SliceCounts, 2> byLanguage;
        std::array<SliceCounts, 2> byLength;
        std::array<SliceCounts, kMembershipCount> byStem;
        std::array<SliceCounts, kMembershipCount> byControl;
        std::array<SliceCounts, kMembershipCount> byFull;
        for (const std::size_t i : indices) {
            const EvalCase &c = cases[i];
            const MorphFeatures &f = morphCases[i].features;
            const bool amb = sharedLabel ? morphCases[i].shared : c.ambiguous;
            overall.add(c.wrong, amb, o[i]);
            byLanguage[std::size_t(c.language)].add(c.wrong, amb, o[i]);
            byLength[c.length >= kMinDecisionKeys ? 1 : 0].add(c.wrong, amb, o[i]);
            byStem[std::size_t(fullMembership(f.base))].add(c.wrong, amb, o[i]);
            byControl[std::size_t(pairMembership(f.base.supported, f.control))].add(c.wrong, amb, o[i]);
            byFull[std::size_t(pairMembership(f.base.supported, f.full))].add(c.wrong, amb, o[i]);
        }
        QJsonObject j = sliceJson(overall);
        QJsonObject lang;
        set(lang, "en", sliceJson(byLanguage[0]));
        set(lang, "ru", sliceJson(byLanguage[1]));
        set(j, "byIntendedLanguage", lang);
        if (sharedLabel) {
            QJsonObject len;
            set(len, "len1to3", sliceJson(byLength[0]));
            set(len, "len4plus", sliceJson(byLength[1]));
            set(j, "byLength", len);
            QJsonObject stem;
            QJsonObject control;
            QJsonObject full;
            for (int k = 0; k < kMembershipCount; ++k) {
                set(stem, membershipName(Membership(k)), sliceJson(byStem[std::size_t(k)]));
                set(control, membershipName(Membership(k)), sliceJson(byControl[std::size_t(k)]));
                set(full, membershipName(Membership(k)), sliceJson(byFull[std::size_t(k)]));
            }
            set(j, "byStemMembership", stem);
            set(j, "byControlMembership", control);
            set(j, "byFullMorphMembership", full);
        }
        return std::make_pair(j, overall);
    };

    QJsonObject methodsJson;
    for (std::size_t m = 0; m < kMorphMethods.size(); ++m) {
        const auto occurrences = slicesJson(outcomes[m], allIndices, true);
        QJsonObject j;
        set(j, "occurrences", occurrences.first);
        set(j, "uniqueExactCaseTypes", slicesJson(outcomes[m], typeIndices, true).first);
        set(j, "expectedPrecision", expectedPrecisionJson(occurrences.second));
        std::array<RawChanges, 2> raw;
        for (std::size_t i = 0; i < cases.size(); ++i) {
            const EvalCase &c = cases[i];
            RawChanges &r = raw[std::size_t(c.language)];
            const bool change = outcomes[m][i] == Outcome::Recovered || isUnsafe(outcomes[m][i]);
            if (c.wrong) {
                ++r.wrongIntentCases;
                r.wrongIntentChanges += change;
                r.wrongIntentChangesRoundTrip += change && c.conversionMatches;
            } else {
                ++r.validIntentCases;
                r.validIntentChanges += change;
            }
        }
        QJsonObject rawJson;
        set(rawJson, "en", raw[0].json());
        set(rawJson, "ru", raw[1].json());
        set(rawJson, "definition",
            QStringLiteral("every Correct decision on natural valid-intent or wrong-intent occurrences, ignoring "
                           "the ambiguity label; for transparency only, ambiguous changes remain unsafe"));
        set(j, "rawChangeTotals", rawJson);
        if (m <= std::size_t(MorphMethod::ModelConfirmedStem))
            set(j, "legacyStemOnlyAmbiguityOccurrences", slicesJson(legacyOutcomes[m], allIndices, false).first);
        set(methodsJson, morphMethodName(kMorphMethods[m]), j);
    }
    legacyOutcomes = {};

    QJsonObject pairwiseJson;
    const std::array<std::pair<MorphMethod, const char *>, 4> baselines{{
        {MorphMethod::StemExact, "vsStemExact"},
        {MorphMethod::TwoScore, "vsTwoScore"},
        {MorphMethod::HunspellNoPfxSfx, "vsHunspellNoPfxSfx"},
        {MorphMethod::HunspellFull, "vsHunspellFull"},
    }};
    for (std::size_t m = 0; m < kMorphMethods.size(); ++m) {
        QJsonObject j;
        for (const auto &[b, name] : baselines) {
            if (b == kMorphMethods[m])
                continue;
            const std::size_t bi = std::size_t(b);
            QJsonObject p;
            set(p, "occurrences", pairedJson(pairedDelta(outcomes[m], outcomes[bi], allIndices)));
            set(p, "uniqueExactCaseTypes", pairedJson(pairedDelta(outcomes[m], outcomes[bi], typeIndices)));
            set(j, name, p);
        }
        set(pairwiseJson, morphMethodName(kMorphMethods[m]), j);
    }
    set(pairwiseJson, "definition",
        QStringLiteral("paired over identical cases with the shared ambiguity label: addedRecoveries = method "
                       "Recovered and baseline not; lostRecoveries = reverse; addedUnsafeChanges = method unsafe "
                       "(false change, wrong conversion or ambiguous change) and baseline not; "
                       "preventedUnsafeChanges = reverse"));

    // Принятие и неоднозначность.
    AcceptanceSet acceptanceOcc;
    AcceptanceSet acceptanceTypes;
    std::array<std::array<AmbiguityTally, 2>, 2> ambiguityOcc;   // [язык][роль]
    std::array<std::array<AmbiguityTally, 2>, 2> ambiguityTypes; // [язык][роль]
    for (std::size_t i = 0; i < cases.size(); ++i) {
        const EvalCase &c = cases[i];
        const MorphFeatures &f = morphCases[i].features;
        const std::size_t li = std::size_t(c.language);
        const bool representative = morphCases[i].exactRepresentative;
        ambiguityOcc[li][c.wrong ? 1 : 0].add(f);
        if (representative)
            ambiguityTypes[li][c.wrong ? 1 : 0].add(f);
        if (!isDecisionEligible(f.base))
            continue;
        for (AcceptanceSet *target : {&acceptanceOcc, representative ? &acceptanceTypes : nullptr}) {
            if (!target)
                continue;
            if (c.wrong) {
                target->wrongAlternative[li].add(f.base.alternativeFull, f.control.alternative, f.full.alternative);
                target->wrongObserved[li].add(f.base.observedFull, f.control.observed, f.full.observed);
            } else {
                target->validObserved[li].add(f.base.observedFull, f.control.observed, f.full.observed);
            }
        }
    }
    auto ambiguityJson = [](const std::array<std::array<AmbiguityTally, 2>, 2> &t) {
        QJsonObject o;
        for (const DetectorLanguage language : kLanguages) {
            QJsonObject j;
            set(j, "valid", t[std::size_t(language)][0].json());
            set(j, "wrong", t[std::size_t(language)][1].json());
            set(o, languageName(language), j);
        }
        return o;
    };
    QJsonObject acceptanceJson;
    set(acceptanceJson, "occurrences", acceptanceOcc.json());
    set(acceptanceJson, "uniqueExactCaseTypes", acceptanceTypes.json());
    set(acceptanceJson, "definition",
        QStringLiteral("eligible (supported, >=4 keys) natural cases only. validObservedAccepted: the intended "
                       "word in its own language; wrongAlternativeAccepted: the converted candidate of a wrong "
                       "copy (recovery potential); wrongObservedAccepted: the wrong-layout text itself accepted "
                       "by the observed language (blocks correction). stem = experiment-002 stem lexicon on "
                       "canonical keys; control = Hunspell noPfxSfx; full = Hunspell original aff; Hunspell "
                       "queries keep the exact case"));
    QJsonObject ambiguityRoot;
    set(ambiguityRoot, "occurrences", ambiguityJson(ambiguityOcc));
    set(ambiguityRoot, "uniqueExactCaseTypes", ambiguityJson(ambiguityTypes));
    set(ambiguityRoot, "rule",
        QStringLiteral("shared label for all nine methods: stem-both OR noPfxSfx-both OR full-both on the actual "
                       "case-preserved candidate pair; any Correct on a labelled case is unsafe and never a "
                       "recovery. addedBeyondStem counts labels added by the Hunspell engines over the "
                       "experiment-002 stem label"));

    // Пересечение нового блока со старым (только диагностика корреляции).
    QJsonObject overlapJson;
    for (const DetectorLanguage language : kLanguages) {
        const std::size_t li = std::size_t(language);
        qint64 occ = 0;
        qint64 occSeen = 0;
        qint64 types = 0;
        qint64 typesSeen = 0;
        for (std::size_t i = 0; i < cases.size(); ++i) {
            const EvalCase &c = cases[i];
            if (c.wrong || std::size_t(c.language) != li)
                continue;
            const bool seen = oldTokenTypes[li].contains(exactCaseTypeKey(c.language, false, c.text));
            ++occ;
            occSeen += seen;
            if (morphCases[i].exactRepresentative) {
                ++types;
                typesSeen += seen;
            }
        }
        QJsonObject j;
        set(j, "newValidOccurrences", occ);
        set(j, "newValidOccurrencesWhoseExactTypeOccursInReserved", occSeen);
        set(j, "newValidExactTypes", types);
        set(j, "newValidExactTypesAlsoInReserved", typesSeen);
        set(j, "typeShareAlsoInReserved", ratio(typesSeen, types));
        set(overlapJson, languageName(language), j);
    }
    oldTokenTypes = {};

    QJsonObject naturalJson;
    for (const DetectorLanguage language : kLanguages)
        set(naturalJson, languageName(language), naturalStatsJson(natural.stats[std::size_t(language)]));
    set(naturalJson, "caseCount", qint64(cases.size()));
    set(naturalJson, "uniqueExactCaseTypeCount", qint64(typeIndices.size()));
    set(naturalJson, "experiment002CanonicalTypeCountForReference", canonicalTypes);
    set(naturalJson, "typeContract",
        QStringLiteral("experiment 003 unique types are (language, role, exact case-preserved text) for every "
                       "method, unlike the canonical-key types of experiment 002. Protocol 003 explicitly "
                       "preserves case: Hunspell membership depends on case, so canonical types would merge case "
                       "variants with different outcomes. Per-language validTypes/wrongTypes above keep the "
                       "experiment-002 canonical definition"));
    QJsonObject cacheJson;
    set(cacheJson, "entries", qint64(morphCache.size()));
    set(cacheJson, "eligibleLookups", cacheLookups);
    set(cacheJson, "hunspellSpellCalls", hunspellQueries);
    set(cacheJson, "scope",
        QStringLiteral("evaluation-only cache of Hunspell pair membership keyed by (typed layout, exact text); "
                       "decisions are identical to uncached queries (checked on the latency sample); latency is "
                       "measured without this cache"));
    set(naturalJson, "featureCache", cacheJson);
    const qint64 evaluationMs = stageTimer.elapsed();

    // ---- 9. Испытание: те же 645 случаев, что в 002 ----
    stageTimer.start();
    std::vector<std::pair<QString, MorphFeatures>> challenge;
    auto addChallenge = [&](const QString &category, SourceLayout layout, const QString &text) {
        // Текст живёт только здесь: запрос Hunspell и сразу отбрасывание.
        const ChallengeCase base = makeChallengeCase(category, layout, text, context);
        MorphFeatures f;
        f.base = base.features;
        f.control = queryPair(controlSpell, text, layout, base.features);
        f.full = queryPair(fullSpell, text, layout, base.features);
        challenge.push_back({category, f});
    };
    for (const ChallengeFixture &f : challengeFixtures())
        addChallenge(f.category, f.layout, f.text);
    const auto excluded = [&baseline](const KeySequence &k) {
        return baseline.fullSets[0].contains(k) || baseline.fullSets[1].contains(k);
    };
    qint64 noiseCounter = 0;
    QJsonObject noiseJson;
    for (const SourceLayout layout : {SourceLayout::Us, SourceLayout::Ru}) {
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
            addChallenge(category, layout, text);
        }
        QJsonObject n;
        set(n, "accepted", qint64(noise.keys.size()));
        set(n, "attempts", noise.attempts);
        set(n, "rejectedStemLexiconHits", noise.rejectedExcluded);
        set(n, "rejectedDuplicates", noise.rejectedDuplicate);
        set(noiseJson, layoutName(layout), n);
    }
    for (const KeySequence &k : baseline.ambiguousLen4) {
        addChallenge(QStringLiteral("ambiguousLen4TypedInUs"), SourceLayout::Us, keysToLayoutText(k, SourceLayout::Us));
        addChallenge(QStringLiteral("ambiguousLen4TypedInRu"), SourceLayout::Ru, keysToLayoutText(k, SourceLayout::Ru));
    }
    if (challenge.size() != std::size_t(kExpectedChallengeCases))
        return fail(QStringLiteral("challenge differs from the experiment-002 case count"), kExitIntegrity);

    QStringList categoryOrder;
    QHash<QString, ChallengeTally> tallies;
    ChallengeTally totals;
    for (const auto &[category, f] : challenge) {
        if (!tallies.contains(category))
            categoryOrder.append(category);
        const bool shared = sharedAmbiguous(f);
        for (ChallengeTally *t : {&tallies[category], &totals}) {
            ++t->count;
            t->supported += f.base.supported;
            t->eligible += isDecisionEligible(f.base);
            t->ambiguity.add(f);
            for (std::size_t m = 0; m < kMorphMethods.size(); ++m)
                t->methods[m].add(classifyOutcome(decideMorph(kMorphMethods[m], f), false, shared, false));
        }
    }
    auto challengeJsonFor = [](const ChallengeTally &t) {
        QJsonObject c;
        set(c, "count", t.count);
        set(c, "supported", t.supported);
        set(c, "eligibleLen4Plus", t.eligible);
        set(c, "ambiguityLabels", t.ambiguity.json());
        QJsonObject methods;
        for (std::size_t m = 0; m < kMorphMethods.size(); ++m) {
            QJsonObject r;
            set(r, "keep", t.methods[m].keep);
            set(r, "abstain", t.methods[m].abstain);
            set(r, "unsafeChanges", t.methods[m].changes());
            set(r, "changeRate", ratio(t.methods[m].changes(), t.methods[m].count));
            set(methods, morphMethodName(kMorphMethods[m]), r);
        }
        set(c, "methods", methods);
        return c;
    };
    QJsonObject categoriesJson;
    for (const QString &name : std::as_const(categoryOrder))
        categoriesJson.insert(name, challengeJsonFor(tallies[name]));
    QJsonObject challengeJson;
    set(challengeJson, "version", kChallengeVersion);
    set(challengeJson, "cases", qint64(challenge.size()));
    set(challengeJson, "intent",
        QStringLiteral("every challenge token is labelled Keep; any Correct is an unsafe change. Same fixtures, "
                       "noise sequence and ambiguous stem keys as experiment 002: noise excludes only the old "
                       "full stem lexicons, not Hunspell acceptance, so cases are not chosen with the new "
                       "engines. Texts are held in memory only for the Hunspell query. No heuristic gates are "
                       "derived from these results"));
    set(challengeJson, "categories", categoriesJson);
    set(challengeJson, "allCategories", challengeJsonFor(totals));
    set(challengeJson, "noise", noiseJson);
    set(challengeJson, "noiseRule",
        QStringLiteral("unchanged from experiment 002: attempt i: d = SHA-256(noise_seed || 0x00 || ASCII decimal "
                       "i), us first, ru continues the counter; length = 4 + d[0] mod 13; key j = d[1+j] mod 33; "
                       "keys in either old full stem lexicon and duplicates rejected; at most 100000 attempts in "
                       "total"));
    set(challengeJson, "ambiguousStemKeysTotal", baseline.ambiguousKeyCount);
    set(challengeJson, "ambiguousStemKeysLen4Plus", qint64(baseline.ambiguousLen4.size()));
    challenge.clear();
    const qint64 challengeMs = stageTimer.elapsed();

    // ---- 10. Тёплая задержка без кэша ----
    stageTimer.start();
    std::vector<std::size_t> sample;
    const std::size_t stride = std::max<std::size_t>(1, cases.size() / std::size_t(kLatencySampleMax));
    for (std::size_t i = 0; i < cases.size() && sample.size() < std::size_t(kLatencySampleMax); i += stride)
        sample.push_back(i);
    for (const std::size_t i : sample) {
        const EvalCase &c = cases[i];
        const MorphFeatures &f = morphCases[i].features;
        const PairMembership liveControl = queryPair(controlSpell, c.text, c.layout, f.base);
        const PairMembership liveFull = queryPair(fullSpell, c.text, c.layout, f.base);
        if (liveControl.observed != f.control.observed || liveControl.alternative != f.control.alternative
            || liveFull.observed != f.full.observed || liveFull.alternative != f.full.alternative)
            return fail(QStringLiteral("cached Hunspell membership differs from a direct query"), kExitIntegrity);
        for (const MorphMethod method : kMorphMethods) {
            if (decideMorphLive(method, c.text, c.layout, context, controlSpell, fullSpell)
                != decideMorph(method, f))
                return fail(QStringLiteral("live decision path differs from evaluated decision"), kExitIntegrity);
        }
    }
    volatile qint64 sink = 0;
    QJsonObject pathsJson;
    for (const MorphMethod method : kMorphMethods) {
        const LatencyResult r = measure(sample.size(), [&](std::size_t s) {
            const EvalCase &c = cases[sample[s]];
            sink = sink + int(decideMorphLive(method, c.text, c.layout, context, controlSpell, fullSpell));
        });
        set(pathsJson, morphMethodName(method), latencyJson(r));
    }
    struct Lookup {
        const SpellChecker *observedFull;
        const SpellChecker *alternativeFull;
        const SpellChecker *observedControl;
        const SpellChecker *alternativeControl;
        QString observed;
        QString alternative;
        KeySequence keys;
        SourceLayout layout;
    };
    std::vector<Lookup> lookups;
    for (const std::size_t i : sample) {
        const EvalCase &c = cases[i];
        if (!isDecisionEligible(c.features))
            continue;
        const std::size_t obs = std::size_t(observedLanguage(c.layout));
        const std::size_t alt = std::size_t(alternativeLanguage(c.layout));
        lookups.push_back({fullSpell.engines[obs], fullSpell.engines[alt], controlSpell.engines[obs],
                           controlSpell.engines[alt], c.text, convertLayout(c.text, correctionDirection(c.layout)),
                           canonicalizeToken(c.text, c.layout).keys, c.layout});
    }
    const LatencyResult fullLookup = measure(lookups.size(), [&](std::size_t s) {
        const Lookup &l = lookups[s];
        sink = sink + int(l.observedFull->accepts(l.observed)) + 2 * int(l.alternativeFull->accepts(l.alternative));
    });
    const LatencyResult controlLookup = measure(lookups.size(), [&](std::size_t s) {
        const Lookup &l = lookups[s];
        sink = sink + int(l.observedControl->accepts(l.observed))
            + 2 * int(l.alternativeControl->accepts(l.alternative));
    });
    const LatencyResult stemLookup = measure(lookups.size(), [&](std::size_t s) {
        const Lookup &l = lookups[s];
        const bool observed = baseline.fullLex->at(std::size_t(observedLanguage(l.layout))).contains(l.keys);
        const bool alternative = baseline.fullLex->at(std::size_t(alternativeLanguage(l.layout))).contains(l.keys);
        sink = sink + int(observed) + 2 * int(alternative);
    });
    QJsonObject lookupJson;
    set(lookupJson, "hunspellFullObservedAndAlternative", latencyJson(fullLookup));
    set(lookupJson, "hunspellControlObservedAndAlternative", latencyJson(controlLookup));
    set(lookupJson, "stemFrontCodedObservedAndAlternative", latencyJson(stemLookup));
    set(lookupJson, "sampleEligibleCases", qint64(lookups.size()));
    QJsonObject latencyRoot;
    set(latencyRoot, "fullDecisionPaths", pathsJson);
    set(latencyRoot, "prenormalizedLookups", lookupJson);
    set(latencyRoot, "sampleCases", qint64(sample.size()));
    set(latencyRoot, "repeatsPerCase", kLatencyRepeats);
    set(latencyRoot, "methodology",
        QStringLiteral("warm, single thread, new-block natural cases: every k-th case, at most 2000 (k = max(1, "
                       "cases/2000)). First the cached Hunspell membership and every live decision are checked "
                       "to equal the evaluated ones. One warm-up pass; pNN = nearest-rank percentile over cases "
                       "of the mean of 32 back-to-back calls; batchedMeanNs = one timer over 32 passes. "
                       "fullDecisionPaths include canonicalizeToken, convertLayout and direct uncached "
                       "Hunspell_spell (no evaluation cache); prenormalizedLookups exclude normalization and "
                       "conversion. Not single-keystroke latency"));
    const qint64 latencyMs = stageTimer.elapsed();

    // ---- 11. Отчёт ----
    QJsonObject resourcesJson;
    set(resourcesJson, "hunspell", dependencyJson(morph, true));
    QJsonObject modelJson;
    set(modelJson, "serializedBytes", qint64(baseline.modelBytes.size()));
    set(modelJson, "serializedSha256", ex::sha256Hex(baseline.modelBytes));
    set(modelJson, "matchesProtocolSha256", true);
    set(modelJson, "trainingTypesEn", qint64(baseline.trainKeys[0].size()));
    set(modelJson, "trainingTypesRu", qint64(baseline.trainKeys[1].size()));
    set(resourcesJson, "ngramModel", modelJson);
    QJsonObject stemJson;
    set(stemJson, "pairSerializedBytes", qint64(baseline.fullPairBytes.size()));
    set(stemJson, "pairSerializedSha256", ex::sha256Hex(baseline.fullPairBytes));
    set(stemJson, "keysEn", baseline.fullLex->at(0).keyCount());
    set(stemJson, "keysRu", baseline.fullLex->at(1).keyCount());
    set(resourcesJson, "stemExactFrontCoded", stemJson);
    set(resourcesJson, "boundary",
        QStringLiteral("aff/dic and library sizes are file sizes, not RSS; Hunspell builds its own in-memory "
                       "tables; process RSS and load wall are measured only externally on separate --probe "
                       "children. Dictionaries and the library are local development inputs, not part of an "
                       "application bundle"));

    QJsonObject sourcesJson;
    QJsonObject trainingJson;
    set(trainingJson, "manifestSha256", manifest.manifestSha256);
    set(trainingJson, "upstreamCommit", manifest.commit);
    QJsonObject evaluationJson;
    set(evaluationJson, "manifestSha256", evalManifest.manifestSha256);
    set(evaluationJson, "releaseApproved", false);
    for (const DetectorLanguage language : kLanguages) {
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
    set(sourcesJson, "morphologyManifestSha256", morph.manifest.manifestSha256);
    set(sourcesJson, "verified",
        QStringLiteral("protocol 003, all three manifests, all source/aff/control/notice sizes and SHA-256, the "
                       "loaded Hunspell library and the control derivation were verified before any parsing or "
                       "Hunspell_create; aff/dic were re-hashed after Hunspell_create; morphology dic equals the "
                       "approved experiment-001 dictionary"));

    QJsonObject protocolJson;
    set(protocolJson, "sha256", protocol.sha256);
    set(protocolJson, "validation", QStringLiteral("exact deep match with the frozen protocol 003 document"));
    QJsonObject thresholdsJson;
    set(thresholdsJson, "twoScoreGap", kTwoScoreGap);
    set(thresholdsJson, "twoScoreMinAlternative", kTwoScoreMinAlternative);
    set(thresholdsJson, "twoScoreMaxObserved", kTwoScoreMaxObserved);
    set(thresholdsJson, "calibration", QStringLiteral("none; fixed by protocol from experiment 001"));
    set(protocolJson, "thresholds", thresholdsJson);

    QJsonObject hypothesesJson;
    set(hypothesesJson, "M1",
        QStringLiteral("acceptance.*.validObservedAccepted/wrongAlternativeAccepted full vs control gains/losses; "
                       "pairwise.hunspellFull.vsHunspellNoPfxSfx added recoveries and added unsafe changes"));
    set(hypothesesJson, "M2",
        QStringLiteral("pairwise.hunspellFull.vsTwoScore and methods.hunspellFull vs methods.twoScore recovered "
                       "and unsafe totals"));
    set(hypothesesJson, "M3",
        QStringLiteral("pairwise.modelConfirmedMorph.vsHunspellFull prevented unsafe vs lost recoveries"));
    set(hypothesesJson, "M4",
        QStringLiteral("pairwise.morphThenModel.vsHunspellFull added recoveries vs added unsafe changes; "
                       "occurrences.byFullMorphMembership.neither"));
    set(hypothesesJson, "verdict",
        QStringLiteral("not computed by this tool; the result report states supported/refuted/inconclusive"));

    QJsonObject timings;
    set(timings, "baselineMs", baselineMs);
    set(timings, "trainingMs", baseline.trainingMs);
    set(timings, "hunspellLoadMs", engineLoadMs);
    set(timings, "evaluationMs", evaluationMs);
    set(timings, "challengeMs", challengeMs);
    set(timings, "latencyMs", latencyMs);
    set(timings, "totalMs", totalTimer.elapsed());

    QJsonObject root;
    set(root, "experiment", QStringLiteral("SYS-235 experiment 003 morphology control"));
    set(root, "scope",
        QStringLiteral("local evaluation only; aggregate counts, hashes, sizes and timings; no words, sentences, "
                       "IDs or paths; automatic input stays disabled"));
    set(root, "protocol", protocolJson);
    set(root, "sources", sourcesJson);
    set(root, "dictionaries", baseline.dictionariesJson);
    set(root, "selection", selectionJson);
    set(root, "tokenization", tokenizationJson);
    set(root, "naturalTokens", naturalJson);
    set(root, "newBlockTypeOverlapWithReserved", overlapJson);
    set(root, "acceptance", acceptanceJson);
    set(root, "ambiguity", ambiguityRoot);
    set(root, "methods", methodsJson);
    set(root, "pairwise", pairwiseJson);
    set(root, "challenge", challengeJson);
    set(root, "latency", latencyRoot);
    set(root, "resources", resourcesJson);
    set(root, "hypothesisEvidence", hypothesesJson);
    set(root, "definitions",
        QStringLiteral("valid = natural new-block token, intent Keep, native layout; wrong = synthetic copy of a "
                       "native letter-only token typed in the other layout, case retained; Hunspell membership: "
                       "observed = the exact typed text in the typed layout's language, alternative = "
                       "convertLayout(text) in the other language, only for supported cases with >=4 keys; "
                       "ambiguous = shared label (see ambiguity.rule); length = canonical keys if supported, "
                       "else UTF-16 length; unique types = first occurrence per (language, role, exact text); "
                       "membership slices are relative to the case's typed layout. hunspellNoPfxSfx is NOT "
                       "no-morphology: case, compounds and other directives remain"));
    set(root, "limitations",
        QStringLiteral("same-source new block, not new-domain independence; Tatoeba example sentences, not user "
                       "input; Hunspell applies its own normalization (case rules, trailing-period handling, "
                       "IGNORE/ICONV) inside spell; control removes only PFX/SFX rows; oracle token boundaries; "
                       "repeated words are not independent trials and no confidence is claimed; resource sizes "
                       "are file sizes, not RSS"));
    set(root, "timings", timings);

    if (!writeStdout(root))
        return fail(QStringLiteral("cannot write stdout"), kExitOutput);
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
                       {"--morph-manifest", &opt.morphManifest}},
                      json, error)) {
        printUsage();
        return fail(error, kExitUsage);
    }
    if (opt.manifest.isEmpty() || opt.en.isEmpty() || opt.ru.isEmpty() || opt.evalManifest.isEmpty()
        || opt.evalEn.isEmpty() || opt.evalRu.isEmpty() || opt.protocol.isEmpty() || opt.morphManifest.isEmpty()
        || !json)
        return usageError("--manifest, --en, --ru, --eval-manifest, --eval-en, --eval-ru, --protocol, "
                          "--morph-manifest and --json are required");
    return runBenchmark(opt);
}
