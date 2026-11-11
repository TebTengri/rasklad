// Developer-only CLI опыта 004 (SYS-236): действия девяти замороженных
// политик для каждого случая технического домена. Не GUI, не служба, не
// устанавливается и не входит в runtime приложения.
//
//   domain_decisions --protocol data/experiment-004-protocol.json
//       --cases-manifest <паспорт случаев> --morph-manifest data/morphology-003-sources.json
//
// Все три аргумента обязательны, каждый ровно один раз; других нет. Порядок:
//   1. Канонические пути; корни репозитория всех трёх файлов совпадают.
//   2. Протокол 004: <= 64 KiB, точный SHA-256, поля совпадают с константами.
//   3. Паспорт морфологии 003 (закреплённый SHA-256), протокол 002 (пороги) и
//      паспорт словарей опыта 001 с notices.
//   4. Паспорт случаев, его notices, TSV случаев (путь под build/ без symlink,
//      размер, SHA-256, число строк) и строгая проверка всех строк.
//   5. Морфология как в опыте 003: notices, загруженная /usr/lib Hunspell
//      (dladdr, размер, SHA-256, версия), исходные .aff/.dic, контроль noPfxSfx
//      равен детерминированному удалению строк PFX/SFX; .dic равен опыту 001.
//   6. Закреплённые артефакты опыта 002: metadata, копии notices,
//      MODIFICATIONS.txt, модель и stem-пара (размер, SHA-256, разбор, round trip).
//   7. Только затем — движки Hunspell full и control, повторная проверка
//      файлов морфологии, признаки и решения для каждой строки.
// Хеш загруженной библиотеки — проверка воспроизводимости, не sandbox:
// dynamic loader работает до неё.
//
// stdout — только после полного успеха, одной записью: по строке на случай
// в порядке ввода, без заголовка:
//   ordinal<TAB>eligible<TAB>ambiguous<TAB>roundTrip<TAB><9 кодов действий><LF>
// флаги 0/1; коды K = Keep, C = Correct, A = Abstain в порядке protocol.methods.
// Это per-case вывод для локальной агрегации в ignored build/, не публичный
// агрегат. Токены, ID документов, пути и хеши не выводятся; stderr — только
// категория ошибки. Логирования, сети, процессов и записи файлов нет.
//
// Коды выхода: 0 — успех; 1 — отказ входных данных; 2 — неверные аргументы;
// 3 — нарушена целостность; 4 — ошибка записи stdout.

#include "benchmarkeval.h"
#include "benchmarkio.h"
#include "benchmarkprotocol.h"
#include "detectormanifest.h"
#include "domaindecisions.h"
#include "frontcodedlexicon.h"
#include "hunspellengine.h"
#include "morphologydata.h"
#include "morphologyeval.h"
#include "ngramdetector.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <QStringList>
#include <QTextStream>

#include <array>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

using namespace rasklad;
using namespace rasklad::benchmark;
using namespace rasklad::morphology;
using namespace rasklad::domain;
namespace ex = rasklad::experiment;

namespace {

constexpr int kExitInput = 1;
constexpr int kExitUsage = 2;
constexpr int kExitIntegrity = 3;
constexpr int kExitOutput = 4;

constexpr qint64 kMaxModificationsBytes = 64LL * 1024;

const std::array<DetectorLanguage, 2> kLanguages{{DetectorLanguage::En, DetectorLanguage::Ru}};

struct Failure {
    QString message;
    int code = kExitInput;
};

int fail(const QString &message, int code)
{
    QTextStream(stderr) << "domain_decisions: " << message << '\n';
    return code;
}

int fail(const Failure &failure)
{
    return fail(failure.message, failure.code);
}

int usageError(const QString &message)
{
    QTextStream(stderr) << "usage: domain_decisions --protocol <experiment-004-protocol.json>\n"
                           "           --cases-manifest <cases manifest> --morph-manifest <morphology-003-sources.json>\n";
    return fail(message, kExitUsage);
}

struct Options {
    QString protocol;
    QString casesManifest;
    QString morphManifest;
};

bool parseArguments(const QStringList &args, Options &opt, QString &error)
{
    const std::array<std::pair<const char *, QString *>, 3> known{{
        {"--protocol", &opt.protocol},
        {"--cases-manifest", &opt.casesManifest},
        {"--morph-manifest", &opt.morphManifest},
    }};
    for (qsizetype i = 1; i < args.size(); ++i) {
        QString *target = nullptr;
        for (const auto &[name, value] : known) {
            if (args.at(i) == QLatin1String(name))
                target = value;
        }
        if (!target) {
            error = QStringLiteral("unknown argument");
            return false;
        }
        if (!target->isEmpty()) {
            error = QStringLiteral("duplicate option");
            return false;
        }
        if (i + 1 >= args.size()) {
            error = QStringLiteral("option without value");
            return false;
        }
        *target = args.at(++i);
        if (target->isEmpty() || target->startsWith(QLatin1String("--"))) {
            error = QStringLiteral("empty or option-like value");
            return false;
        }
    }
    if (opt.protocol.isEmpty() || opt.casesManifest.isEmpty() || opt.morphManifest.isEmpty()) {
        error = QStringLiteral("--protocol, --cases-manifest and --morph-manifest are required");
        return false;
    }
    return true;
}

// ---- Пути ----

// Канонический путь файла-манифеста и канонический корень репозитория
// (родитель его каталога). Корень по исходному аргументу и по
// каноническому пути обязан совпасть.
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
    return !root.isEmpty() && root == argumentRoot && isStrictlyInside(canonical, root);
}

// Файл <root>/<relative> под data/: ни один компонент не symlink, обычный файл.
QString resolveDataFile(const QString &root, const QString &relative)
{
    if (!isSafeProjectPath(relative, "data/"))
        return {};
    const QString joined = root + QLatin1Char('/') + relative;
    const QFileInfo info(joined);
    if (!info.exists() || !info.isFile() || info.canonicalFilePath() != joined || !isStrictlyInside(joined, root))
        return {};
    return joined;
}

// Закреплённый файл: путь, точный размер, SHA-256.
bool readPinned(const QString &canonical, qint64 bytes, const QString &sha256, const QString &label,
                QByteArray &out, Failure &failure)
{
    QString error;
    if (canonical.isEmpty()) {
        failure = {label + QStringLiteral(": file is missing, unsafe or reached through a symbolic link"),
                   kExitInput};
        return false;
    }
    if (!readBoundedFile(canonical, bytes, out, error)) {
        failure = {label + QStringLiteral(": ") + error, error == QLatin1String("file exceeds size limit")
                                                             ? kExitIntegrity
                                                             : kExitInput};
        return false;
    }
    if (!ex::bytesMatch(out, bytes, sha256)) {
        failure = {label + QStringLiteral(": size or SHA-256 differs from the pinned value"), kExitIntegrity};
        return false;
    }
    return true;
}

bool readBuildPinned(const QString &root, const char *relative, qint64 bytes, const char *sha256,
                     const QString &label, QByteArray &out, Failure &failure)
{
    QString error;
    const QString canonical = resolveProjectFile(root, QString::fromLatin1(relative), error);
    return readPinned(canonical, bytes, QString::fromLatin1(sha256), label, out, failure);
}

// Файл под build/ с SHA-256 и верхним пределом размера (размер не закреплён).
bool readBuildHashed(const QString &root, const QString &relative, qint64 limit, const QString &sha256,
                     const QString &label, QByteArray &out, Failure &failure)
{
    QString error;
    const QString canonical = resolveProjectFile(root, relative, error);
    if (canonical.isEmpty()) {
        failure = {label + QStringLiteral(": ") + error, kExitInput};
        return false;
    }
    if (!readBoundedFile(canonical, limit, out, error)) {
        failure = {label + QStringLiteral(": ") + error, kExitInput};
        return false;
    }
    if (!sha256.isEmpty() && ex::sha256Hex(out) != sha256) {
        failure = {label + QStringLiteral(": SHA-256 differs from the approved notice"), kExitIntegrity};
        return false;
    }
    return true;
}

// ---- Морфология (та же проверка, что в опыте 003) ----

struct LanguageFiles {
    QString dic;
    QString aff;
    QString control;
};

struct VerifiedMorphology {
    MorphManifest manifest;
    LoadedLibrary library;
    std::array<LanguageFiles, 2> files;
};

// Потоковая проверка обычного файла: размер <= limit и равен ref.bytes,
// SHA-256 равен ref.sha256, весь файл — строгий UTF-8. Байты сохраняются
// только при keep != nullptr (.aff до 1 MiB).
bool streamVerify(const QString &canonical, qint64 limit, const FileRef &ref, QByteArray *keep, Failure &failure,
                  const QString &label)
{
    if (keep)
        keep->clear();
    const QFileInfo info(canonical);
    if (!info.exists() || !info.isFile()) {
        failure = {label + QStringLiteral(": not a regular file"), kExitInput};
        return false;
    }
    if (info.size() > limit || info.size() != ref.bytes) {
        failure = {label + QStringLiteral(": size or SHA-256 does not match morphology manifest"), kExitIntegrity};
        return false;
    }
    QFile file(canonical);
    if (!file.open(QIODevice::ReadOnly)) {
        failure = {label + QStringLiteral(": cannot open file"), kExitInput};
        return false;
    }
    QCryptographicHash hash(QCryptographicHash::Sha256);
    QByteArray carry;
    qint64 total = 0;
    bool utf8 = true;
    for (;;) {
        const QByteArray chunk = file.read(64 * 1024);
        if (file.error() != QFileDevice::NoError) {
            failure = {label + QStringLiteral(": read error"), kExitInput};
            return false;
        }
        if (chunk.isEmpty())
            break;
        total += chunk.size();
        if (total > ref.bytes) {
            failure = {label + QStringLiteral(": file size changed while reading"), kExitIntegrity};
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
        failure = {label + QStringLiteral(": size or SHA-256 does not match morphology manifest"), kExitIntegrity};
        return false;
    }
    if (!utf8) {
        failure = {label + QStringLiteral(": file is not strict UTF-8"), kExitIntegrity};
        return false;
    }
    return true;
}

bool resolveAndVerify(const QString &root, const FileRef &ref, qint64 limit, const QString &label,
                      QString &canonical, QByteArray *keep, Failure &failure)
{
    QString error;
    canonical = resolveProjectFile(root, ref.path, error);
    if (canonical.isEmpty()) {
        failure = {label + QStringLiteral(": ") + error, kExitInput};
        return false;
    }
    return streamVerify(canonical, limit, ref, keep, failure, label);
}

bool verifyMorphology(const QString &manifestCanonical, const QByteArray &manifestBytes, const QString &root,
                      VerifiedMorphology &out, Failure &failure)
{
    QString error;
    const MorphManifestResult parsed = parseMorphManifest(manifestBytes);
    if (parsed.error != MorphManifestError::None) {
        failure = {QStringLiteral("--morph-manifest: ") + morphManifestErrorText(parsed.error), kExitInput};
        return false;
    }
    out.manifest = parsed.manifest;
    std::vector<NoticeRef> notices{out.manifest.libraryNotice};
    notices.insert(notices.end(), out.manifest.additionalLibraryNotices.begin(),
                   out.manifest.additionalLibraryNotices.end());
    for (const MorphLanguage &l : out.manifest.languages)
        notices.push_back(l.notice);
    for (const NoticeRef &n : notices) {
        if (!verifyNoticeFile(manifestCanonical, n.notice, n.sha256, error)) {
            failure = {QStringLiteral("--morph-manifest: ") + error, kExitIntegrity};
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
        QByteArray controlBytes;
        if (!resolveAndVerify(root, l.dic, kMaxMorphDicBytes, label + QStringLiteral(" dic"), files.dic, nullptr,
                              failure)
            || !resolveAndVerify(root, l.aff, kMaxAffBytes, label + QStringLiteral(" aff"), files.aff, &affBytes,
                                 failure)
            || !resolveAndVerify(root, l.controlAff, kMaxAffBytes, label + QStringLiteral(" control aff"),
                                 files.control, &controlBytes, failure))
            return false;
        const AffControlResult derived = removePfxSfxRows(affBytes);
        if (derived.error != AffControlError::None) {
            failure = {label + QStringLiteral(" aff: ") + affControlErrorText(derived.error), kExitIntegrity};
            return false;
        }
        if (derived.bytes != controlBytes) {
            failure = {label + QStringLiteral(" control aff is not the PFX/SFX-row removal of the source aff"),
                       kExitIntegrity};
            return false;
        }
    }
    return true;
}

// После Hunspell_create: файлы не изменились, пока библиотека читала их по пути.
bool recheckMorphology(const VerifiedMorphology &v, Failure &failure)
{
    for (std::size_t li = 0; li < 2; ++li) {
        const MorphLanguage &l = v.manifest.languages[li];
        const std::array<std::pair<const QString *, const FileRef *>, 3> files{
            {{&v.files[li].dic, &l.dic}, {&v.files[li].aff, &l.aff}, {&v.files[li].control, &l.controlAff}}};
        for (std::size_t f = 0; f < files.size(); ++f) {
            const qint64 limit = f == 0 ? kMaxMorphDicBytes : kMaxAffBytes;
            if (!streamVerify(*files[f].first, limit, *files[f].second, nullptr, failure,
                              QStringLiteral("morphology recheck"))) {
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

bool writeStdoutOnce(const QByteArray &bytes)
{
    QFile out;
    if (!out.open(stdout, QIODevice::WriteOnly))
        return false;
    return out.write(bytes) == bytes.size() && out.flush();
}

int run(const Options &opt)
{
    QString error;
    Failure failure;

    // ---- 1. Пути и корни ----
    QString protocolPath;
    QString casesManifestPath;
    QString morphManifestPath;
    QString root;
    QString casesRoot;
    QString morphRoot;
    if (!resolveManifest(opt.protocol, protocolPath, root))
        return fail(QStringLiteral("--protocol: not a regular file inside a repository"), kExitInput);
    if (!resolveManifest(opt.casesManifest, casesManifestPath, casesRoot))
        return fail(QStringLiteral("--cases-manifest: not a regular file inside a repository"), kExitInput);
    if (!resolveManifest(opt.morphManifest, morphManifestPath, morphRoot))
        return fail(QStringLiteral("--morph-manifest: not a regular file inside a repository"), kExitInput);
    if (casesRoot != root || morphRoot != root)
        return fail(QStringLiteral("protocol and manifests are not in the same repository"), kExitInput);
    if (protocolPath != resolveDataFile(root, QString::fromLatin1(kProtocolPath)))
        return fail(QStringLiteral("--protocol: not the repository experiment-004 protocol file"), kExitInput);
    if (morphManifestPath != resolveDataFile(root, QString::fromLatin1(kMorphManifestPath)))
        return fail(QStringLiteral("--morph-manifest: not the protocol morphology manifest file"), kExitInput);

    // ---- 2. Протокол 004 ----
    QByteArray protocolBytes;
    if (!readBoundedFile(protocolPath, domain::kMaxProtocolBytes, protocolBytes, error))
        return fail(QStringLiteral("--protocol: ") + error, kExitInput);
    const domain::ProtocolError protocolError = validateProtocol004(protocolBytes);
    if (protocolError != domain::ProtocolError::None)
        return fail(QStringLiteral("--protocol: ") + domain::protocolErrorText(protocolError), kExitIntegrity);

    // ---- 3. Паспорт морфологии, протокол 002, паспорт словарей 001 ----
    QByteArray morphManifestBytes;
    if (!readBoundedFile(morphManifestPath, kMaxMorphManifestBytes, morphManifestBytes, error))
        return fail(QStringLiteral("--morph-manifest: ") + error, kExitInput);
    if (ex::sha256Hex(morphManifestBytes) != QLatin1String(kMorphManifestSha256))
        return fail(QStringLiteral("--morph-manifest: SHA-256 differs from the experiment-003 manifest"),
                    kExitIntegrity);

    // Протокол 002: пороги frozenTwoScore/frozenSingleD сверяются с файлом целиком.
    QByteArray protocol002Bytes;
    {
        const QString path = resolveDataFile(root, QString::fromLatin1(kProtocol002Path));
        if (path.isEmpty() || !readBoundedFile(path, benchmark::kMaxProtocolBytes, protocol002Bytes, error))
            return fail(QStringLiteral("experiment-002 protocol: missing or unreadable"), kExitInput);
        if (ex::sha256Hex(protocol002Bytes) != QLatin1String(kProtocol002Sha256)
            || validateProtocol(protocol002Bytes).error != benchmark::ProtocolError::None)
            return fail(QStringLiteral("experiment-002 protocol: differs from the frozen thresholds"),
                        kExitIntegrity);
    }
    const SingleDThresholds singleD = frozenSingleD();
    const TwoScoreThresholds twoScore = frozenTwoScore();
    if (!singleD.enabled || singleD.gap != kSingleDGap || !twoScore.enabled || twoScore.gap != kTwoScoreGap
        || twoScore.minAlternative != kTwoScoreMinAlternative || twoScore.maxObserved != kTwoScoreMaxObserved)
        return fail(QStringLiteral("frozen thresholds differ from experiment 002"), kExitIntegrity);

    const QString trainingManifestPath = resolveDataFile(root, QString::fromLatin1(kTrainingManifestPath));
    QByteArray trainingBytes;
    if (trainingManifestPath.isEmpty()
        || !readBoundedFile(trainingManifestPath, ex::kMaxManifestBytes, trainingBytes, error))
        return fail(QStringLiteral("training manifest: missing or unreadable"), kExitInput);
    if (ex::sha256Hex(trainingBytes) != QLatin1String(kTrainingManifestSha256))
        return fail(QStringLiteral("training manifest: SHA-256 differs from the experiment-001 manifest"),
                    kExitIntegrity);
    const ex::ManifestParseResult training = ex::parseExperimentManifest(trainingBytes);
    if (training.error != ex::ManifestError::None)
        return fail(QStringLiteral("training manifest: ") + ex::manifestErrorText(training.error), kExitIntegrity);
    for (const ex::ManifestSource &source : training.manifest.sources) {
        if (!verifyNoticeFile(trainingManifestPath, source.notice, source.noticeSha256, error))
            return fail(QStringLiteral("training manifest: ") + error, kExitIntegrity);
    }

    // ---- 4. Паспорт случаев и TSV ----
    QByteArray casesManifestBytes;
    if (!readBoundedFile(casesManifestPath, kMaxCasesManifestBytes, casesManifestBytes, error))
        return fail(QStringLiteral("--cases-manifest: ") + error, kExitInput);
    const CasesManifestResult casesManifest = parseCasesManifest(casesManifestBytes);
    if (casesManifest.error != CasesManifestError::None)
        return fail(QStringLiteral("--cases-manifest: ") + casesManifestErrorText(casesManifest.error), kExitInput);
    const CasesManifest &cm = casesManifest.manifest;
    for (const CasesNotice &n : cm.notices) {
        if (!verifyNoticeFile(casesManifestPath, n.notice, n.sha256, error))
            return fail(QStringLiteral("--cases-manifest: ") + error, kExitIntegrity);
    }
    QByteArray caseBytes;
    {
        const QString casesPath = resolveProjectFile(root, cm.casesPath, error);
        if (casesPath.isEmpty())
            return fail(QStringLiteral("cases: ") + error, kExitInput);
        if (!readBoundedFile(casesPath, kMaxCasesBytes, caseBytes, error))
            return fail(QStringLiteral("cases: ") + error, kExitInput);
        if (!ex::bytesMatch(caseBytes, cm.casesBytes, cm.casesSha256))
            return fail(QStringLiteral("cases: size or SHA-256 does not match the cases manifest"), kExitIntegrity);
    }
    CasesTsvResult parsedCases = parseCasesTsv(caseBytes, cm.casesRows);
    caseBytes.clear();
    caseBytes.squeeze();
    if (parsedCases.error != CasesTsvError::None)
        return fail(QStringLiteral("cases: ") + casesTsvErrorText(parsedCases.error), kExitInput);
    const std::vector<DomainCase> cases = std::move(parsedCases.cases);

    // ---- 5. Морфология ----
    VerifiedMorphology morph;
    if (!verifyMorphology(morphManifestPath, morphManifestBytes, root, morph, failure))
        return fail(failure);
    std::array<ExpectedTrainingSource, 2> expectedSources;
    for (const DetectorLanguage language : kLanguages) {
        const std::size_t li = std::size_t(language);
        const ex::ManifestSource &source = training.manifest.sources[li];
        const MorphLanguage &l = morph.manifest.languages[li];
        if (l.dic.sha256 != source.sha256 || l.dic.bytes != source.bytes || l.notice.notice != source.notice
            || l.notice.sha256 != source.noticeSha256)
            return fail(QStringLiteral("--morph-manifest: dic or notice is not the approved experiment-001 entry"),
                        kExitIntegrity);
        expectedSources[li] = {source.sha256, source.notice, source.noticeSha256};
    }

    // ---- 6. Закреплённые артефакты опыта 002 ----
    {
        QByteArray metadata;
        if (!readBuildHashed(root, QString::fromLatin1(kArtifactMetadataPath), kMaxArtifactMetadataBytes, QString(),
                             QStringLiteral("artifact metadata"), metadata, failure))
            return fail(failure);
        const ArtifactMetadataError metaError = verifyArtifactMetadata(metadata, expectedSources);
        if (metaError != ArtifactMetadataError::None)
            return fail(artifactMetadataErrorText(metaError), kExitIntegrity);
        for (const DetectorLanguage language : kLanguages) {
            const std::size_t li = std::size_t(language);
            QByteArray notice;
            if (!readBuildHashed(root, QString::fromLatin1(kArtifactNoticePaths[li]), ex::kMaxNoticeBytes,
                                 expectedSources[li].noticeSha256, QStringLiteral("artifact notice copy"), notice,
                                 failure))
                return fail(failure);
        }
        QByteArray modifications;
        if (!readBuildHashed(root, QString::fromLatin1(kArtifactModificationsPath), kMaxModificationsBytes, QString(),
                             QStringLiteral("artifact modification notice"), modifications, failure))
            return fail(failure);
        if (modifications.isEmpty() || !isStrictUtf8(modifications)
            || !modifications.contains("release_approved=false"))
            return fail(QStringLiteral("artifact modification notice does not forbid release"), kExitIntegrity);
    }
    QByteArray modelBytes;
    QByteArray stemBytes;
    if (!readBuildPinned(root, kModelFile.path, kModelFile.bytes, kModelFile.sha256, QStringLiteral("model"),
                         modelBytes, failure)
        || !readBuildPinned(root, kStemFile.path, kStemFile.bytes, kStemFile.sha256, QStringLiteral("stem pair"),
                            stemBytes, failure))
        return fail(failure);
    const std::optional<NgramModel> model = NgramModel::deserialize(modelBytes);
    if (!model || !model->isValid() || model->serialize() != modelBytes)
        return fail(QStringLiteral("model: pinned bytes do not round-trip"), kExitIntegrity);
    const std::optional<std::array<FrontCodedLexicon, 2>> stem = deserializeLexiconPair(stemBytes);
    if (!stem || serializeLexiconPair(stem->at(0), stem->at(1)) != stemBytes)
        return fail(QStringLiteral("stem pair: pinned bytes do not round-trip"), kExitIntegrity);
    {
        // Случаи — отдельный источник: их хеш не совпадает ни с одной зависимостью.
        QSet<QString> others{QString::fromLatin1(kModelFile.sha256), QString::fromLatin1(kStemFile.sha256)};
        for (const MorphLanguage &l : morph.manifest.languages)
            others.unite(QSet<QString>{l.dic.sha256, l.aff.sha256, l.controlAff.sha256});
        if (others.contains(cm.casesSha256))
            return fail(QStringLiteral("cases: file duplicates a model or dictionary source"), kExitIntegrity);
    }
    modelBytes.clear();
    stemBytes.clear();

    FeatureContext context;
    context.model = &*model;
    // train не используется девятью методами; full = закреплённая stem-пара.
    context.train = {{&stem->at(0), &stem->at(1)}};
    context.full = {{&stem->at(0), &stem->at(1)}};
    context.singleD = singleD;
    context.twoScore = twoScore;
    if (!context.isValid())
        return fail(QStringLiteral("feature context is invalid"), kExitIntegrity);

    // ---- 7. Движки и решения ----
    EnginePair fullEngines;
    EnginePair controlEngines;
    if (!createPair(morph, false, fullEngines, failure) || !createPair(morph, true, controlEngines, failure))
        return fail(failure);
    if (!recheckMorphology(morph, failure))
        return fail(failure);
    const SpellPair fullSpell = fullEngines.spell();
    const SpellPair controlSpell = controlEngines.spell();

    QByteArray output;
    output.reserve(qsizetype(cases.size()) * kMaxOutputRowBytes);
    for (std::size_t i = 0; i < cases.size(); ++i) {
        const DomainRow row = evaluateCase(cases[i], context, controlSpell, fullSpell);
        if (cases[i].role == CaseRole::Wrong && !row.roundTrip)
            return fail(QStringLiteral("validated wrong row lost its round trip"), kExitIntegrity);
        appendOutputRow(output, qint64(i), row);
    }
    if (qint64(output.size()) > qint64(cases.size()) * kMaxOutputRowBytes)
        return fail(QStringLiteral("output exceeds its bound"), kExitIntegrity);
    if (!writeStdoutOnce(output))
        return fail(QStringLiteral("cannot write stdout"), kExitOutput);
    // Движки освобождаются только после вывода.
    return 0;
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    Options opt;
    QString error;
    if (!parseArguments(QCoreApplication::arguments(), opt, error))
        return usageError(error);
    return run(opt);
}
