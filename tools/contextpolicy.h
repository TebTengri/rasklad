#pragma once

// Опыт 005 (SYS-237): чистые политики и строгие парсеры developer-only CLI
// tools/context_decisions.
//
// Протокол data/experiment-005-protocol.json закреплён точным SHA-256 (он
// проверяется первым, поэтому молча изменить политику через конфигурацию
// нельзя). Паспорт оценки и TSV признаков готовит Codex; здесь они
// разбираются строго, до любых вычислений. В признаках нет gold intent,
// сценариев, split и ключей: политики их не получают. Hunspell, модель,
// словари и корпус здесь не загружаются: membership и token-действия
// приходят готовыми из независимой подготовки, а этот модуль лишь сверяет их
// с guard >= 4 клавиш.
//
// Функции работают с байтами и строками в памяти: не открывают файлы, не
// пишут на диск, не логируют, не используют сеть и процессы. Тексты ошибок не
// содержат слов, путей и номеров строк.

#include "ngramdetector.h"

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QStringView>

#include <array>
#include <vector>

namespace rasklad::context {

// ---- Закреплённый протокол и пределы ----

inline constexpr char kProtocolPath[] = "data/experiment-005-protocol.json";
inline constexpr char kProtocolSha256[] = "265757d6d538c400c95243996d853a9e71f77f96892c34510d26beb0c3e23e8b";
inline constexpr qint64 kMaxProtocolBytes = 32768;

inline constexpr char kManifestPath[] = "data/evaluation-005-sources.json";
inline constexpr qint64 kMaxManifestBytes = 64LL * 1024;
inline constexpr char kManifestRequiredStatus[] = "approved-local-synthetic-evaluation";
inline constexpr char kManifestRequiredScope[] = "local-evaluation-only";
inline constexpr char kFeaturesPath[] = "build/experiment-005-data/features.tsv";
inline constexpr int kMaxManifestNotices = 12;

inline constexpr qint64 kMaxFeatureBytes = 8LL * 1024 * 1024;
inline constexpr qint64 kMaxFeatureRows = 100000;
inline constexpr int kMaxFeatureLineBytes = 4096;
inline constexpr int kMaxFieldUtf16 = 32;
inline constexpr int kFeatureColumns = 12;

// Анкор контекста: 4..32 буквы одного алфавита (ASCII EN или 33 буквы RU).
inline constexpr int kMinAnchorLetters = 4;
inline constexpr int kMaxAnchorLetters = 32;
// Цель новых context-методов: каноническая supported-последовательность
// 2..32 клавиш. Старые token-методы сохраняют kMinDecisionKeys (4).
inline constexpr int kMinContextTargetKeys = 2;

// Вывод строго меньше 3 MiB.
inline constexpr qint64 kMaxOutputBytes = 3LL * 1024 * 1024;

// ---- Методы ----

enum class ContextMethod {
    AlwaysKeep,
    TokenTwoScore,
    TokenHunspell,
    ContextAuto,
    GuardedContextAuto,
    ContextSuggest,
};
inline constexpr int kContextMethodCount = 6;
// Ровно порядок protocol.policies.
inline constexpr std::array<ContextMethod, kContextMethodCount> kContextMethods{{
    ContextMethod::AlwaysKeep,
    ContextMethod::TokenTwoScore,
    ContextMethod::TokenHunspell,
    ContextMethod::ContextAuto,
    ContextMethod::GuardedContextAuto,
    ContextMethod::ContextSuggest,
}};

[[nodiscard]] const char *contextMethodName(ContextMethod method);

// Suggest — предложение без замены текста; автоматическим исправлением не является.
enum class PolicyAction { Keep, Correct, Abstain, Suggest };

// K, C, A, S.
[[nodiscard]] char policyActionCode(PolicyAction action);

// ---- Аргументы CLI ----

struct Arguments {
    QString protocol;
    QString manifest;
};

enum class ArgumentsError { None, Unknown, Duplicate, MissingValue, BadValue, Missing };

struct ArgumentsResult {
    ArgumentsError error = ArgumentsError::None;
    Arguments arguments;
};

[[nodiscard]] QString argumentsErrorText(ArgumentsError error);

// args[0] — имя программы. Ровно --protocol <путь> и --manifest <путь>, каждый
// один раз, в любом порядке; значение непустое и не начинается с "--".
[[nodiscard]] ArgumentsResult parseArguments(const QStringList &args);

// ---- Протокол 005 ----

enum class ProtocolError {
    None,
    TooLarge,
    // SHA-256 файла не равен kProtocolSha256.
    HashMismatch,
    InvalidJson,
    // Хеш совпал, но поля policies/observables/limits расходятся с
    // константами этого кода (защита от опечатки в коде).
    FieldMismatch,
};

[[nodiscard]] QString protocolErrorText(ProtocolError error);

// Сначала размер <= 32768, затем точный SHA-256, затем validateProtocolFields.
[[nodiscard]] ProtocolError validateProtocol005(const QByteArray &bytes);

// Без проверки хеша (для тестов согласованности кода): schema_version == 1;
// policies — ровно kContextMethods по именам и порядку; observables.
// feature_fields — ровно 12 имён колонок TSV; observables.field_values —
// prose, protected, unknown; limits — ровно feature_bytes, feature_rows,
// field_utf16, protocol_bytes с константами этого модуля.
[[nodiscard]] ProtocolError validateProtocolFields(const QByteArray &bytes);

// ---- Паспорт оценки ----

enum class ManifestError {
    None,
    TooLarge,
    InvalidJson,
    SchemaVersion,
    StatusNotApproved,
    ScopeMismatch,
    ReleaseApproved,
    // protocol_sha256 не равен kProtocolSha256.
    ProtocolMismatch,
    // features не объект, путь не kFeaturesPath, плохие sha256/bytes/rows.
    BadFeatures,
    // notices не массив 1..12 объектов {notice, notice_sha256} без повторов.
    BadNotices,
};

[[nodiscard]] QString manifestErrorText(ManifestError error);

struct ManifestNotice {
    QString notice;
    QString sha256;
};

struct EvaluationManifest {
    QString featuresPath;
    QString featuresSha256;
    qint64 featuresBytes = 0;
    qint64 featuresRows = 0;
    std::vector<ManifestNotice> notices;
};

struct ManifestResult {
    ManifestError error = ManifestError::None;
    EvaluationManifest manifest;
};

// Размер <= 64 KiB; schema_version — целое 1; status/scope равны
// обязательным; release_approved — логическое false; protocol_sha256 равен
// закреплённому. features: path == kFeaturesPath, sha256 — 64 строчных hex,
// bytes — целое 1..8 MiB, rows — целое 1..100000. notices: 1..12 объектов,
// notice — безопасный путь под data/notices/, notice_sha256 — 64 строчных
// hex, без повторов путей. Остальные поля (в том числе описание labels)
// допускаются и не используются: файлы labels этот модуль и CLI не открывают.
[[nodiscard]] ManifestResult parseManifest(const QByteArray &bytes);

[[nodiscard]] bool isLowerHex64(const QString &value);

// ---- TSV признаков ----

enum class FieldKind { Prose, Protected, Unknown };

struct FeatureRow {
    SourceLayout layout = SourceLayout::Us;
    QString target;
    // left1..left4; left4 — слово непосредственно перед целью.
    std::array<QString, 4> left;
    FieldKind field = FieldKind::Prose;
    bool fullObserved = false;
    bool fullAlternative = false;
    bool sharedAmbiguous = false;
    // Только K/C/A (Suggest во входе недопустим).
    PolicyAction tokenModelAction = PolicyAction::Abstain;
    PolicyAction tokenFullAction = PolicyAction::Abstain;
};

enum class FeatureError {
    None,
    TooLarge,
    Empty,
    MissingFinalNewline,
    TooManyRows,
    RowCountMismatch,
    CarriageReturn,
    EmptyLine,
    LineTooLong,
    InvalidUtf8,
    BadColumns,
    BadLayout,
    EmptyTarget,
    FieldTooLong,
    // Символ Cc или пробельный символ Unicode внутри строкового поля.
    BadCharacter,
    BadField,
    BadFlag,
    BadAction,
    // fullObserved && fullAlternative, но sharedAmbiguous == 0.
    InconsistentAmbiguity,
    // tokenFullAction не равен guard>=4 решению по fullObserved/fullAlternative.
    InconsistentFullAction,
    // tokenModelAction не Abstain для короткой или unsupported цели.
    InconsistentModelAction,
};

[[nodiscard]] QString featureErrorText(FeatureError error);

struct FeatureResult {
    FeatureError error = FeatureError::None;
    std::vector<FeatureRow> rows;
};

// Порядок: по сырым байтам до декодирования и разбиения — размер <= 8 MiB,
// непустой, последний байт '\n', число '\n' <= 100000 и равно expectedRows,
// нет '\r', нет пустых строк, строка <= 4096 байт. Затем весь буфер — строгий
// UTF-8. Строка — ровно 12 полей через '\t' (feature_fields протокола):
// layout us|ru; target непустой; left1..left4 (пустые допустимы); field
// prose|protected|unknown; три флага 0|1; tokenModelAction и tokenFullAction
// K|C|A. Строковые поля <= 32 UTF-16 без Cc и пробельных символов Unicode.
// Согласованность: fullObserved && fullAlternative => sharedAmbiguous;
// tokenFullAction == expectedTokenFullAction; для цели вне guard (короче 4
// или unsupported) tokenModelAction == Abstain. Любая ошибка — отказ всего
// файла и пустой список.
[[nodiscard]] FeatureResult parseFeatures(const QByteArray &data, qint64 expectedRows);

// Цель старых token-методов: canonicalizeToken Ok и 4..32 клавиш.
[[nodiscard]] bool isTokenGuardTarget(QStringView target, SourceLayout layout);
// Цель новых context-методов: canonicalizeToken Ok и 2..32 клавиш.
[[nodiscard]] bool isContextTarget(QStringView target, SourceLayout layout);

// Guard>=4: observed ? Keep : alternative ? Correct : Abstain; вне guard — Abstain.
[[nodiscard]] PolicyAction expectedTokenFullAction(QStringView target, SourceLayout layout, bool fullObserved,
                                                   bool fullAlternative);

// ---- Контекст ----

// Голос письменности двух непосредственно предшествующих слов. En
// соответствует набранной раскладке us, Ru — ru.
enum class ContextVote { None, En, Ru };

// 'U' (En), 'R' (Ru), '-' (None).
[[nodiscard]] char contextVoteCode(ContextVote vote);

// Письменность одного анкора: 4..32 кодовых единиц, все — ASCII-буквы (En)
// или все — 33 буквы русского алфавита в любом регистре (Ru); иначе None.
[[nodiscard]] ContextVote anchorScript(QStringView word);

// Голос только по left3 и left4: оба анкора одной письменности — она, иначе
// None. left1/left2 намеренно не учитываются.
[[nodiscard]] ContextVote contextVote(const FeatureRow &row);

// ---- Решения ----

// Все решения — чистые функции FeatureRow; gold labels недоступны.
[[nodiscard]] PolicyAction decideContextAuto(const FeatureRow &row);
[[nodiscard]] PolicyAction decideGuardedContextAuto(const FeatureRow &row);
[[nodiscard]] PolicyAction decideContextSuggest(const FeatureRow &row);
[[nodiscard]] PolicyAction decide(ContextMethod method, const FeatureRow &row);

struct ContextRow {
    ContextVote vote = ContextVote::None;
    std::array<PolicyAction, kContextMethodCount> actions{};
};

[[nodiscard]] ContextRow evaluateRow(const FeatureRow &row);

// "ordinal\t<U|R|->\t<6 кодов>\n".
void appendOutputRow(QByteArray &out, qint64 ordinal, const ContextRow &row);

// Весь вывод для проверенных строк: по строке на вход в исходном порядке.
// Пустой результат, если строк больше kMaxFeatureRows или вывод не меньше
// kMaxOutputBytes.
[[nodiscard]] QByteArray renderOutput(const std::vector<FeatureRow> &rows);

} // namespace rasklad::context
