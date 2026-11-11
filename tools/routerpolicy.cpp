#include "routerpolicy.h"

#include "detectormanifest.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QSet>

#include <cmath>
#include <utility>

namespace rasklad::router {

namespace {

QString key(const char *name)
{
    return QString::fromLatin1(name);
}

bool parseObject(const QByteArray &bytes, QJsonObject &out)
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(bytes, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
        return false;
    out = document.object();
    return true;
}

// Целое JSON-число в [min, max] без дробной части.
bool exactInteger(const QJsonValue &value, qint64 min, qint64 max, qint64 &out)
{
    if (!value.isDouble())
        return false;
    const double d = value.toDouble();
    if (!std::isfinite(d) || d != std::floor(d) || d < double(min) || d > double(max))
        return false;
    out = qint64(d);
    return true;
}

bool integerEquals(const QJsonObject &object, const char *name, qint64 expected)
{
    qint64 value = 0;
    return exactInteger(object.value(key(name)), expected, expected, value);
}

bool stringEquals(const QJsonObject &object, const char *name, const char *expected)
{
    const QJsonValue value = object.value(key(name));
    return value.isString() && value.toString() == QLatin1String(expected);
}

bool modelCorrect(const FeatureRow &row)
{
    return row.tokenModelAction == PolicyAction::Correct;
}

bool contextOpposes(const FeatureRow &row)
{
    return voteOpposesLayout(context::contextVote(row), row.layout);
}

} // namespace

const char *routerPolicyName(RouterPolicy policy)
{
    switch (policy) {
    case RouterPolicy::AlwaysKeep:
        return "alwaysKeep";
    case RouterPolicy::SafeFull:
        return "safeFull";
    case RouterPolicy::SafeConfirmed:
        return "safeConfirmed";
    case RouterPolicy::ContextConfirmed:
        return "contextConfirmed";
    case RouterPolicy::Hybrid:
        return "hybrid";
    case RouterPolicy::ContextHybrid:
        return "contextHybrid";
    case RouterPolicy::ProposalsOnly:
        return "proposalsOnly";
    case RouterPolicy::ScriptContext:
        return "scriptContext";
    }
    return "unknown";
}

bool isAutomaticEdit(PolicyAction action)
{
    return action == PolicyAction::Correct;
}

// ---- Аргументы ----

QString argumentsErrorText(ArgumentsError error)
{
    switch (error) {
    case ArgumentsError::None:
        return QStringLiteral("ok");
    case ArgumentsError::Unknown:
        return QStringLiteral("unknown argument");
    case ArgumentsError::Duplicate:
        return QStringLiteral("duplicate option");
    case ArgumentsError::MissingValue:
        return QStringLiteral("option without value");
    case ArgumentsError::BadValue:
        return QStringLiteral("empty or option-like value");
    case ArgumentsError::BadShard:
        return QStringLiteral("--shard must be a decimal index 0..31 without leading zeros");
    case ArgumentsError::Missing:
        return QStringLiteral("--protocol, --manifest and --shard are required");
    }
    return QStringLiteral("unknown arguments error");
}

bool parseShardIndex(const QString &value, int &out)
{
    if (value.isEmpty() || value.size() > 2)
        return false;
    int result = 0;
    for (const QChar ch : value) {
        const char16_t c = ch.unicode();
        if (c < u'0' || c > u'9')
            return false;
        result = result * 10 + int(c - u'0');
    }
    if (value.size() == 2 && value.at(0) == QLatin1Char('0'))
        return false;
    if (result >= kMaxShards)
        return false;
    out = result;
    return true;
}

QString shardFeaturesPath(int index)
{
    if (index < 0 || index >= kMaxShards)
        return {};
    return QString::fromLatin1(kShardDirectory) + QStringLiteral("/features-")
        + QStringLiteral("%1").arg(index, 2, 10, QLatin1Char('0')) + QStringLiteral(".tsv");
}

ArgumentsResult parseArguments(const QStringList &args)
{
    ArgumentsResult result;
    auto failWith = [&result](ArgumentsError error) {
        result.error = error;
        result.arguments = Arguments();
        return result;
    };
    bool seenProtocol = false;
    bool seenManifest = false;
    bool seenShard = false;
    QString shardValue;
    for (qsizetype i = 1; i < args.size(); ++i) {
        QString *target = nullptr;
        bool *seen = nullptr;
        if (args.at(i) == QLatin1String("--protocol")) {
            target = &result.arguments.protocol;
            seen = &seenProtocol;
        } else if (args.at(i) == QLatin1String("--manifest")) {
            target = &result.arguments.manifest;
            seen = &seenManifest;
        } else if (args.at(i) == QLatin1String("--shard")) {
            target = &shardValue;
            seen = &seenShard;
        } else {
            return failWith(ArgumentsError::Unknown);
        }
        if (*seen)
            return failWith(ArgumentsError::Duplicate);
        *seen = true;
        if (i + 1 >= args.size())
            return failWith(ArgumentsError::MissingValue);
        *target = args.at(++i);
        if (target->isEmpty() || target->startsWith(QLatin1String("--")))
            return failWith(ArgumentsError::BadValue);
        if (target == &shardValue && !parseShardIndex(shardValue, result.arguments.shard))
            return failWith(ArgumentsError::BadShard);
    }
    if (!seenProtocol || !seenManifest || !seenShard)
        return failWith(ArgumentsError::Missing);
    return result;
}

// ---- Протокол ----

QString protocolErrorText(ProtocolError error)
{
    switch (error) {
    case ProtocolError::None:
        return QStringLiteral("ok");
    case ProtocolError::TooLarge:
        return QStringLiteral("protocol exceeds size limit");
    case ProtocolError::HashMismatch:
        return QStringLiteral("protocol SHA-256 differs from the frozen experiment-006 protocol");
    case ProtocolError::InvalidJson:
        return QStringLiteral("protocol is not a JSON object");
    case ProtocolError::FieldMismatch:
        return QStringLiteral("protocol fields differ from the constants of this tool");
    }
    return QStringLiteral("unknown protocol error");
}

ProtocolError validateProtocolFields(const QByteArray &bytes)
{
    if (qint64(bytes.size()) > kMaxProtocolBytes)
        return ProtocolError::TooLarge;
    QJsonObject root;
    if (!parseObject(bytes, root))
        return ProtocolError::InvalidJson;
    if (!integerEquals(root, "schema_version", 1))
        return ProtocolError::FieldMismatch;
    const QJsonValue policiesValue = root.value(key("policies"));
    if (!policiesValue.isArray())
        return ProtocolError::FieldMismatch;
    const QJsonArray policies = policiesValue.toArray();
    if (policies.size() != qsizetype(kRouterPolicyCount))
        return ProtocolError::FieldMismatch;
    for (std::size_t i = 0; i < kRouterPolicies.size(); ++i) {
        const QJsonValue v = policies.at(qsizetype(i));
        if (!v.isString() || v.toString() != QLatin1String(routerPolicyName(kRouterPolicies[i])))
            return ProtocolError::FieldMismatch;
    }
    const QJsonValue limitsValue = root.value(key("limits"));
    if (!limitsValue.isObject())
        return ProtocolError::FieldMismatch;
    const QJsonObject limits = limitsValue.toObject();
    if (limits.size() != 6 || !integerEquals(limits, "source_bytes", kMaxSourceBytes)
        || !integerEquals(limits, "max_shards", kMaxShards)
        || !integerEquals(limits, "shard_bytes", kMaxShardBytes)
        || !integerEquals(limits, "shard_rows", kMaxShardRows)
        || !integerEquals(limits, "manifest_bytes", kMaxManifestBytes)
        || !integerEquals(limits, "protocol_bytes", kMaxProtocolBytes))
        return ProtocolError::FieldMismatch;
    return ProtocolError::None;
}

ProtocolError validateProtocol006(const QByteArray &bytes)
{
    if (qint64(bytes.size()) > kMaxProtocolBytes)
        return ProtocolError::TooLarge;
    if (experiment::sha256Hex(bytes) != QLatin1String(kProtocolSha256))
        return ProtocolError::HashMismatch;
    return validateProtocolFields(bytes);
}

// ---- Паспорт оценки ----

QString manifestErrorText(ManifestError error)
{
    switch (error) {
    case ManifestError::None:
        return QStringLiteral("ok");
    case ManifestError::TooLarge:
        return QStringLiteral("evaluation manifest exceeds size limit");
    case ManifestError::InvalidJson:
        return QStringLiteral("evaluation manifest is not a JSON object");
    case ManifestError::SchemaVersion:
        return QStringLiteral("evaluation manifest schema_version must be 1");
    case ManifestError::StatusNotApproved:
        return QStringLiteral("evaluation manifest status is not approved-local-evaluation");
    case ManifestError::ScopeMismatch:
        return QStringLiteral("evaluation manifest scope is not local-evaluation-only");
    case ManifestError::ReleaseApproved:
        return QStringLiteral("evaluation manifest release_approved must be false");
    case ManifestError::ProtocolMismatch:
        return QStringLiteral("evaluation manifest protocol_sha256 is not the frozen experiment-006 protocol");
    case ManifestError::BadFeatures:
        return QStringLiteral("evaluation manifest has an invalid features list");
    case ManifestError::BadNotices:
        return QStringLiteral("evaluation manifest has an invalid notices list");
    }
    return QStringLiteral("unknown evaluation manifest error");
}

ManifestResult parseManifest(const QByteArray &bytes)
{
    ManifestResult result;
    auto failWith = [&result](ManifestError error) {
        result.error = error;
        result.manifest = EvaluationManifest();
        return result;
    };
    if (qint64(bytes.size()) > kMaxManifestBytes)
        return failWith(ManifestError::TooLarge);
    QJsonObject root;
    if (!parseObject(bytes, root))
        return failWith(ManifestError::InvalidJson);
    if (!integerEquals(root, "schema_version", 1))
        return failWith(ManifestError::SchemaVersion);
    if (!stringEquals(root, "status", kManifestRequiredStatus))
        return failWith(ManifestError::StatusNotApproved);
    if (!stringEquals(root, "scope", kManifestRequiredScope))
        return failWith(ManifestError::ScopeMismatch);
    const QJsonValue release = root.value(key("release_approved"));
    if (!release.isBool() || release.toBool())
        return failWith(ManifestError::ReleaseApproved);
    if (!stringEquals(root, "protocol_sha256", kProtocolSha256))
        return failWith(ManifestError::ProtocolMismatch);

    EvaluationManifest &m = result.manifest;
    const QJsonValue featuresValue = root.value(key("features"));
    if (!featuresValue.isArray())
        return failWith(ManifestError::BadFeatures);
    const QJsonArray features = featuresValue.toArray();
    if (features.isEmpty() || features.size() > kMaxShards)
        return failWith(ManifestError::BadFeatures);
    for (qsizetype i = 0; i < features.size(); ++i) {
        const QJsonValue entryValue = features.at(i);
        if (!entryValue.isObject())
            return failWith(ManifestError::BadFeatures);
        const QJsonObject entry = entryValue.toObject();
        const QJsonValue path = entry.value(key("path"));
        const QJsonValue sha = entry.value(key("sha256"));
        ShardEntry shard;
        if (!path.isString() || path.toString() != shardFeaturesPath(int(i)) || !sha.isString()
            || !context::isLowerHex64(sha.toString())
            || !exactInteger(entry.value(key("bytes")), 1, kMaxShardBytes, shard.bytes)
            || !exactInteger(entry.value(key("rows")), 1, kMaxShardRows, shard.rows))
            return failWith(ManifestError::BadFeatures);
        shard.path = path.toString();
        shard.sha256 = sha.toString();
        m.shards.push_back(std::move(shard));
    }

    const QJsonValue noticesValue = root.value(key("notices"));
    if (!noticesValue.isArray())
        return failWith(ManifestError::BadNotices);
    const QJsonArray notices = noticesValue.toArray();
    if (notices.isEmpty() || notices.size() > kMaxManifestNotices)
        return failWith(ManifestError::BadNotices);
    QSet<QString> seen;
    for (const QJsonValue &value : notices) {
        if (!value.isObject())
            return failWith(ManifestError::BadNotices);
        const QJsonObject o = value.toObject();
        const QJsonValue notice = o.value(key("notice"));
        const QJsonValue noticeSha = o.value(key("notice_sha256"));
        if (!notice.isString() || !experiment::isSafeNoticePath(notice.toString()) || !noticeSha.isString()
            || !context::isLowerHex64(noticeSha.toString()) || seen.contains(notice.toString()))
            return failWith(ManifestError::BadNotices);
        seen.insert(notice.toString());
        m.notices.push_back({notice.toString(), noticeSha.toString()});
    }
    return result;
}

// ---- Решения ----

bool isRouterTarget(QStringView target, SourceLayout layout)
{
    const CanonicalToken token = canonicalizeToken(target, layout);
    if (token.status != CanonicalStatus::Ok)
        return false;
    const qsizetype keys = token.keys.size();
    return keys >= kMinRouterKeys && keys <= kMaxTokenKeys;
}

bool voteOpposesLayout(ContextVote vote, SourceLayout layout)
{
    return (vote == ContextVote::Ru && layout == SourceLayout::Us)
        || (vote == ContextVote::En && layout == SourceLayout::Ru);
}

std::optional<PolicyAction> commonRouterGate(const FeatureRow &row)
{
    if (row.field == FieldKind::Protected)
        return PolicyAction::Keep;
    if (row.field != FieldKind::Prose)
        return PolicyAction::Abstain;
    if (!isRouterTarget(row.target, row.layout))
        return PolicyAction::Abstain;
    if (row.fullObserved)
        return PolicyAction::Keep;
    if (row.sharedAmbiguous)
        return PolicyAction::Abstain;
    return std::nullopt;
}

PolicyAction decideSafeFull(const FeatureRow &row)
{
    if (const std::optional<PolicyAction> gated = commonRouterGate(row))
        return *gated;
    return row.fullAlternative ? PolicyAction::Correct : PolicyAction::Abstain;
}

PolicyAction decideSafeConfirmed(const FeatureRow &row)
{
    if (const std::optional<PolicyAction> gated = commonRouterGate(row))
        return *gated;
    return row.fullAlternative && modelCorrect(row) ? PolicyAction::Correct : PolicyAction::Abstain;
}

PolicyAction decideContextConfirmed(const FeatureRow &row)
{
    if (const std::optional<PolicyAction> gated = commonRouterGate(row))
        return *gated;
    return row.fullAlternative && modelCorrect(row) && contextOpposes(row) ? PolicyAction::Correct
                                                                          : PolicyAction::Abstain;
}

PolicyAction decideHybrid(const FeatureRow &row)
{
    if (const std::optional<PolicyAction> gated = commonRouterGate(row))
        return *gated;
    if (row.fullAlternative)
        return modelCorrect(row) ? PolicyAction::Correct : PolicyAction::Suggest;
    return modelCorrect(row) ? PolicyAction::Suggest : PolicyAction::Abstain;
}

PolicyAction decideContextHybrid(const FeatureRow &row)
{
    if (const std::optional<PolicyAction> gated = commonRouterGate(row))
        return *gated;
    // Словарь за замену: как hybrid (Correct при согласии модели, иначе Suggest).
    if (row.fullAlternative)
        return modelCorrect(row) ? PolicyAction::Correct : PolicyAction::Suggest;
    // Только модель: предложение лишь при противоположном голосе контекста.
    return modelCorrect(row) && contextOpposes(row) ? PolicyAction::Suggest : PolicyAction::Abstain;
}

PolicyAction decideProposalsOnly(const FeatureRow &row)
{
    const PolicyAction action = decideContextHybrid(row);
    return action == PolicyAction::Correct ? PolicyAction::Suggest : action;
}

PolicyAction decideScriptContext(const FeatureRow &row)
{
    return context::decideContextAuto(row);
}

PolicyAction decide(RouterPolicy policy, const FeatureRow &row)
{
    switch (policy) {
    case RouterPolicy::AlwaysKeep:
        return PolicyAction::Keep;
    case RouterPolicy::SafeFull:
        return decideSafeFull(row);
    case RouterPolicy::SafeConfirmed:
        return decideSafeConfirmed(row);
    case RouterPolicy::ContextConfirmed:
        return decideContextConfirmed(row);
    case RouterPolicy::Hybrid:
        return decideHybrid(row);
    case RouterPolicy::ContextHybrid:
        return decideContextHybrid(row);
    case RouterPolicy::ProposalsOnly:
        return decideProposalsOnly(row);
    case RouterPolicy::ScriptContext:
        return decideScriptContext(row);
    }
    return PolicyAction::Abstain;
}

RouterRow evaluateRow(const FeatureRow &row)
{
    RouterRow out;
    out.vote = context::contextVote(row);
    for (std::size_t i = 0; i < kRouterPolicies.size(); ++i)
        out.actions[i] = decide(kRouterPolicies[i], row);
    return out;
}

void appendOutputRow(QByteArray &out, qint64 ordinal, const RouterRow &row)
{
    out.append(QByteArray::number(ordinal));
    out.append('\t');
    out.append(context::contextVoteCode(row.vote));
    out.append('\t');
    for (const PolicyAction action : row.actions)
        out.append(context::policyActionCode(action));
    out.append('\n');
}

QByteArray renderOutput(const std::vector<FeatureRow> &rows)
{
    if (qint64(rows.size()) > kMaxShardRows)
        return {};
    QByteArray out;
    // ordinal <= 5 цифр, '\t', голос, '\t', 8 кодов, '\n'.
    out.reserve(qsizetype(rows.size()) * (5 + 1 + 1 + 1 + kRouterPolicyCount + 1));
    for (std::size_t i = 0; i < rows.size(); ++i) {
        appendOutputRow(out, qint64(i), router::evaluateRow(rows[i]));
        if (qint64(out.size()) >= kMaxOutputBytes)
            return {};
    }
    return out;
}

} // namespace rasklad::router
