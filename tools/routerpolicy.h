#pragma once

// Опыт 006 (SYS-238): чистые политики маршрутизации и строгие парсеры
// developer-only CLI tools/router_decisions.
//
// Протокол data/experiment-006-protocol.json закреплён точным SHA-256 (он
// проверяется первым). Признаки — те же 12 колонок опыта 005: строки
// разбирает неизменённый context::parseFeatures, по одному шарду за запуск.
// Политики получают только context::FeatureRow: в нём нет gold intent, kind,
// split, документа, порядкового номера и хешей слов. Модель, словари,
// Hunspell и корпус здесь не загружаются; membership и token-действия
// приходят готовыми из подготовки Codex.
//
// Функции работают с байтами и строками в памяти: не открывают файлы, не
// пишут на диск, не логируют, не используют сеть и процессы. Тексты ошибок
// не содержат слов, путей, номеров шардов и строк.

#include "contextpolicy.h"
#include "ngramdetector.h"

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QStringView>

#include <array>
#include <optional>
#include <vector>

namespace rasklad::router {

using context::ContextVote;
using context::FeatureRow;
using context::FieldKind;
using context::PolicyAction;

// ---- Закреплённый протокол и пределы ----

inline constexpr char kProtocolPath[] = "data/experiment-006-protocol.json";
inline constexpr char kProtocolSha256[] = "0dbd4b61d265293a071a01fc74e047d968fd3dc264d2b0d0b3c37811a703560b";
inline constexpr qint64 kMaxProtocolBytes = 32768;

inline constexpr char kManifestPath[] = "data/evaluation-006-sources.json";
inline constexpr qint64 kMaxManifestBytes = 64LL * 1024;
inline constexpr char kManifestRequiredStatus[] = "approved-local-evaluation";
inline constexpr char kManifestRequiredScope[] = "local-evaluation-only";
inline constexpr int kMaxManifestNotices = 16;

// Шарды: build/experiment-006-data/features-NN.tsv, NN = 00..31 = индекс в
// массиве features паспорта.
inline constexpr int kMaxShards = 32;
inline constexpr char kShardDirectory[] = "build/experiment-006-data";
inline constexpr qint64 kMaxShardBytes = 8LL * 1024 * 1024;
inline constexpr qint64 kMaxShardRows = 100000;
// limits.source_bytes протокола; CLI исходные документы не читает.
inline constexpr qint64 kMaxSourceBytes = 1048576;

// Каждый шард разбирает context::parseFeatures со своими пределами 005.
static_assert(kMaxShardBytes == context::kMaxFeatureBytes, "shard byte limit must equal the 005 parser limit");
static_assert(kMaxShardRows == context::kMaxFeatureRows, "shard row limit must equal the 005 parser limit");

// Общий guard новых router-политик: 4..32 канонических клавиш.
inline constexpr int kMinRouterKeys = 4;
static_assert(kMinRouterKeys == kMinDecisionKeys, "router guard must equal the token decision guard");

// Вывод строго меньше 3 MiB (100000 строк дают не более 1688890 байт).
inline constexpr qint64 kMaxOutputBytes = 3LL * 1024 * 1024;

// ---- Политики ----

enum class RouterPolicy {
    AlwaysKeep,
    SafeFull,
    SafeConfirmed,
    ContextConfirmed,
    Hybrid,
    ContextHybrid,
    ProposalsOnly,
    ScriptContext,
};
inline constexpr int kRouterPolicyCount = 8;
// Ровно порядок protocol.policies.
inline constexpr std::array<RouterPolicy, kRouterPolicyCount> kRouterPolicies{{
    RouterPolicy::AlwaysKeep,
    RouterPolicy::SafeFull,
    RouterPolicy::SafeConfirmed,
    RouterPolicy::ContextConfirmed,
    RouterPolicy::Hybrid,
    RouterPolicy::ContextHybrid,
    RouterPolicy::ProposalsOnly,
    RouterPolicy::ScriptContext,
}};

[[nodiscard]] const char *routerPolicyName(RouterPolicy policy);

// Только Correct меняет текст без участия пользователя. Suggest — предложение,
// Keep и Abstain — отсутствие изменения.
[[nodiscard]] bool isAutomaticEdit(PolicyAction action);

// ---- Аргументы CLI ----

struct Arguments {
    QString protocol;
    QString manifest;
    int shard = -1;
};

enum class ArgumentsError { None, Unknown, Duplicate, MissingValue, BadValue, BadShard, Missing };

struct ArgumentsResult {
    ArgumentsError error = ArgumentsError::None;
    Arguments arguments;
};

[[nodiscard]] QString argumentsErrorText(ArgumentsError error);

// Десятичное 0..31 только из ASCII-цифр, без ведущего нуля, знака и пробелов.
[[nodiscard]] bool parseShardIndex(const QString &value, int &out);

// "build/experiment-006-data/features-NN.tsv" для 0..31, иначе пустая строка.
[[nodiscard]] QString shardFeaturesPath(int index);

// args[0] — имя программы. Ровно --protocol <путь>, --manifest <путь> и
// --shard <0..31>, каждый один раз, в любом порядке; значение непустое и не
// начинается с "--"; других аргументов нет.
[[nodiscard]] ArgumentsResult parseArguments(const QStringList &args);

// ---- Протокол 006 ----

enum class ProtocolError {
    None,
    TooLarge,
    // SHA-256 файла не равен kProtocolSha256.
    HashMismatch,
    InvalidJson,
    // Хеш совпал, но policies/limits расходятся с константами этого кода.
    FieldMismatch,
};

[[nodiscard]] QString protocolErrorText(ProtocolError error);

// Сначала размер <= 32768, затем точный SHA-256, затем validateProtocolFields.
[[nodiscard]] ProtocolError validateProtocol006(const QByteArray &bytes);

// Без проверки хеша (для тестов согласованности кода): schema_version == 1;
// policies — ровно kRouterPolicies по именам и порядку; limits — ровно
// source_bytes, max_shards, shard_bytes, shard_rows, manifest_bytes,
// protocol_bytes с константами этого модуля.
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
    // features не массив 1..32 объектов в порядке индексов, плохие path/sha256/bytes/rows.
    BadFeatures,
    // notices не массив 1..16 объектов {notice, notice_sha256} без повторов.
    BadNotices,
};

[[nodiscard]] QString manifestErrorText(ManifestError error);

struct ShardEntry {
    QString path;
    QString sha256;
    qint64 bytes = 0;
    qint64 rows = 0;
};

struct ManifestNotice {
    QString notice;
    QString sha256;
};

struct EvaluationManifest {
    std::vector<ShardEntry> shards;
    std::vector<ManifestNotice> notices;
};

struct ManifestResult {
    ManifestError error = ManifestError::None;
    EvaluationManifest manifest;
};

// Размер <= 64 KiB; schema_version — целое 1; status/scope равны
// обязательным; release_approved — логическое false; protocol_sha256 равен
// закреплённому. features: массив 1..32 объектов; у элемента i path ровно
// shardFeaturesPath(i), sha256 — 64 строчных hex, bytes — целое 1..8 MiB,
// rows — целое 1..100000 (сумма по шардам не ограничена). notices: 1..16
// объектов, notice — безопасный путь под data/notices/, notice_sha256 — 64
// строчных hex, без повторов путей. Прочие поля (описание исходных
// документов, labels, отбора) допускаются и не используются: эти файлы
// модуль и CLI не открывают.
[[nodiscard]] ManifestResult parseManifest(const QByteArray &bytes);

// ---- Решения ----

// canonicalizeToken(target, layout) Ok и 4..32 клавиш. Sentinel U+FFFD
// unsupported-цели, цифры, смешанная письменность и 1..3 клавиши — false.
[[nodiscard]] bool isRouterTarget(QStringView target, SourceLayout layout);

// Голос 005 (left3 и left4) указывает на раскладку, противоположную
// набранной: Ru при us, En при ru. None — false.
[[nodiscard]] bool voteOpposesLayout(ContextVote vote, SourceLayout layout);

// Общие ворота новых router-политик, строго по порядку: protected => Keep;
// не prose (unknown) => Abstain; не isRouterTarget => Abstain; fullObserved
// => Keep; sharedAmbiguous => Abstain. nullopt — ворота пройдены. Контекст
// здесь не учитывается.
[[nodiscard]] std::optional<PolicyAction> commonRouterGate(const FeatureRow &row);

// Все решения — чистые функции FeatureRow; gold labels и порядковый номер
// строки недоступны.
[[nodiscard]] PolicyAction decideSafeFull(const FeatureRow &row);
[[nodiscard]] PolicyAction decideSafeConfirmed(const FeatureRow &row);
[[nodiscard]] PolicyAction decideContextConfirmed(const FeatureRow &row);
[[nodiscard]] PolicyAction decideHybrid(const FeatureRow &row);
[[nodiscard]] PolicyAction decideContextHybrid(const FeatureRow &row);
[[nodiscard]] PolicyAction decideProposalsOnly(const FeatureRow &row);
// Ровно context::decideContextAuto (guard >= 2 и ворота 005), без общих ворот.
[[nodiscard]] PolicyAction decideScriptContext(const FeatureRow &row);
[[nodiscard]] PolicyAction decide(RouterPolicy policy, const FeatureRow &row);

struct RouterRow {
    ContextVote vote = ContextVote::None;
    std::array<PolicyAction, kRouterPolicyCount> actions{};
};

[[nodiscard]] RouterRow evaluateRow(const FeatureRow &row);

// "ordinal\t<U|R|->\t<8 кодов K/C/A/S>\n"; ordinal — локальный номер в шарде.
void appendOutputRow(QByteArray &out, qint64 ordinal, const RouterRow &row);

// Весь вывод шарда: по строке на вход в исходном порядке, ordinal 0..n-1.
// Пустой результат, если строк больше kMaxShardRows или вывод не меньше
// kMaxOutputBytes.
[[nodiscard]] QByteArray renderOutput(const std::vector<FeatureRow> &rows);

} // namespace rasklad::router
