// Тесты router-политик и парсеров опыта 006 (SYS-238). Только синтетические
// fixtures в памяти и вымышленные строки; сгенерированные признаки, labels,
// корпус, словари, модель и Hunspell не читаются и не вызываются. Из
// репозитория читаются лишь замороженные протоколы 005 и 006.

#include "contextpolicy.h"
#include "detectormanifest.h"
#include "ngramdetector.h"
#include "routerpolicy.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>

#include <array>
#include <functional>
#include <initializer_list>
#include <optional>
#include <vector>

using namespace rasklad;
using namespace rasklad::router;
namespace ctx = rasklad::context;

namespace {

QByteArray readRepo(const char *relative)
{
    QFile file(QStringLiteral(RASKLAD_SOURCE_ROOT) + QLatin1Char('/') + QString::fromLatin1(relative));
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return file.readAll();
}

QString hex(char c)
{
    return QString(64, QLatin1Char(c));
}

// ---- Вымышленные анкоры и цели ----

QString en3()
{
    return QStringLiteral("please");
}
QString en4()
{
    return QStringLiteral("write");
}
QString ru3()
{
    return QStringLiteral("прошу");
}
QString ru4()
{
    return QStringLiteral("записать");
}
// Burst: те же русские анкоры, набранные в us («прошу», «записать»), — только
// ASCII-буквы, голос En; и с клавишей ';' («должен») — голос None.
QString burstUs3()
{
    return QStringLiteral("ghjie");
}
QString burstUs4()
{
    return QStringLiteral("pfgbcfnm");
}
QString burstUsPunct()
{
    return QStringLiteral("ljk;ty");
}
// Burst в ru: английские анкоры, набранные в ru («please», «write»), — голос Ru.
QString burstRu3()
{
    return QStringLiteral("здуфыу");
}
QString burstRu4()
{
    return QStringLiteral("цкшеу");
}
// us-набор русского слова (6 клавиш) и ru-набор английского (5 клавиш).
QString usLong()
{
    return QStringLiteral("ghbdtn");
}
QString ruLong()
{
    return QStringLiteral("руддщ");
}
QString sentinel()
{
    return QString(QChar(char16_t(0xFFFD)));
}

QString longTarget(SourceLayout layout)
{
    return layout == SourceLayout::Us ? usLong() : ruLong();
}

// Анкоры, указывающие на противоположную набранной раскладку, и на ту же.
std::array<QString, 2> oppositeAnchors(SourceLayout layout)
{
    return layout == SourceLayout::Us ? std::array<QString, 2>{{ru3(), ru4()}}
                                      : std::array<QString, 2>{{en3(), en4()}};
}
std::array<QString, 2> sameAnchors(SourceLayout layout)
{
    return layout == SourceLayout::Us ? std::array<QString, 2>{{en3(), en4()}}
                                      : std::array<QString, 2>{{ru3(), ru4()}};
}
std::array<QString, 2> noAnchors()
{
    return {{QString(), QString()}};
}

FeatureRow makeRow(SourceLayout layout, const QString &target, const std::array<QString, 2> &anchors,
                   FieldKind field, bool observed, bool alternative, bool ambiguous,
                   PolicyAction model = PolicyAction::Abstain)
{
    FeatureRow row;
    row.layout = layout;
    row.target = target;
    row.left = {{QString(), QString(), anchors[0], anchors[1]}};
    row.field = field;
    row.fullObserved = observed;
    row.fullAlternative = alternative;
    row.sharedAmbiguous = ambiguous;
    row.tokenFullAction = ctx::expectedTokenFullAction(target, layout, observed, alternative);
    row.tokenModelAction = model;
    return row;
}

FeatureRow prose(SourceLayout layout, const std::array<QString, 2> &anchors, bool alternative, PolicyAction model)
{
    return makeRow(layout, longTarget(layout), anchors, FieldKind::Prose, false, alternative, false, model);
}

QByteArray codes(const RouterRow &row)
{
    QByteArray out;
    for (const PolicyAction action : row.actions)
        out.append(ctx::policyActionCode(action));
    return out;
}

QByteArray codes(const FeatureRow &row)
{
    return codes(router::evaluateRow(row));
}

QByteArray codes005(const FeatureRow &row)
{
    QByteArray out;
    for (const PolicyAction action : ctx::evaluateRow(row).actions)
        out.append(ctx::policyActionCode(action));
    return out;
}

// ---- TSV ----

QString join(const QStringList &fields)
{
    return fields.join(QLatin1Char('\t'));
}

QByteArray tsv(const QStringList &lines)
{
    QByteArray out;
    for (const QString &line : lines) {
        out.append(line.toUtf8());
        out.append('\n');
    }
    return out;
}

QStringList featureFields(const QString &layout, const QString &target, const QString &left3, const QString &left4,
                          const QString &field, const char *obs, const char *alt, const char *amb, const char *model,
                          const char *full)
{
    return {layout,
            target,
            QString(),
            QString(),
            left3,
            left4,
            field,
            QString::fromLatin1(obs),
            QString::fromLatin1(alt),
            QString::fromLatin1(amb),
            QString::fromLatin1(model),
            QString::fromLatin1(full)};
}

// ---- Паспорт ----

QJsonObject shardEntry(int index, qint64 rows = 2)
{
    QJsonObject e;
    e.insert(QStringLiteral("path"), shardFeaturesPath(index));
    e.insert(QStringLiteral("sha256"), hex('a'));
    e.insert(QStringLiteral("bytes"), 100);
    e.insert(QStringLiteral("rows"), double(rows));
    return e;
}

QJsonArray shardList(int count, qint64 rows = 2)
{
    QJsonArray out;
    for (int i = 0; i < count; ++i)
        out.append(shardEntry(i, rows));
    return out;
}

QJsonObject noticeEntry(const QString &path, const QString &sha)
{
    QJsonObject n;
    n.insert(QStringLiteral("notice"), path);
    n.insert(QStringLiteral("notice_sha256"), sha);
    return n;
}

QJsonObject goodManifest(int shards = 2)
{
    QJsonObject labels;
    labels.insert(QStringLiteral("path"), QStringLiteral("build/experiment-006-data/labels-00.tsv"));
    labels.insert(QStringLiteral("sha256"), hex('c'));
    QJsonObject raw;
    raw.insert(QStringLiteral("path"), QStringLiteral("build/experiment-006-data/raw"));
    QJsonObject o;
    o.insert(QStringLiteral("schema_version"), 1);
    o.insert(QStringLiteral("status"), QStringLiteral("approved-local-evaluation"));
    o.insert(QStringLiteral("scope"), QStringLiteral("local-evaluation-only"));
    o.insert(QStringLiteral("release_approved"), false);
    o.insert(QStringLiteral("protocol_sha256"), QString::fromLatin1(kProtocolSha256));
    o.insert(QStringLiteral("description"), QStringLiteral("extra descriptive field"));
    o.insert(QStringLiteral("labels"), QJsonArray{labels});
    o.insert(QStringLiteral("raw_documents"), QJsonArray{raw});
    o.insert(QStringLiteral("selection"), QStringLiteral("ranks 20..39"));
    o.insert(QStringLiteral("features"), shardList(shards));
    o.insert(QStringLiteral("notices"),
             QJsonArray{noticeEntry(QStringLiteral("data/notices/Fixture-LICENSE.txt"), hex('b'))});
    return o;
}

ManifestError manifestError(const QJsonObject &o)
{
    return parseManifest(QJsonDocument(o).toJson()).error;
}

QJsonObject withShard(int index, const char *field, const QJsonValue &value)
{
    QJsonObject o = goodManifest();
    QJsonArray shards = o.value(QStringLiteral("features")).toArray();
    QJsonObject e = shards.at(index).toObject();
    if (value.isUndefined())
        e.remove(QString::fromLatin1(field));
    else
        e.insert(QString::fromLatin1(field), value);
    shards[index] = e;
    o.insert(QStringLiteral("features"), shards);
    return o;
}

QJsonObject withNotices(const QJsonArray &notices)
{
    QJsonObject o = goodManifest();
    o.insert(QStringLiteral("notices"), notices);
    return o;
}

QStringList cli(std::initializer_list<const char *> items)
{
    QStringList out{QStringLiteral("router_decisions")};
    for (const char *item : items)
        out.append(QString::fromUtf8(item));
    return out;
}

QByteArray mutatedProtocol(const std::function<void(QJsonObject &)> &mutate)
{
    QJsonObject o = QJsonDocument::fromJson(readRepo(kProtocolPath)).object();
    mutate(o);
    return QJsonDocument(o).toJson();
}

// Независимая от routerpolicy.cpp запись порядка общих ворот.
std::optional<PolicyAction> referenceGate(const FeatureRow &row)
{
    if (row.field == FieldKind::Protected)
        return PolicyAction::Keep;
    if (row.field == FieldKind::Unknown)
        return PolicyAction::Abstain;
    const CanonicalToken token = canonicalizeToken(row.target, row.layout);
    if (token.status != CanonicalStatus::Ok || token.keys.size() < 4 || token.keys.size() > 32)
        return PolicyAction::Abstain;
    if (row.fullObserved)
        return PolicyAction::Keep;
    if (row.sharedAmbiguous)
        return PolicyAction::Abstain;
    return std::nullopt;
}

// '-' — ворота пройдены, иначе код решения ворот.
char gateCode(const std::optional<PolicyAction> &gate)
{
    return gate.has_value() ? ctx::policyActionCode(*gate) : '-';
}

constexpr std::size_t kSafeFull = 1;
constexpr std::size_t kSafeConfirmed = 2;
constexpr std::size_t kContextConfirmed = 3;
constexpr std::size_t kHybrid = 4;
constexpr std::size_t kContextHybrid = 5;
constexpr std::size_t kProposalsOnly = 6;
constexpr std::size_t kScriptContext = 7;

} // namespace

class RouterTests : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    // ---- Протокол 006 ----

    void frozenProtocolExactHash()
    {
        const QByteArray raw = readRepo(kProtocolPath);
        QVERIFY(!raw.isEmpty());
        QVERIFY(qint64(raw.size()) <= kMaxProtocolBytes);
        QCOMPARE(experiment::sha256Hex(raw), QString::fromLatin1(kProtocolSha256));
        QCOMPARE(validateProtocol006(raw), ProtocolError::None);
        QCOMPARE(validateProtocolFields(raw), ProtocolError::None);

        QByteArray changed = raw;
        changed[changed.size() - 2] = changed.at(changed.size() - 2) == ' ' ? '\t' : ' ';
        QCOMPARE(validateProtocol006(changed), ProtocolError::HashMismatch);
        QCOMPARE(validateProtocol006(raw + '\n'), ProtocolError::HashMismatch);
        QCOMPARE(validateProtocol006(raw.left(raw.size() - 1)), ProtocolError::HashMismatch);
        QCOMPARE(validateProtocol006(QByteArray()), ProtocolError::HashMismatch);
        QCOMPARE(validateProtocol006(QByteArray(kMaxProtocolBytes + 1, ' ')), ProtocolError::TooLarge);
        QCOMPARE(validateProtocol006(raw + QByteArray(kMaxProtocolBytes, ' ')), ProtocolError::TooLarge);

        const QByteArray reformatted = QJsonDocument::fromJson(raw).toJson(QJsonDocument::Compact);
        QCOMPARE(validateProtocolFields(reformatted), ProtocolError::None);
        QCOMPARE(validateProtocol006(reformatted), ProtocolError::HashMismatch);

        // Протоколы 005 и 006 не взаимозаменяемы.
        QCOMPARE(validateProtocol006(readRepo(ctx::kProtocolPath)), ProtocolError::HashMismatch);
        QCOMPARE(ctx::validateProtocol005(raw), ctx::ProtocolError::HashMismatch);
    }

    void protocolFieldsMatchCode()
    {
        const QJsonObject protocol = QJsonDocument::fromJson(readRepo(kProtocolPath)).object();
        const QJsonArray policies = protocol.value(QStringLiteral("policies")).toArray();
        QCOMPARE(policies.size(), qsizetype(kRouterPolicyCount));
        for (int i = 0; i < kRouterPolicyCount; ++i)
            QCOMPARE(policies.at(i).toString(),
                     QString::fromLatin1(routerPolicyName(kRouterPolicies[std::size_t(i)])));
        const std::array<const char *, kRouterPolicyCount> expected{{"alwaysKeep", "safeFull", "safeConfirmed",
                                                                     "contextConfirmed", "hybrid", "contextHybrid",
                                                                     "proposalsOnly", "scriptContext"}};
        for (std::size_t i = 0; i < expected.size(); ++i)
            QCOMPARE(QByteArray(routerPolicyName(kRouterPolicies[i])), QByteArray(expected[i]));

        const QByteArray swapped = mutatedProtocol([](QJsonObject &o) {
            QJsonArray p = o.value(QStringLiteral("policies")).toArray();
            const QJsonValue first = p.at(2);
            p[2] = p.at(3);
            p[3] = first;
            o.insert(QStringLiteral("policies"), p);
        });
        QCOMPARE(validateProtocol006(swapped), ProtocolError::HashMismatch);
        QCOMPARE(validateProtocolFields(swapped), ProtocolError::FieldMismatch);

        QCOMPARE(validateProtocolFields(mutatedProtocol([](QJsonObject &o) {
                     QJsonArray p = o.value(QStringLiteral("policies")).toArray();
                     p.append(QStringLiteral("extraPolicy"));
                     o.insert(QStringLiteral("policies"), p);
                 })),
                 ProtocolError::FieldMismatch);
        QCOMPARE(validateProtocolFields(mutatedProtocol([](QJsonObject &o) {
                     QJsonArray p = o.value(QStringLiteral("policies")).toArray();
                     p.removeLast();
                     o.insert(QStringLiteral("policies"), p);
                 })),
                 ProtocolError::FieldMismatch);
        for (const char *limit :
             {"source_bytes", "max_shards", "shard_bytes", "shard_rows", "manifest_bytes", "protocol_bytes"}) {
            QCOMPARE(validateProtocolFields(mutatedProtocol([limit](QJsonObject &o) {
                         QJsonObject limits = o.value(QStringLiteral("limits")).toObject();
                         limits.insert(QString::fromLatin1(limit), limits.value(QString::fromLatin1(limit)).toDouble() + 1);
                         o.insert(QStringLiteral("limits"), limits);
                     })),
                     ProtocolError::FieldMismatch);
            QCOMPARE(validateProtocolFields(mutatedProtocol([limit](QJsonObject &o) {
                         QJsonObject limits = o.value(QStringLiteral("limits")).toObject();
                         limits.remove(QString::fromLatin1(limit));
                         o.insert(QStringLiteral("limits"), limits);
                     })),
                     ProtocolError::FieldMismatch);
        }
        QCOMPARE(validateProtocolFields(mutatedProtocol([](QJsonObject &o) {
                     QJsonObject limits = o.value(QStringLiteral("limits")).toObject();
                     limits.insert(QStringLiteral("extra"), 1);
                     o.insert(QStringLiteral("limits"), limits);
                 })),
                 ProtocolError::FieldMismatch);
        QCOMPARE(validateProtocolFields(mutatedProtocol([](QJsonObject &o) {
                     o.insert(QStringLiteral("schema_version"), 2);
                 })),
                 ProtocolError::FieldMismatch);
        QCOMPARE(validateProtocolFields(QByteArray("[]")), ProtocolError::InvalidJson);
    }

    // ---- Неизменность опыта 005 ----

    void legacy005Unchanged()
    {
        QCOMPARE(QByteArray(ctx::kProtocolSha256),
                 QByteArray("265757d6d538c400c95243996d853a9e71f77f96892c34510d26beb0c3e23e8b"));
        QCOMPARE(QByteArray(ctx::kProtocolPath), QByteArray("data/experiment-005-protocol.json"));
        QCOMPARE(QByteArray(ctx::kFeaturesPath), QByteArray("build/experiment-005-data/features.tsv"));
        QCOMPARE(QByteArray(ctx::kManifestRequiredStatus), QByteArray("approved-local-synthetic-evaluation"));
        QCOMPARE(ctx::validateProtocol005(readRepo(ctx::kProtocolPath)), ctx::ProtocolError::None);
        QCOMPARE(ctx::kContextMethodCount, 6);
        const std::array<const char *, 6> names{
            {"alwaysKeep", "tokenTwoScore", "tokenHunspell", "contextAuto", "guardedContextAuto", "contextSuggest"}};
        for (std::size_t i = 0; i < names.size(); ++i)
            QCOMPARE(QByteArray(ctx::contextMethodName(ctx::kContextMethods[i])), QByteArray(names[i]));
        QCOMPARE(ctx::kMinContextTargetKeys, 2);
        QCOMPARE(ctx::kMinAnchorLetters, 4);

        // Те же действия 005, что и в tests/context.
        FeatureRow longRow = prose(SourceLayout::Us, oppositeAnchors(SourceLayout::Us), true, PolicyAction::Correct);
        QCOMPARE(codes005(longRow), QByteArray("KCCCCS"));
        const FeatureRow shortRow = makeRow(SourceLayout::Us, QStringLiteral("jy"), oppositeAnchors(SourceLayout::Us),
                                            FieldKind::Prose, false, true, false);
        QCOMPARE(codes005(shortRow), QByteArray("KAACCS"));
        const FeatureRow native = prose(SourceLayout::Us, sameAnchors(SourceLayout::Us), true, PolicyAction::Correct);
        QCOMPARE(codes005(native), QByteArray("KCCAAA"));
        // Вывод 005 сохраняет свой формат из шести кодов.
        QCOMPARE(ctx::renderOutput({longRow}), QByteArray("0\tR\tKCCCCS\n"));
    }

    // ---- Аргументы ----

    void arguments()
    {
        const ArgumentsResult good =
            parseArguments(cli({"--protocol", "p.json", "--manifest", "m.json", "--shard", "7"}));
        QCOMPARE(good.error, ArgumentsError::None);
        QCOMPARE(good.arguments.protocol, QStringLiteral("p.json"));
        QCOMPARE(good.arguments.manifest, QStringLiteral("m.json"));
        QCOMPARE(good.arguments.shard, 7);
        const ArgumentsResult reordered =
            parseArguments(cli({"--shard", "31", "--manifest", "m.json", "--protocol", "p.json"}));
        QCOMPARE(reordered.error, ArgumentsError::None);
        QCOMPARE(reordered.arguments.shard, 31);
        for (const char *value : {"0", "9", "10", "19", "31"})
            QCOMPARE(parseArguments(cli({"--protocol", "p", "--manifest", "m", "--shard", value})).error,
                     ArgumentsError::None);

        QCOMPARE(parseArguments(cli({})).error, ArgumentsError::Missing);
        QCOMPARE(parseArguments(cli({"--protocol", "p", "--manifest", "m"})).error, ArgumentsError::Missing);
        QCOMPARE(parseArguments(cli({"--protocol", "p", "--shard", "0"})).error, ArgumentsError::Missing);
        QCOMPARE(parseArguments(cli({"--manifest", "m", "--shard", "0"})).error, ArgumentsError::Missing);
        QCOMPARE(parseArguments(cli({"--protocol", "p", "--manifest", "m", "--shard", "0", "--shard", "1"})).error,
                 ArgumentsError::Duplicate);
        QCOMPARE(parseArguments(cli({"--protocol", "p", "--protocol", "q", "--manifest", "m", "--shard", "0"})).error,
                 ArgumentsError::Duplicate);
        QCOMPARE(parseArguments(cli({"--protocol", "p", "--manifest", "m", "--shard", "0", "--labels", "l"})).error,
                 ArgumentsError::Unknown);
        QCOMPARE(parseArguments(cli({"--protocol", "p", "--manifest", "m", "--shard", "0", "x"})).error,
                 ArgumentsError::Unknown);
        QCOMPARE(parseArguments(cli({"--protocol", "p", "--manifest", "m", "--shard=0"})).error,
                 ArgumentsError::Unknown);
        QCOMPARE(parseArguments(cli({"--protocol", "p", "--manifest", "m", "--shard"})).error,
                 ArgumentsError::MissingValue);
        QCOMPARE(parseArguments(cli({"--protocol", "p", "--shard", "--manifest", "m"})).error,
                 ArgumentsError::BadValue);
        QCOMPARE(parseArguments(cli({"--protocol", "p", "--manifest", "m", "--shard", ""})).error,
                 ArgumentsError::BadValue);
        QCOMPARE(parseArguments(cli({"--protocol", "", "--manifest", "m", "--shard", "0"})).error,
                 ArgumentsError::BadValue);
        for (const char *value : {"32", "99", "00", "01", "07", "-1", "+1", "1.0", " 1", "1 ", "0x1", "1e1", "a",
                                  "999", "4294967296", "\xef\xbc\x91", "\xd9\xa3"})
            QCOMPARE(parseArguments(cli({"--protocol", "p", "--manifest", "m", "--shard", value})).error,
                     ArgumentsError::BadShard);
        // Отказ не оставляет частично заполненных аргументов.
        const ArgumentsResult bad = parseArguments(cli({"--protocol", "p", "--manifest", "m", "--shard", "32"}));
        QVERIFY(bad.arguments.protocol.isEmpty());
        QCOMPARE(bad.arguments.shard, -1);
    }

    void shardIndexAndPath()
    {
        for (int i = 0; i < kMaxShards; ++i) {
            int parsed = -1;
            QVERIFY(parseShardIndex(QString::number(i), parsed));
            QCOMPARE(parsed, i);
            const QString path = shardFeaturesPath(i);
            QVERIFY(path.startsWith(QStringLiteral("build/experiment-006-data/features-")));
            QVERIFY(path.endsWith(QStringLiteral(".tsv")));
            QCOMPARE(path.size(), qsizetype(QStringLiteral("build/experiment-006-data/features-00.tsv").size()));
        }
        QCOMPARE(shardFeaturesPath(0), QStringLiteral("build/experiment-006-data/features-00.tsv"));
        QCOMPARE(shardFeaturesPath(9), QStringLiteral("build/experiment-006-data/features-09.tsv"));
        QCOMPARE(shardFeaturesPath(31), QStringLiteral("build/experiment-006-data/features-31.tsv"));
        QVERIFY(shardFeaturesPath(-1).isEmpty());
        QVERIFY(shardFeaturesPath(32).isEmpty());
        int untouched = 5;
        QVERIFY(!parseShardIndex(QString(), untouched));
        QVERIFY(!parseShardIndex(QStringLiteral("32"), untouched));
        QVERIFY(!parseShardIndex(QStringLiteral("05"), untouched));
        QCOMPARE(untouched, 5);
    }

    // ---- Паспорт ----

    void manifest()
    {
        const ManifestResult good = parseManifest(QJsonDocument(goodManifest()).toJson());
        QCOMPARE(good.error, ManifestError::None);
        QCOMPARE(good.manifest.shards.size(), std::size_t(2));
        QCOMPARE(good.manifest.shards[1].path, shardFeaturesPath(1));
        QCOMPARE(good.manifest.shards[0].sha256, hex('a'));
        QCOMPARE(good.manifest.shards[0].bytes, qint64(100));
        QCOMPARE(good.manifest.shards[0].rows, qint64(2));
        QCOMPARE(good.manifest.notices.size(), std::size_t(1));
        QCOMPARE(manifestError(goodManifest(1)), ManifestError::None);
        QCOMPARE(manifestError(goodManifest(kMaxShards)), ManifestError::None);
        QCOMPARE(manifestError(goodManifest(kMaxShards + 1)), ManifestError::BadFeatures);
        QCOMPARE(manifestError(goodManifest(0)), ManifestError::BadFeatures);
        // Сумма строк по шардам не ограничена: каждый шард проверяется отдельно.
        QJsonObject large = goodManifest();
        large.insert(QStringLiteral("features"), shardList(kMaxShards, kMaxShardRows));
        QCOMPARE(manifestError(large), ManifestError::None);

        QCOMPARE(parseManifest(QByteArray("[]")).error, ManifestError::InvalidJson);
        QCOMPARE(parseManifest(QByteArray("{")).error, ManifestError::InvalidJson);
        QCOMPARE(parseManifest(QByteArray(kMaxManifestBytes + 1, ' ')).error, ManifestError::TooLarge);

        QJsonObject o = goodManifest();
        o.insert(QStringLiteral("schema_version"), 2);
        QCOMPARE(manifestError(o), ManifestError::SchemaVersion);
        o.insert(QStringLiteral("schema_version"), 1.5);
        QCOMPARE(manifestError(o), ManifestError::SchemaVersion);
        o = goodManifest();
        o.insert(QStringLiteral("status"), QStringLiteral("approved-local-synthetic-evaluation"));
        QCOMPARE(manifestError(o), ManifestError::StatusNotApproved);
        o = goodManifest();
        o.insert(QStringLiteral("scope"), QStringLiteral("release"));
        QCOMPARE(manifestError(o), ManifestError::ScopeMismatch);
        o = goodManifest();
        o.insert(QStringLiteral("release_approved"), true);
        QCOMPARE(manifestError(o), ManifestError::ReleaseApproved);
        o.insert(QStringLiteral("release_approved"), QStringLiteral("false"));
        QCOMPARE(manifestError(o), ManifestError::ReleaseApproved);
        o.remove(QStringLiteral("release_approved"));
        QCOMPARE(manifestError(o), ManifestError::ReleaseApproved);
        o = goodManifest();
        o.insert(QStringLiteral("protocol_sha256"), QString::fromLatin1(ctx::kProtocolSha256));
        QCOMPARE(manifestError(o), ManifestError::ProtocolMismatch);
        o.insert(QStringLiteral("protocol_sha256"), QString::fromLatin1(kProtocolSha256).toUpper());
        QCOMPARE(manifestError(o), ManifestError::ProtocolMismatch);
        o.remove(QStringLiteral("protocol_sha256"));
        QCOMPARE(manifestError(o), ManifestError::ProtocolMismatch);

        // features: массив объектов строго в порядке индексов.
        o = goodManifest();
        o.insert(QStringLiteral("features"), shardEntry(0));
        QCOMPARE(manifestError(o), ManifestError::BadFeatures);
        o.remove(QStringLiteral("features"));
        QCOMPARE(manifestError(o), ManifestError::BadFeatures);
        o = goodManifest();
        o.insert(QStringLiteral("features"), QJsonArray{shardEntry(1), shardEntry(0)});
        QCOMPARE(manifestError(o), ManifestError::BadFeatures);
        o.insert(QStringLiteral("features"), QJsonArray{shardEntry(0), shardEntry(0)});
        QCOMPARE(manifestError(o), ManifestError::BadFeatures);
        o.insert(QStringLiteral("features"), QJsonArray{shardEntry(0), QJsonValue(shardFeaturesPath(1))});
        QCOMPARE(manifestError(o), ManifestError::BadFeatures);
        for (const char *path :
             {"build/experiment-006-data/features-0.tsv", "build/experiment-006-data/features-000.tsv",
              "build/experiment-006-data/features-01.tsv", "build/experiment-006-data/../experiment-006-data/features-00.tsv",
              "build/experiment-006-data/labels-00.tsv", "/tmp/features-00.tsv", "build/experiment-006-data/features-00.tsv/",
              "build/experiment-005-data/features.tsv", "data/features-00.tsv", "./build/experiment-006-data/features-00.tsv",
              "build/experiment-006-data/features-00.TSV"})
            QCOMPARE(manifestError(withShard(0, "path", QString::fromLatin1(path))), ManifestError::BadFeatures);
        QCOMPARE(manifestError(withShard(1, "path", shardFeaturesPath(0))), ManifestError::BadFeatures);
        QCOMPARE(manifestError(withShard(0, "path", QJsonValue())), ManifestError::BadFeatures);
        QCOMPARE(manifestError(withShard(0, "sha256", hex('A'))), ManifestError::BadFeatures);
        QCOMPARE(manifestError(withShard(1, "sha256", QString(63, QLatin1Char('a')))), ManifestError::BadFeatures);
        QCOMPARE(manifestError(withShard(1, "sha256", QJsonValue())), ManifestError::BadFeatures);
        QCOMPARE(manifestError(withShard(0, "bytes", 0)), ManifestError::BadFeatures);
        QCOMPARE(manifestError(withShard(0, "bytes", double(kMaxShardBytes + 1))), ManifestError::BadFeatures);
        QCOMPARE(manifestError(withShard(0, "bytes", 10.5)), ManifestError::BadFeatures);
        QCOMPARE(manifestError(withShard(0, "bytes", QStringLiteral("100"))), ManifestError::BadFeatures);
        QCOMPARE(manifestError(withShard(0, "bytes", double(kMaxShardBytes))), ManifestError::None);
        QCOMPARE(manifestError(withShard(1, "rows", 0)), ManifestError::BadFeatures);
        QCOMPARE(manifestError(withShard(1, "rows", double(kMaxShardRows + 1))), ManifestError::BadFeatures);
        QCOMPARE(manifestError(withShard(1, "rows", -1)), ManifestError::BadFeatures);
        QCOMPARE(manifestError(withShard(1, "rows", double(kMaxShardRows))), ManifestError::None);

        QCOMPARE(manifestError(withNotices(QJsonArray())), ManifestError::BadNotices);
        QJsonArray many;
        for (int i = 0; i <= kMaxManifestNotices; ++i)
            many.append(noticeEntry(QStringLiteral("data/notices/n%1.txt").arg(i), hex('b')));
        QCOMPARE(manifestError(withNotices(many)), ManifestError::BadNotices);
        many.removeLast();
        QCOMPARE(manifestError(withNotices(many)), ManifestError::None);
        const QJsonObject n = noticeEntry(QStringLiteral("data/notices/a.txt"), hex('b'));
        QCOMPARE(manifestError(withNotices({n, n})), ManifestError::BadNotices);
        QCOMPARE(manifestError(withNotices({noticeEntry(QStringLiteral("data/other/a.txt"), hex('b'))})),
                 ManifestError::BadNotices);
        QCOMPARE(manifestError(withNotices({noticeEntry(QStringLiteral("data/notices/../x.txt"), hex('b'))})),
                 ManifestError::BadNotices);
        QCOMPARE(manifestError(withNotices({noticeEntry(QStringLiteral("data/notices/a.txt"), hex('B'))})),
                 ManifestError::BadNotices);
        QCOMPARE(manifestError(withNotices({QJsonValue(QStringLiteral("data/notices/a.txt"))})),
                 ManifestError::BadNotices);
        o = goodManifest();
        o.remove(QStringLiteral("notices"));
        QCOMPARE(manifestError(o), ManifestError::BadNotices);

        const ManifestResult bad = parseManifest(QJsonDocument(withShard(1, "rows", 0)).toJson());
        QVERIFY(bad.manifest.shards.empty());
        QVERIFY(bad.manifest.notices.empty());
    }

    // ---- Общие ворота ----

    void commonGateOrder()
    {
        const SourceLayout us = SourceLayout::Us;
        // protected раньше всего: даже короткая цель, observed и ambiguity.
        QCOMPARE(gateCode(commonRouterGate(makeRow(us, QStringLiteral("jy"), noAnchors(), FieldKind::Protected, true,
                                                   true, true, PolicyAction::Correct))),
                 'K');
        QCOMPARE(gateCode(commonRouterGate(
                     makeRow(us, sentinel(), noAnchors(), FieldKind::Protected, false, true, false))),
                 'K');
        // unknown раньше observed-Keep.
        QCOMPARE(gateCode(commonRouterGate(makeRow(us, usLong(), sameAnchors(us), FieldKind::Unknown, true, false,
                                                   false, PolicyAction::Keep))),
                 'A');
        // Недопустимая цель раньше observed-Keep.
        QCOMPARE(gateCode(commonRouterGate(makeRow(us, QStringLiteral("jyf"), sameAnchors(us), FieldKind::Prose, true,
                                                   false, false))),
                 'A');
        // observed раньше ambiguity.
        QCOMPARE(gateCode(commonRouterGate(makeRow(us, usLong(), oppositeAnchors(us), FieldKind::Prose, true, true,
                                                   true, PolicyAction::Correct))),
                 'K');
        QCOMPARE(gateCode(commonRouterGate(makeRow(us, usLong(), oppositeAnchors(us), FieldKind::Prose, false, true,
                                                   true, PolicyAction::Correct))),
                 'A');
        // Пройденные ворота не зависят от контекста, alternative и модели.
        for (const auto &anchors : {oppositeAnchors(us), sameAnchors(us), noAnchors()}) {
            for (const bool alt : {false, true}) {
                for (const PolicyAction model : {PolicyAction::Keep, PolicyAction::Correct, PolicyAction::Abstain})
                    QVERIFY(!commonRouterGate(prose(us, anchors, alt, model)).has_value());
            }
        }
    }

    void tinyAndUnsupportedTargets()
    {
        for (const SourceLayout layout : {SourceLayout::Us, SourceLayout::Ru}) {
            const bool isUs = layout == SourceLayout::Us;
            const QString t1 = isUs ? QStringLiteral("j") : QStringLiteral("р");
            const QString t2 = isUs ? QStringLiteral("jy") : QStringLiteral("ру");
            const QString t3 = isUs ? QStringLiteral("jyf") : QStringLiteral("руд");
            const QString t4 = isUs ? QStringLiteral("ghbd") : QStringLiteral("рудд");
            // U+0430 — кириллическая «а» (клавиша f).
            const QString t32 = isUs ? QString(32, QLatin1Char('f')) : QString(32, QChar(char16_t(0x0430)));
            const QString t33 = isUs ? QString(33, QLatin1Char('f')) : QString(33, QChar(char16_t(0x0430)));
            const QString mixed = isUs ? QStringLiteral("abвгд") : QStringLiteral("руdd");
            QVERIFY(!isRouterTarget(QString(), layout));
            QVERIFY(!isRouterTarget(t1, layout));
            QVERIFY(!isRouterTarget(t2, layout));
            QVERIFY(!isRouterTarget(t3, layout));
            QVERIFY(isRouterTarget(t4, layout));
            QVERIFY(isRouterTarget(t32, layout));
            QVERIFY(!isRouterTarget(t33, layout));
            QVERIFY(!isRouterTarget(sentinel(), layout));
            QVERIFY(!isRouterTarget(QStringLiteral("1234"), layout));
            QVERIFY(!isRouterTarget(mixed, layout));

            const auto opp = oppositeAnchors(layout);
            // Новые методы: 1..3 клавиши и unsupported — Abstain даже при
            // согласных словаре, модели и контексте; legacy scriptContext
            // сохраняет guard >= 2.
            for (const QString &target : {t2, t3}) {
                const FeatureRow row = makeRow(layout, target, opp, FieldKind::Prose, false, true, false,
                                               PolicyAction::Correct);
                QCOMPARE(codes(row), QByteArray("KAAAAAAC"));
            }
            for (const QString &target : {t1, sentinel(), QStringLiteral("1234"), mixed, t33}) {
                const FeatureRow row = makeRow(layout, target, opp, FieldKind::Prose, false, true, false,
                                               PolicyAction::Correct);
                QCOMPARE(codes(row), QByteArray("KAAAAAAA"));
                FeatureRow protectedRow = row;
                protectedRow.field = FieldKind::Protected;
                QCOMPARE(codes(protectedRow), QByteArray("KKKKKKKK"));
            }
            QCOMPARE(codes(makeRow(layout, t4, opp, FieldKind::Prose, false, true, false, PolicyAction::Correct)),
                     QByteArray("KCCCCCSC"));
        }
    }

    // ---- Таблица истинности кандидатов (ворота пройдены) ----

    void candidateTruthTable()
    {
        enum Vote { Opposite, Same, NoVote };
        struct Case {
            bool alternative;
            PolicyAction model;
            Vote vote;
            const char *expected; // все 8 политик
        };
        const PolicyAction K = PolicyAction::Keep;
        const PolicyAction C = PolicyAction::Correct;
        const PolicyAction A = PolicyAction::Abstain;
        const std::array<Case, 18> cases{{
            {true, C, Opposite, "KCCCCCSC"},
            {true, C, Same, "KCCACCSA"},
            {true, C, NoVote, "KCCACCSA"},
            {true, K, Opposite, "KCAASSSC"},
            {true, K, Same, "KCAASSSA"},
            {true, K, NoVote, "KCAASSSA"},
            {true, A, Opposite, "KCAASSSC"},
            {true, A, Same, "KCAASSSA"},
            {true, A, NoVote, "KCAASSSA"},
            {false, C, Opposite, "KAAASSSA"},
            {false, C, Same, "KAAASAAA"},
            {false, C, NoVote, "KAAASAAA"},
            {false, K, Opposite, "KAAAAAAA"},
            {false, K, Same, "KAAAAAAA"},
            {false, K, NoVote, "KAAAAAAA"},
            {false, A, Opposite, "KAAAAAAA"},
            {false, A, Same, "KAAAAAAA"},
            {false, A, NoVote, "KAAAAAAA"},
        }};
        for (const SourceLayout layout : {SourceLayout::Us, SourceLayout::Ru}) {
            for (const Case &c : cases) {
                const std::array<QString, 2> anchors = c.vote == Opposite ? oppositeAnchors(layout)
                    : c.vote == Same                                     ? sameAnchors(layout)
                                                                         : noAnchors();
                const FeatureRow row = prose(layout, anchors, c.alternative, c.model);
                QCOMPARE(codes(row), QByteArray(c.expected));
                const RouterRow r = router::evaluateRow(row);
                QCOMPARE(r.actions[kSafeFull], decideSafeFull(row));
                QCOMPARE(r.actions[kSafeConfirmed], decideSafeConfirmed(row));
                QCOMPARE(r.actions[kContextConfirmed], decideContextConfirmed(row));
                QCOMPARE(r.actions[kHybrid], decideHybrid(row));
                QCOMPARE(r.actions[kContextHybrid], decideContextHybrid(row));
                QCOMPARE(r.actions[kProposalsOnly], decideProposalsOnly(row));
                QCOMPARE(r.actions[kScriptContext], decideScriptContext(row));
            }
        }
    }

    void voteDirection()
    {
        QVERIFY(voteOpposesLayout(ContextVote::Ru, SourceLayout::Us));
        QVERIFY(voteOpposesLayout(ContextVote::En, SourceLayout::Ru));
        QVERIFY(!voteOpposesLayout(ContextVote::En, SourceLayout::Us));
        QVERIFY(!voteOpposesLayout(ContextVote::Ru, SourceLayout::Ru));
        QVERIFY(!voteOpposesLayout(ContextVote::None, SourceLayout::Us));
        QVERIFY(!voteOpposesLayout(ContextVote::None, SourceLayout::Ru));
    }

    // ---- Observed full: Keep при любом мнении модели и контекста ----

    void observedFullAlwaysKeep()
    {
        for (const SourceLayout layout : {SourceLayout::Us, SourceLayout::Ru}) {
            for (const auto &anchors : {oppositeAnchors(layout), sameAnchors(layout), noAnchors()}) {
                for (const PolicyAction model : {PolicyAction::Keep, PolicyAction::Correct, PolicyAction::Abstain}) {
                    // observed без alternative и observed+alternative (тогда ambiguity обязательна).
                    for (const bool alt : {false, true}) {
                        const FeatureRow row =
                            makeRow(layout, longTarget(layout), anchors, FieldKind::Prose, true, alt, alt, model);
                        const RouterRow r = router::evaluateRow(row);
                        for (std::size_t i = 0; i < kScriptContext; ++i)
                            QCOMPARE(r.actions[i], PolicyAction::Keep);
                        QCOMPARE(r.actions[kScriptContext], ctx::decideContextAuto(row));
                    }
                }
            }
            // Модель и контекст за замену, словарь видит слово как есть: router сохраняет,
            // legacy 005 при общей неоднозначности и противоположном голосе исправляет.
            const auto opp = oppositeAnchors(layout);
            QCOMPARE(codes(makeRow(layout, longTarget(layout), opp, FieldKind::Prose, true, false, false,
                                   PolicyAction::Correct)),
                     QByteArray("KKKKKKKK"));
            QCOMPARE(codes(makeRow(layout, longTarget(layout), opp, FieldKind::Prose, true, true, true,
                                   PolicyAction::Correct)),
                     QByteArray("KKKKKKKC"));
            QCOMPARE(codes(makeRow(layout, longTarget(layout), noAnchors(), FieldKind::Prose, true, true, true,
                                   PolicyAction::Correct)),
                     QByteArray("KKKKKKKA"));
        }
    }

    // ---- Общая неоднозначность: без автоматики и без предложений ----

    void sharedAmbiguityVeto()
    {
        for (const SourceLayout layout : {SourceLayout::Us, SourceLayout::Ru}) {
            for (const auto &anchors : {oppositeAnchors(layout), sameAnchors(layout), noAnchors()}) {
                for (const PolicyAction model : {PolicyAction::Keep, PolicyAction::Correct, PolicyAction::Abstain}) {
                    for (const bool alt : {false, true}) {
                        const FeatureRow row =
                            makeRow(layout, longTarget(layout), anchors, FieldKind::Prose, false, alt, true, model);
                        const RouterRow r = router::evaluateRow(row);
                        for (std::size_t i = kSafeFull; i < kScriptContext; ++i)
                            QCOMPARE(r.actions[i], PolicyAction::Abstain);
                    }
                }
            }
            QCOMPARE(codes(makeRow(layout, longTarget(layout), oppositeAnchors(layout), FieldKind::Prose, false, true,
                                   true, PolicyAction::Correct)),
                     QByteArray("KAAAAAAC"));
            // Вето не отменяет protected-Keep и unknown-Abstain.
            QCOMPARE(codes(makeRow(layout, longTarget(layout), oppositeAnchors(layout), FieldKind::Protected, false,
                                   true, true, PolicyAction::Correct)),
                     QByteArray("KKKKKKKK"));
            QCOMPARE(codes(makeRow(layout, longTarget(layout), oppositeAnchors(layout), FieldKind::Unknown, false, true,
                                   true, PolicyAction::Correct)),
                     QByteArray("KAAAAAAA"));
        }
    }

    // ---- Контекст: single против burst ----

    void contextSingleVsBurst()
    {
        struct Prefix {
            SourceLayout layout;
            std::array<QString, 2> single;
            std::array<QString, 2> burst;
            ContextVote burstVote;
        };
        const std::array<Prefix, 3> prefixes{{
            {SourceLayout::Us, {{ru3(), ru4()}}, {{burstUs3(), burstUs4()}}, ContextVote::En},
            {SourceLayout::Us, {{ru3(), ru4()}}, {{burstUs3(), burstUsPunct()}}, ContextVote::None},
            {SourceLayout::Ru, {{en3(), en4()}}, {{burstRu3(), burstRu4()}}, ContextVote::Ru},
        }};
        for (const Prefix &p : prefixes) {
            const FeatureRow single = prose(p.layout, p.single, true, PolicyAction::Correct);
            const FeatureRow burst = prose(p.layout, p.burst, true, PolicyAction::Correct);
            QVERIFY(voteOpposesLayout(ctx::contextVote(single), p.layout));
            QCOMPARE(ctx::contextVote(burst), p.burstVote);
            QCOMPARE(codes(single), QByteArray("KCCCCCSC"));
            QCOMPARE(codes(burst), QByteArray("KCCACCSA"));

            const FeatureRow singleModelOnly = prose(p.layout, p.single, false, PolicyAction::Correct);
            const FeatureRow burstModelOnly = prose(p.layout, p.burst, false, PolicyAction::Correct);
            QCOMPARE(codes(singleModelOnly), QByteArray("KAAASSSA"));
            QCOMPARE(codes(burstModelOnly), QByteArray("KAAASAAA"));

            // Политики без контекста не замечают сдвиг.
            for (const std::size_t i : {kSafeFull, kSafeConfirmed, kHybrid}) {
                QCOMPARE(router::evaluateRow(single).actions[i], router::evaluateRow(burst).actions[i]);
                QCOMPARE(router::evaluateRow(singleModelOnly).actions[i], router::evaluateRow(burstModelOnly).actions[i]);
            }
        }
        // Слова до двух ближайших слотов на голос не влияют: burst только в left1/left2.
        FeatureRow farBurst = prose(SourceLayout::Us, {{ru3(), ru4()}}, true, PolicyAction::Correct);
        farBurst.left[0] = burstUs3();
        farBurst.left[1] = burstUs4();
        QCOMPARE(codes(farBurst), QByteArray("KCCCCCSC"));
    }

    // ---- Suggest не является автоматическим исправлением ----

    void suggestIsNotAutomatic()
    {
        QVERIFY(isAutomaticEdit(PolicyAction::Correct));
        QVERIFY(!isAutomaticEdit(PolicyAction::Suggest));
        QVERIFY(!isAutomaticEdit(PolicyAction::Keep));
        QVERIFY(!isAutomaticEdit(PolicyAction::Abstain));
        // proposalsOnly на всех кейсах таблицы: ровно contextHybrid с C -> S.
        for (const SourceLayout layout : {SourceLayout::Us, SourceLayout::Ru}) {
            for (const auto &anchors : {oppositeAnchors(layout), sameAnchors(layout), noAnchors()}) {
                for (const bool alt : {false, true}) {
                    for (const PolicyAction model :
                         {PolicyAction::Keep, PolicyAction::Correct, PolicyAction::Abstain}) {
                        const FeatureRow row = prose(layout, anchors, alt, model);
                        const PolicyAction proposal = decideProposalsOnly(row);
                        QVERIFY(!isAutomaticEdit(proposal));
                        const PolicyAction contextHybrid = decideContextHybrid(row);
                        QCOMPARE(proposal,
                                 contextHybrid == PolicyAction::Correct ? PolicyAction::Suggest : contextHybrid);
                    }
                }
            }
        }
    }

    // ---- scriptContext — ровно contextAuto опыта 005 ----

    void scriptContextIsExact005()
    {
        // Короткие цели 2..3 клавиши и ворота 005 (голос обязателен).
        const SourceLayout us = SourceLayout::Us;
        QCOMPARE(decideScriptContext(makeRow(us, QStringLiteral("jy"), oppositeAnchors(us), FieldKind::Prose, false,
                                             true, false)),
                 PolicyAction::Correct);
        QCOMPARE(decideScriptContext(makeRow(us, QStringLiteral("jy"), noAnchors(), FieldKind::Prose, false, true,
                                             false)),
                 PolicyAction::Abstain);
        QCOMPARE(decideScriptContext(makeRow(us, QStringLiteral("j"), oppositeAnchors(us), FieldKind::Prose, false,
                                             true, false)),
                 PolicyAction::Abstain);
        // Без общих ворот router: ambiguity не накладывает вето.
        QCOMPARE(decideScriptContext(makeRow(us, usLong(), oppositeAnchors(us), FieldKind::Prose, false, true, true)),
                 PolicyAction::Correct);
        QCOMPARE(decideScriptContext(makeRow(us, usLong(), sameAnchors(us), FieldKind::Prose, true, true, true)),
                 PolicyAction::Keep);
        // Модель scriptContext не читает.
        for (const PolicyAction model : {PolicyAction::Keep, PolicyAction::Correct, PolicyAction::Abstain})
            QCOMPARE(decideScriptContext(prose(us, oppositeAnchors(us), true, model)), PolicyAction::Correct);
    }

    // ---- Перебор наблюдаемых комбинаций ----

    void exhaustiveInvariants()
    {
        const std::array<std::array<QString, 2>, 9> anchorSets{{
            {{en3(), en4()}},
            {{ru3(), ru4()}},
            {{en3(), ru4()}},
            {{QString(), en4()}},
            {{QString(), QString()}},
            {{QStringLiteral("the"), QStringLiteral("cat")}},
            {{burstUs3(), burstUs4()}},
            {{burstUs3(), burstUsPunct()}},
            {{burstRu3(), burstRu4()}},
        }};
        int checked = 0;
        for (const SourceLayout layout : {SourceLayout::Us, SourceLayout::Ru}) {
            const bool isUs = layout == SourceLayout::Us;
            const std::array<QString, 9> targets{{
                longTarget(layout),
                isUs ? QStringLiteral("ghbd") : QStringLiteral("рудд"),
                isUs ? QStringLiteral("Ghbdtn") : QStringLiteral("Руддщ"),
                isUs ? QStringLiteral("jyf") : QStringLiteral("руд"),
                isUs ? QStringLiteral("jy") : QStringLiteral("да"),
                isUs ? QStringLiteral("j") : QStringLiteral("д"),
                sentinel(),
                QStringLiteral("1234"),
                isUs ? QStringLiteral("abвгд") : QStringLiteral("руdd"),
            }};
            for (const QString &target : targets) {
                for (const FieldKind field : {FieldKind::Prose, FieldKind::Protected, FieldKind::Unknown}) {
                    for (const auto &anchors : anchorSets) {
                        for (int bits = 0; bits < 8; ++bits) {
                            const bool obs = bits & 1;
                            const bool alt = bits & 2;
                            const bool amb = bits & 4;
                            if (obs && alt && !amb)
                                continue;
                            for (const PolicyAction model :
                                 {PolicyAction::Keep, PolicyAction::Correct, PolicyAction::Abstain}) {
                                const FeatureRow row = makeRow(layout, target, anchors, field, obs, alt, amb, model);
                                const RouterRow r = router::evaluateRow(row);
                                const auto &a = r.actions;
                                QCOMPARE(r.vote, ctx::contextVote(row));
                                QCOMPARE(a[0], PolicyAction::Keep);
                                QCOMPARE(a[kScriptContext], ctx::decide(ctx::ContextMethod::ContextAuto, row));

                                const std::optional<PolicyAction> gate = referenceGate(row);
                                QCOMPARE(gateCode(commonRouterGate(row)), gateCode(gate));
                                QCOMPARE(isRouterTarget(target, layout),
                                         ctx::isTokenGuardTarget(target, layout));
                                for (std::size_t i = kSafeFull; i < kScriptContext; ++i) {
                                    if (gate.has_value())
                                        QCOMPARE(a[i], *gate);
                                    else
                                        QVERIFY(a[i] != PolicyAction::Keep);
                                }
                                // Безопасные политики не предлагают; proposalsOnly не правит.
                                for (const std::size_t i : {kSafeFull, kSafeConfirmed, kContextConfirmed})
                                    QVERIFY(a[i] != PolicyAction::Suggest);
                                QVERIFY(!isAutomaticEdit(a[kProposalsOnly]));
                                QCOMPARE(a[kProposalsOnly], a[kContextHybrid] == PolicyAction::Correct
                                                                ? PolicyAction::Suggest
                                                                : a[kContextHybrid]);
                                // Вложенность автоматических исправлений.
                                if (a[kContextConfirmed] == PolicyAction::Correct)
                                    QCOMPARE(a[kSafeConfirmed], PolicyAction::Correct);
                                if (a[kSafeConfirmed] == PolicyAction::Correct)
                                    QCOMPARE(a[kSafeFull], PolicyAction::Correct);
                                QCOMPARE(a[kHybrid] == PolicyAction::Correct,
                                         a[kSafeConfirmed] == PolicyAction::Correct);
                                QCOMPARE(a[kContextHybrid] == PolicyAction::Correct,
                                         a[kHybrid] == PolicyAction::Correct);
                                if (a[kContextHybrid] == PolicyAction::Suggest)
                                    QCOMPARE(a[kHybrid], PolicyAction::Suggest);
                                // Любое автоматическое исправление router — только prose,
                                // guard >= 4, без observed и ambiguity, со словарной альтернативой.
                                for (std::size_t i = kSafeFull; i < kScriptContext; ++i) {
                                    if (a[i] != PolicyAction::Correct)
                                        continue;
                                    QCOMPARE(field, FieldKind::Prose);
                                    QVERIFY(isRouterTarget(target, layout));
                                    QVERIFY(!obs && !amb && alt);
                                }
                                if (a[kSafeConfirmed] == PolicyAction::Correct)
                                    QCOMPARE(model, PolicyAction::Correct);
                                if (a[kContextConfirmed] == PolicyAction::Correct)
                                    QVERIFY(voteOpposesLayout(r.vote, layout));
                                // Suggest только у гибридов и только из prose без observed/ambiguity.
                                for (std::size_t i = kSafeFull; i < kScriptContext; ++i) {
                                    if (a[i] == PolicyAction::Suggest)
                                        QVERIFY(field == FieldKind::Prose && !obs && !amb);
                                }
                                // Безконтекстные политики не зависят от анкоров.
                                const FeatureRow bare = makeRow(layout, target, noAnchors(), field, obs, alt, amb, model);
                                QCOMPARE(decideSafeFull(bare), a[kSafeFull]);
                                QCOMPARE(decideSafeConfirmed(bare), a[kSafeConfirmed]);
                                QCOMPARE(decideHybrid(bare), a[kHybrid]);
                                // Детерминированность.
                                QCOMPARE(codes(router::evaluateRow(row)), codes(r));
                                ++checked;
                            }
                        }
                    }
                }
            }
        }
        QVERIFY(checked > 9000);
    }

    // ---- Коллизии и инвариантность маршрута ----

    void routingCollisionAndInvariance()
    {
        // Одинаковые наблюдаемые байты (исходное слово и порча с тем же
        // признаковым следом) получают одно действие во всех политиках.
        const QString collision =
            join(featureFields(QStringLiteral("us"), usLong(), ru3(), ru4(), QStringLiteral("prose"), "0", "1", "0",
                               "C", "C"));
        const QString other = join(featureFields(QStringLiteral("us"), usLong(), en3(), en4(), QStringLiteral("prose"),
                                                 "0", "1", "0", "C", "C"));
        const ctx::FeatureResult parsed = ctx::parseFeatures(tsv({collision, other, collision}), 3);
        QCOMPARE(parsed.error, ctx::FeatureError::None);
        const QByteArray output = router::renderOutput(parsed.rows);
        QCOMPARE(output, QByteArray("0\tR\tKCCCCCSC\n1\tU\tKCCACCSA\n2\tR\tKCCCCCSC\n"));

        // Перестановка строк меняет только локальные ordinal.
        std::vector<FeatureRow> reversed(parsed.rows.rbegin(), parsed.rows.rend());
        QCOMPARE(router::renderOutput(reversed), QByteArray("0\tR\tKCCCCCSC\n1\tU\tKCCACCSA\n2\tR\tKCCCCCSC\n"));
        std::vector<FeatureRow> rotated{parsed.rows[1], parsed.rows[0], parsed.rows[2]};
        QCOMPARE(router::renderOutput(rotated), QByteArray("0\tU\tKCCACCSA\n1\tR\tKCCCCCSC\n2\tR\tKCCCCCSC\n"));

        // left1/left2 и регистр цели не меняют ни голос, ни действия.
        const FeatureRow base = prose(SourceLayout::Us, oppositeAnchors(SourceLayout::Us), true, PolicyAction::Correct);
        const QByteArray expected = codes(base);
        for (const QString &w : {en3(), ru3(), QString(), QStringLiteral("x"), burstUs3()}) {
            FeatureRow changed = base;
            changed.left[0] = w;
            changed.left[1] = w;
            QCOMPARE(codes(changed), expected);
            QCOMPARE(router::evaluateRow(changed).vote, ContextVote::Ru);
        }
        FeatureRow upper = base;
        upper.target = QStringLiteral("GHBDTN");
        QCOMPARE(codes(upper), expected);

        // Поле tokenFullAction новые политики не читают (решает membership).
        FeatureRow fullChanged = base;
        fullChanged.tokenFullAction = PolicyAction::Abstain;
        QCOMPARE(codes(fullChanged), expected);
    }

    // ---- Шард разбирает неизменённый парсер 005 ----

    void shardParsingReuses005()
    {
        // Unsupported длинная цель — sentinel U+FFFD; token-действия обязаны быть A.
        const QString sentinelProse = join(featureFields(QStringLiteral("us"), sentinel(), ru3(), ru4(),
                                                         QStringLiteral("prose"), "0", "1", "0", "A", "A"));
        const QString sentinelProtected = join(featureFields(QStringLiteral("ru"), sentinel(), en3(), en4(),
                                                             QStringLiteral("protected"), "0", "0", "0", "A", "A"));
        const QString shortTarget = join(featureFields(QStringLiteral("us"), QStringLiteral("jyf"), ru3(), ru4(),
                                                       QStringLiteral("prose"), "0", "1", "0", "A", "A"));
        const QString unknown = join(featureFields(QStringLiteral("us"), usLong(), ru3(), ru4(),
                                                   QStringLiteral("unknown"), "0", "1", "0", "C", "C"));
        const ctx::FeatureResult parsed =
            ctx::parseFeatures(tsv({sentinelProse, sentinelProtected, shortTarget, unknown}), 4);
        QCOMPARE(parsed.error, ctx::FeatureError::None);
        QCOMPARE(router::renderOutput(parsed.rows),
                 QByteArray("0\tR\tKAAAAAAA\n1\tU\tKKKKKKKK\n2\tR\tKAAAAAAC\n3\tR\tKAAAAAAA\n"));

        QCOMPARE(ctx::parseFeatures(tsv({join(featureFields(QStringLiteral("us"), sentinel(), ru3(), ru4(),
                                                            QStringLiteral("prose"), "0", "1", "0", "C", "A"))}),
                                    1)
                     .error,
                 ctx::FeatureError::InconsistentModelAction);
        // Число строк шарда — из паспорта; несовпадение отвергает весь шард.
        QCOMPARE(ctx::parseFeatures(tsv({sentinelProse}), 2).error, ctx::FeatureError::RowCountMismatch);
        const ctx::FeatureResult partial =
            ctx::parseFeatures(tsv({sentinelProse, sentinelProse + QStringLiteral("\tx")}), 2);
        QCOMPARE(partial.error, ctx::FeatureError::BadColumns);
        QVERIFY(partial.rows.empty());
    }

    // ---- Вывод ----

    void outputFormat()
    {
        const FeatureRow a = prose(SourceLayout::Us, oppositeAnchors(SourceLayout::Us), true, PolicyAction::Correct);
        FeatureRow b = a;
        b.field = FieldKind::Protected;
        const FeatureRow c =
            makeRow(SourceLayout::Ru, ruLong(), noAnchors(), FieldKind::Unknown, false, true, false, PolicyAction::Correct);
        const QByteArray output = router::renderOutput({a, b, c});
        QCOMPARE(output, QByteArray("0\tR\tKCCCCCSC\n1\tR\tKKKKKKKK\n2\t-\tKAAAAAAA\n"));
        for (const QString &word : {usLong(), ruLong(), ru3(), ru4(), QStringLiteral("protected"),
                                    QStringLiteral("features"), QStringLiteral("00")})
            QVERIFY(!output.contains(word.toUtf8()));
        const QByteArray alphabet("0123456789\tURKCAS-\n");
        for (const char ch : output)
            QVERIFY(alphabet.contains(ch));
        for (const QByteArray &line : output.split('\n')) {
            if (line.isEmpty())
                continue;
            const QList<QByteArray> parts = line.split('\t');
            QCOMPARE(parts.size(), qsizetype(3));
            QCOMPARE(parts.at(1).size(), qsizetype(1));
            QCOMPARE(parts.at(2).size(), qsizetype(kRouterPolicyCount));
        }

        QByteArray single;
        appendOutputRow(single, 99999, router::evaluateRow(a));
        QCOMPARE(single, QByteArray("99999\tR\tKCCCCCSC\n"));
        QVERIFY(router::renderOutput({}).isEmpty());
    }

    void outputBound()
    {
        const FeatureRow row = prose(SourceLayout::Us, oppositeAnchors(SourceLayout::Us), true, PolicyAction::Correct);
        std::vector<FeatureRow> rows(std::size_t(kMaxShardRows), row);
        const QByteArray output = router::renderOutput(rows);
        // Сумма цифр порядковых номеров 0..99999 плюс 12 байт на строку.
        QCOMPARE(qint64(output.size()), qint64(488890 + 12 * kMaxShardRows));
        QVERIFY(qint64(output.size()) < kMaxOutputBytes);
        QVERIFY(output.startsWith("0\tR\tKCCCCCSC\n"));
        QVERIFY(output.endsWith("99999\tR\tKCCCCCSC\n"));
        rows.push_back(row);
        QVERIFY(router::renderOutput(rows).isEmpty());
    }
};

QTEST_GUILESS_MAIN(RouterTests)
#include "router_tests.moc"
