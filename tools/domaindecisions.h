#pragma once

// Опыт 004 (SYS-236): чистые helper'ы developer-only CLI tools/domain_decisions.
//
// Протокол data/experiment-004-protocol.json закреплён точным SHA-256; паспорт
// случаев и TSV случаев (их готовит Codex) разбираются строго, до любых
// вычислений. Модель и stem-словари опыта 002 загружаются из закреплённых
// байтов: обучения, подбора порогов и новых моделей нет. Девять действий —
// это decideMorph опыта 003 и четыре варианта с oracle-защитой кода.
//
// Функции работают с байтами в памяти: не открывают файлы, не пишут на диск,
// не логируют, не используют сеть и процессы. Сообщения об ошибках не
// содержат слов, путей, идентификаторов документов и номеров строк.

#include "benchmarkeval.h"
#include "morphologyeval.h"
#include "ngramdetector.h"

#include <QByteArray>
#include <QString>

#include <array>
#include <vector>

namespace rasklad::domain {

// ---- Закреплённые зависимости ----

inline constexpr char kProtocolPath[] = "data/experiment-004-protocol.json";
inline constexpr char kProtocolSha256[] = "9e1c9a17f9eb1ca1eb1f19643ae987bf98e2ed610611d12aaf2168964cf603e2";
inline constexpr qint64 kMaxProtocolBytes = 64LL * 1024;

// Паспорт морфологии опыта 003, неизменный (протокол 004: morphology_manifest).
inline constexpr char kMorphManifestPath[] = "data/morphology-003-sources.json";
inline constexpr char kMorphManifestSha256[] = "590b2b69c75f73d6f5ebd817dc4e2f2798d877f090ac42a81fa0a02f5fbf2413";

// Протокол 002 (пороги twoScore/singleD) и паспорт словарей опыта 001: их
// SHA-256 записаны в artifact-metadata.json закреплённых артефактов.
inline constexpr char kProtocol002Path[] = "data/experiment-002-protocol.json";
inline constexpr char kProtocol002Sha256[] = "4e38458069c2aa0eb884392dc8eda841d2ecf1f27bcb7e2174c87d1e6c1889b8";
inline constexpr char kTrainingManifestPath[] = "data/sources.json";
inline constexpr char kTrainingManifestSha256[] = "29042aee03f871e0daa7da36e9401f35141594be798f6e9d411e878d8694cd20";

struct PinnedFile {
    const char *path;
    const char *fileName;
    const char *sha256;
    qint64 bytes;
};

inline constexpr char kArtifactDir[] = "build/experiment-002-artifacts/";
inline constexpr PinnedFile kModelFile{"build/experiment-002-artifacts/detector-model.rskn", "detector-model.rskn",
                                       "e92e549e5f0853e4cd78d6d997653098b78550eb9911de11421434e26e3985d8", 88290};
inline constexpr PinnedFile kStemFile{"build/experiment-002-artifacts/exact-full-lexicons.rskp",
                                      "exact-full-lexicons.rskp",
                                      "3bdd740d4b39bf2b5fe361e0d3c670187563ec59f41913db3525340a30887871", 1200379};
inline constexpr char kArtifactMetadataPath[] = "build/experiment-002-artifacts/artifact-metadata.json";
inline constexpr char kArtifactModificationsPath[] = "build/experiment-002-artifacts/MODIFICATIONS.txt";
// Копии полных notices рядом с производными артефактами. Индекс — DetectorLanguage.
inline constexpr std::array<const char *, 2> kArtifactNoticePaths{{
    "build/experiment-002-artifacts/en_US.NOTICE.txt",
    "build/experiment-002-artifacts/ru_RU.NOTICE.txt",
}};
inline constexpr qint64 kMaxArtifactMetadataBytes = 64LL * 1024;

// ---- Протокол 004 ----

enum class ProtocolError {
    None,
    TooLarge,
    // SHA-256 файла не равен kProtocolSha256.
    HashMismatch,
    InvalidJson,
    // Хеш совпал, но поля model/stem/morphology_manifest/methods расходятся с
    // константами этого кода (защита от опечатки в коде).
    FieldMismatch,
};

[[nodiscard]] QString protocolErrorText(ProtocolError error);

// Сначала размер <= 64 KiB, затем точный SHA-256, затем согласованность полей.
[[nodiscard]] ProtocolError validateProtocol004(const QByteArray &bytes);

// ---- Методы ----

enum class DomainMethod {
    AlwaysKeep,
    TwoScore,
    HunspellFull,
    ModelConfirmedMorph,
    MorphThenModel,
    ProtectedTwoScore,
    ProtectedHunspellFull,
    ProtectedConfirmedMorph,
    ProtectedMorphThenModel,
};
inline constexpr int kDomainMethodCount = 9;
// Ровно порядок protocol.methods.
inline constexpr std::array<DomainMethod, kDomainMethodCount> kDomainMethods{{
    DomainMethod::AlwaysKeep,
    DomainMethod::TwoScore,
    DomainMethod::HunspellFull,
    DomainMethod::ModelConfirmedMorph,
    DomainMethod::MorphThenModel,
    DomainMethod::ProtectedTwoScore,
    DomainMethod::ProtectedHunspellFull,
    DomainMethod::ProtectedConfirmedMorph,
    DomainMethod::ProtectedMorphThenModel,
}};

[[nodiscard]] const char *domainMethodName(DomainMethod method);

// ---- Паспорт случаев ----

inline constexpr qint64 kMaxCasesManifestBytes = 64LL * 1024;
inline constexpr char kCasesRequiredStatus[] = "approved-local-evaluation";
inline constexpr char kCasesRequiredScope[] = "local-evaluation-only";
inline constexpr char kCasesPath[] = "build/experiment-004-data/cases.tsv";
inline constexpr char kCasesPrefix[] = "build/experiment-004-data/";
inline constexpr qint64 kMaxCasesBytes = 64LL * 1024 * 1024;
inline constexpr qint64 kMaxCasesRows = 500000;
inline constexpr int kMaxCaseNotices = 12;

enum class CasesManifestError {
    None,
    TooLarge,
    InvalidJson,
    SchemaVersion,
    StatusNotApproved,
    ScopeMismatch,
    ReleaseApproved,
    // cases не объект, путь не равен kCasesPath, плохой sha256/bytes/rows.
    BadCases,
    // notices не массив 1..12 объектов {notice, notice_sha256} без повторов.
    BadNotices,
};

[[nodiscard]] QString casesManifestErrorText(CasesManifestError error);

struct CasesNotice {
    QString notice;
    QString sha256;
};

struct CasesManifest {
    QString manifestSha256;
    QString casesPath;
    QString casesSha256;
    qint64 casesBytes = 0;
    qint64 casesRows = 0;
    std::vector<CasesNotice> notices;
};

struct CasesManifestResult {
    CasesManifestError error = CasesManifestError::None;
    CasesManifest manifest;
};

// schema_version — целое 1; status/scope равны обязательным; release_approved
// — логическое false. cases: path == kCasesPath; sha256 — 64 строчных hex;
// bytes — целое в [1, 64 MiB]; rows — целое в [1, 500000]. notices: 1..12
// объектов, notice — безопасный путь под data/notices/, notice_sha256 — 64
// строчных hex, пути без повторов. Описательные поля сверх этих допускаются и
// не используются.
[[nodiscard]] CasesManifestResult parseCasesManifest(const QByteArray &bytes);

// ---- TSV случаев ----

inline constexpr int kMaxCaseLineBytes = 32768;
inline constexpr int kMaxCaseFieldUtf16 = 4096;
inline constexpr int kMaxDocumentIndex = 39;

enum class CaseRole { Valid, Protected, Wrong };

struct DomainCase {
    int document = 0;
    DetectorLanguage documentLanguage = DetectorLanguage::En;
    CaseRole role = CaseRole::Valid;
    SourceLayout layout = SourceLayout::Us;
    QString text;
    QString expected;
};

enum class CasesTsvError {
    None,
    TooLarge,
    Empty,
    MissingFinalNewline,
    TooManyRows,
    RowCountMismatch,
    EmptyLine,
    LineTooLong,
    CarriageReturn,
    InvalidUtf8,
    BadColumns,
    BadDocument,
    BadLanguage,
    BadRole,
    BadLayout,
    EmptyField,
    FieldTooLong,
    // Управляющий символ (Cc) или пробельный символ Unicode внутри text/expected.
    BadCharacter,
    // valid/protected: expected != text.
    ExpectedDiffers,
    // wrong: expected не 1..32 букв алфавита, противоположного набранной раскладке.
    BadWrongExpected,
    // wrong: convertLayout(text, correctionDirection(layout)) != expected.
    WrongConversion,
    // wrong: convertLayout(expected, в набранную раскладку) != text.
    WrongNotGenerated,
};

[[nodiscard]] QString casesTsvErrorText(CasesTsvError error);

struct CasesTsvResult {
    CasesTsvError error = CasesTsvError::None;
    std::vector<DomainCase> cases;
};

// Порядок проверок: размер <= 64 MiB; непустой; последний байт '\n'; число
// '\n' <= 500000 и равно expectedRows; ни одного '\r'; ни одной пустой строки
// и строки длиннее 32768 байт — всё по сырым байтам до декодирования и
// разбиения. Затем весь буфер — строгий UTF-8. Строка — ровно шесть полей
// через '\t': документ (десятичное 0..39 без ведущих нулей), язык документа
// en|ru, роль valid|protected|wrong, набранная раскладка us|ru, text,
// expected. text и expected непустые, <= 4096 UTF-16, без символов Cc и
// пробельных символов Unicode. valid/protected: expected == text. wrong:
// expected — только буквы языка, противоположного раскладке (ASCII латиница
// для ru, 33 буквы ru для us), длина 1..32; convertLayout(text,
// correctionDirection(layout)) == expected и обратное преобразование
// expected в набранную раскладку == text. Язык документа может отличаться от
// раскладки. Любая ошибка — отказ всего файла и пустой список.
[[nodiscard]] CasesTsvResult parseCasesTsv(const QByteArray &data, qint64 expectedRows);

// ---- Решения ----

// Пять базовых действий — decideMorph(AlwaysKeep, TwoScore, HunspellFull,
// ModelConfirmedMorph, MorphThenModel). Защищённые варианты: роль protected
// -> Keep, иначе ровно действие соответствующего базового метода.
[[nodiscard]] std::array<DecisionAction, kDomainMethodCount> decideDomain(const morphology::MorphFeatures &features,
                                                                          CaseRole role);

struct DomainRow {
    bool eligible = false;
    bool ambiguous = false;
    bool roundTrip = false;
    std::array<DecisionAction, kDomainMethodCount> actions{};
};

// Признаки: computeFeatures(canonicalizeToken(text, layout)) и queryPair для
// control/full. eligible = isDecisionEligible (supported, 4..32 клавиш);
// ambiguous = sharedAmbiguous (stem-both || control-both || full-both, stem
// сохраняется и для коротких supported); roundTrip — true для
// valid/protected, для wrong — convertLayout(text) == expected.
[[nodiscard]] DomainRow evaluateCase(const DomainCase &c, const benchmark::FeatureContext &context,
                                     const morphology::SpellPair &control, const morphology::SpellPair &full);

// K = Keep, C = Correct, A = Abstain.
[[nodiscard]] char actionCode(DecisionAction action);

// "ordinal\teligible\tambiguous\troundTrip\t<9 кодов>\n", флаги 0/1.
void appendOutputRow(QByteArray &out, qint64 ordinal, const DomainRow &row);

// Верхняя граница длины одной строки вывода (ordinal <= 6 цифр).
inline constexpr int kMaxOutputRowBytes = 6 + 1 + 2 + 2 + 2 + kDomainMethodCount + 1;

// ---- Метаданные закреплённых артефактов ----

enum class ArtifactMetadataError {
    None,
    TooLarge,
    InvalidJson,
    // artifacts.model / artifacts.exactFullLexicons: file, bytes, sha256.
    ArtifactMismatch,
    ProtocolMismatch,
    TrainingManifestMismatch,
    TrainingSourceMismatch,
    // scope не содержит release_approved=false и not approved for distribution.
    DistributionScope,
};

[[nodiscard]] QString artifactMetadataErrorText(ArtifactMetadataError error);

struct ExpectedTrainingSource {
    QString sha256;
    QString notice;
    QString noticeSha256;
};

// artifacts.model и artifacts.exactFullLexicons равны kModelFile/kStemFile;
// protocolSha256 == kProtocol002Sha256; trainingManifestSha256 ==
// kTrainingManifestSha256; trainingSources.en/ru равны ожидаемым (индекс —
// DetectorLanguage); scope запрещает распространение.
[[nodiscard]] ArtifactMetadataError verifyArtifactMetadata(const QByteArray &bytes,
                                                           const std::array<ExpectedTrainingSource, 2> &sources);

[[nodiscard]] bool isLowerHex64(const QString &value);

} // namespace rasklad::domain
