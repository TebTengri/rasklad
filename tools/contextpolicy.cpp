#include "contextpolicy.h"

#include "detectormanifest.h"

#include <QChar>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QSet>
#include <QStringDecoder>

#include <cmath>
#include <cstring>

namespace rasklad::context {

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

bool stringEquals(const QJsonObject &object, const char *name, const char *expected)
{
    const QJsonValue value = object.value(key(name));
    return value.isString() && value.toString() == QLatin1String(expected);
}

// Массив строк, точно равный списку (порядок и длина).
template <std::size_t N>
bool stringArrayEquals(const QJsonValue &value, const std::array<const char *, N> &expected)
{
    if (!value.isArray())
        return false;
    const QJsonArray array = value.toArray();
    if (array.size() != qsizetype(N))
        return false;
    for (std::size_t i = 0; i < N; ++i) {
        const QJsonValue v = array.at(qsizetype(i));
        if (!v.isString() || v.toString() != QLatin1String(expected[i]))
            return false;
    }
    return true;
}

bool integerEquals(const QJsonObject &object, const char *name, qint64 expected)
{
    qint64 value = 0;
    return exactInteger(object.value(key(name)), expected, expected, value);
}

constexpr std::array<const char *, kFeatureColumns> kFeatureFieldNames{{
    "layout",
    "target",
    "left1",
    "left2",
    "left3",
    "left4",
    "field",
    "fullObserved",
    "fullAlternative",
    "sharedAmbiguous",
    "tokenModelAction",
    "tokenFullAction",
}};

constexpr std::array<const char *, 3> kFieldValueNames{{"prose", "protected", "unknown"}};

bool isStrictUtf8(const QByteArray &bytes)
{
    QStringDecoder decoder(QStringDecoder::Utf8, QStringDecoder::Flag::Stateless);
    const QString decoded = decoder(bytes);
    static_cast<void>(decoded);
    return !decoder.hasError();
}

// Cc или пробельный символ Unicode (Zs/Zl/Zp и \t\n\v\f\r, U+0085, U+00A0).
bool isForbiddenCharacter(QChar ch)
{
    return ch.category() == QChar::Other_Control || ch.isSpace();
}

bool isCleanField(const QString &field)
{
    for (const QChar ch : field) {
        if (isForbiddenCharacter(ch))
            return false;
    }
    return true;
}

// Точное побайтовое равенство поля и ASCII-литерала.
bool fieldIs(QByteArrayView field, const char *literal)
{
    const qsizetype length = qsizetype(qstrlen(literal));
    return field.size() == length && std::memcmp(field.data(), literal, std::size_t(length)) == 0;
}

bool parseFlag(QByteArrayView field, bool &out)
{
    if (fieldIs(field, "0"))
        out = false;
    else if (fieldIs(field, "1"))
        out = true;
    else
        return false;
    return true;
}

bool parseInputAction(QByteArrayView field, PolicyAction &out)
{
    if (fieldIs(field, "K"))
        out = PolicyAction::Keep;
    else if (fieldIs(field, "C"))
        out = PolicyAction::Correct;
    else if (fieldIs(field, "A"))
        out = PolicyAction::Abstain;
    else
        return false;
    return true;
}

FeatureResult failFeatures(FeatureError error)
{
    FeatureResult result;
    result.error = error;
    return result;
}

bool isAsciiLetter(char16_t c)
{
    return (c >= u'a' && c <= u'z') || (c >= u'A' && c <= u'Z');
}

// 33 буквы русского алфавита: А..я (32 буквы в двух регистрах) и Ё/ё.
bool isRu33Letter(char16_t c)
{
    // U+0410..U+044F, U+0401 (Ё), U+0451 (ё); числами, чтобы не спутать с латиницей.
    return (c >= char16_t(0x0410) && c <= char16_t(0x044F)) || c == char16_t(0x0401) || c == char16_t(0x0451);
}

int canonicalKeys(QStringView target, SourceLayout layout)
{
    const CanonicalToken token = canonicalizeToken(target, layout);
    return token.status == CanonicalStatus::Ok ? int(token.keys.size()) : -1;
}

bool voteMatchesLayout(ContextVote vote, SourceLayout layout)
{
    return (vote == ContextVote::En && layout == SourceLayout::Us)
        || (vote == ContextVote::Ru && layout == SourceLayout::Ru);
}

// Общие ворота всех context-вариантов. true — решение принято воротами.
bool contextGate(const FeatureRow &row, PolicyAction &out)
{
    if (row.field == FieldKind::Protected) {
        out = PolicyAction::Keep;
        return true;
    }
    if (row.field != FieldKind::Prose) {
        out = PolicyAction::Abstain;
        return true;
    }
    if (!isContextTarget(row.target, row.layout) || contextVote(row) == ContextVote::None) {
        out = PolicyAction::Abstain;
        return true;
    }
    return false;
}

} // namespace

bool isLowerHex64(const QString &value)
{
    if (value.size() != 64)
        return false;
    for (const QChar ch : value) {
        const char16_t c = ch.unicode();
        if (!((c >= u'0' && c <= u'9') || (c >= u'a' && c <= u'f')))
            return false;
    }
    return true;
}

const char *contextMethodName(ContextMethod method)
{
    switch (method) {
    case ContextMethod::AlwaysKeep:
        return "alwaysKeep";
    case ContextMethod::TokenTwoScore:
        return "tokenTwoScore";
    case ContextMethod::TokenHunspell:
        return "tokenHunspell";
    case ContextMethod::ContextAuto:
        return "contextAuto";
    case ContextMethod::GuardedContextAuto:
        return "guardedContextAuto";
    case ContextMethod::ContextSuggest:
        return "contextSuggest";
    }
    return "unknown";
}

char policyActionCode(PolicyAction action)
{
    switch (action) {
    case PolicyAction::Keep:
        return 'K';
    case PolicyAction::Correct:
        return 'C';
    case PolicyAction::Abstain:
        return 'A';
    case PolicyAction::Suggest:
        return 'S';
    }
    return 'A';
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
    case ArgumentsError::Missing:
        return QStringLiteral("--protocol and --manifest are required");
    }
    return QStringLiteral("unknown arguments error");
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
    for (qsizetype i = 1; i < args.size(); ++i) {
        QString *target = nullptr;
        bool *seen = nullptr;
        if (args.at(i) == QLatin1String("--protocol")) {
            target = &result.arguments.protocol;
            seen = &seenProtocol;
        } else if (args.at(i) == QLatin1String("--manifest")) {
            target = &result.arguments.manifest;
            seen = &seenManifest;
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
    }
    if (!seenProtocol || !seenManifest)
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
        return QStringLiteral("protocol SHA-256 differs from the frozen experiment-005 protocol");
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
    std::array<const char *, kContextMethodCount> methodNames{};
    for (std::size_t i = 0; i < kContextMethods.size(); ++i)
        methodNames[i] = contextMethodName(kContextMethods[i]);
    if (!stringArrayEquals(root.value(key("policies")), methodNames))
        return ProtocolError::FieldMismatch;
    const QJsonValue observablesValue = root.value(key("observables"));
    if (!observablesValue.isObject())
        return ProtocolError::FieldMismatch;
    const QJsonObject observables = observablesValue.toObject();
    if (!stringArrayEquals(observables.value(key("feature_fields")), kFeatureFieldNames)
        || !stringArrayEquals(observables.value(key("field_values")), kFieldValueNames))
        return ProtocolError::FieldMismatch;
    const QJsonValue limitsValue = root.value(key("limits"));
    if (!limitsValue.isObject())
        return ProtocolError::FieldMismatch;
    const QJsonObject limits = limitsValue.toObject();
    if (limits.size() != 4 || !integerEquals(limits, "feature_bytes", kMaxFeatureBytes)
        || !integerEquals(limits, "feature_rows", kMaxFeatureRows)
        || !integerEquals(limits, "field_utf16", kMaxFieldUtf16)
        || !integerEquals(limits, "protocol_bytes", kMaxProtocolBytes))
        return ProtocolError::FieldMismatch;
    return ProtocolError::None;
}

ProtocolError validateProtocol005(const QByteArray &bytes)
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
        return QStringLiteral("evaluation manifest status is not approved-local-synthetic-evaluation");
    case ManifestError::ScopeMismatch:
        return QStringLiteral("evaluation manifest scope is not local-evaluation-only");
    case ManifestError::ReleaseApproved:
        return QStringLiteral("evaluation manifest release_approved must be false");
    case ManifestError::ProtocolMismatch:
        return QStringLiteral("evaluation manifest protocol_sha256 is not the frozen experiment-005 protocol");
    case ManifestError::BadFeatures:
        return QStringLiteral("evaluation manifest has an invalid features entry");
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
    if (!featuresValue.isObject())
        return failWith(ManifestError::BadFeatures);
    const QJsonObject features = featuresValue.toObject();
    const QJsonValue path = features.value(key("path"));
    const QJsonValue sha = features.value(key("sha256"));
    if (!path.isString() || path.toString() != QLatin1String(kFeaturesPath) || !sha.isString()
        || !isLowerHex64(sha.toString())
        || !exactInteger(features.value(key("bytes")), 1, kMaxFeatureBytes, m.featuresBytes)
        || !exactInteger(features.value(key("rows")), 1, kMaxFeatureRows, m.featuresRows))
        return failWith(ManifestError::BadFeatures);
    m.featuresPath = path.toString();
    m.featuresSha256 = sha.toString();

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
            || !isLowerHex64(noticeSha.toString()) || seen.contains(notice.toString()))
            return failWith(ManifestError::BadNotices);
        seen.insert(notice.toString());
        m.notices.push_back({notice.toString(), noticeSha.toString()});
    }
    return result;
}

// ---- TSV признаков ----

QString featureErrorText(FeatureError error)
{
    switch (error) {
    case FeatureError::None:
        return QStringLiteral("ok");
    case FeatureError::TooLarge:
        return QStringLiteral("features file exceeds size limit");
    case FeatureError::Empty:
        return QStringLiteral("features file is empty");
    case FeatureError::MissingFinalNewline:
        return QStringLiteral("features file is not LF-terminated");
    case FeatureError::TooManyRows:
        return QStringLiteral("features file has too many rows");
    case FeatureError::RowCountMismatch:
        return QStringLiteral("features row count differs from the manifest");
    case FeatureError::CarriageReturn:
        return QStringLiteral("features file contains a carriage return");
    case FeatureError::EmptyLine:
        return QStringLiteral("features file contains an empty line");
    case FeatureError::LineTooLong:
        return QStringLiteral("features file contains a line exceeding the byte limit");
    case FeatureError::InvalidUtf8:
        return QStringLiteral("features file is not strict UTF-8");
    case FeatureError::BadColumns:
        return QStringLiteral("features row does not have exactly twelve fields");
    case FeatureError::BadLayout:
        return QStringLiteral("features row has an invalid layout");
    case FeatureError::EmptyTarget:
        return QStringLiteral("features row has an empty target");
    case FeatureError::FieldTooLong:
        return QStringLiteral("features row has a string field exceeding the UTF-16 limit");
    case FeatureError::BadCharacter:
        return QStringLiteral("features row has a control or whitespace character in a string field");
    case FeatureError::BadField:
        return QStringLiteral("features row has an invalid field kind");
    case FeatureError::BadFlag:
        return QStringLiteral("features row has an invalid membership flag");
    case FeatureError::BadAction:
        return QStringLiteral("features row has an invalid token action");
    case FeatureError::InconsistentAmbiguity:
        return QStringLiteral("features row has both full memberships without shared ambiguity");
    case FeatureError::InconsistentFullAction:
        return QStringLiteral("features row token full action differs from its memberships and guard");
    case FeatureError::InconsistentModelAction:
        return QStringLiteral("features row token model action is not Abstain outside the guard");
    }
    return QStringLiteral("unknown features error");
}

bool isTokenGuardTarget(QStringView target, SourceLayout layout)
{
    const int keys = canonicalKeys(target, layout);
    return keys >= kMinDecisionKeys && keys <= kMaxTokenKeys;
}

bool isContextTarget(QStringView target, SourceLayout layout)
{
    const int keys = canonicalKeys(target, layout);
    return keys >= kMinContextTargetKeys && keys <= kMaxTokenKeys;
}

PolicyAction expectedTokenFullAction(QStringView target, SourceLayout layout, bool fullObserved,
                                     bool fullAlternative)
{
    if (!isTokenGuardTarget(target, layout))
        return PolicyAction::Abstain;
    if (fullObserved)
        return PolicyAction::Keep;
    if (fullAlternative)
        return PolicyAction::Correct;
    return PolicyAction::Abstain;
}

FeatureResult parseFeatures(const QByteArray &data, qint64 expectedRows)
{
    // ---- Сырые байты: размер, строки, пределы до декодирования ----
    if (qint64(data.size()) > kMaxFeatureBytes)
        return failFeatures(FeatureError::TooLarge);
    if (data.isEmpty())
        return failFeatures(FeatureError::Empty);
    if (data.back() != '\n')
        return failFeatures(FeatureError::MissingFinalNewline);
    const qint64 rows = data.count('\n');
    if (rows > kMaxFeatureRows)
        return failFeatures(FeatureError::TooManyRows);
    if (rows != expectedRows)
        return failFeatures(FeatureError::RowCountMismatch);
    if (data.contains('\r'))
        return failFeatures(FeatureError::CarriageReturn);
    {
        qsizetype start = 0;
        while (start < data.size()) {
            const qsizetype end = data.indexOf('\n', start);
            const qsizetype length = end - start;
            if (length == 0)
                return failFeatures(FeatureError::EmptyLine);
            if (length > kMaxFeatureLineBytes)
                return failFeatures(FeatureError::LineTooLong);
            start = end + 1;
        }
    }
    if (!isStrictUtf8(data))
        return failFeatures(FeatureError::InvalidUtf8);

    // ---- Поля ----
    FeatureResult result;
    result.rows.reserve(std::size_t(rows));
    const QByteArrayView all(data);
    qsizetype start = 0;
    while (start < data.size()) {
        const qsizetype end = data.indexOf('\n', start);
        const QByteArrayView line = all.sliced(start, end - start);
        start = end + 1;

        std::array<QByteArrayView, kFeatureColumns> fields;
        qsizetype fieldStart = 0;
        int count = 0;
        for (qsizetype i = 0; i <= line.size(); ++i) {
            if (i < line.size() && line.at(i) != '\t')
                continue;
            if (count >= kFeatureColumns)
                return failFeatures(FeatureError::BadColumns);
            fields[std::size_t(count++)] = line.sliced(fieldStart, i - fieldStart);
            fieldStart = i + 1;
        }
        if (count != kFeatureColumns)
            return failFeatures(FeatureError::BadColumns);

        FeatureRow row;
        if (fieldIs(fields[0], "us"))
            row.layout = SourceLayout::Us;
        else if (fieldIs(fields[0], "ru"))
            row.layout = SourceLayout::Ru;
        else
            return failFeatures(FeatureError::BadLayout);
        if (fields[1].isEmpty())
            return failFeatures(FeatureError::EmptyTarget);
        row.target = QString::fromUtf8(fields[1]);
        for (std::size_t k = 0; k < row.left.size(); ++k)
            row.left[k] = QString::fromUtf8(fields[2 + k]);
        if (row.target.size() > kMaxFieldUtf16)
            return failFeatures(FeatureError::FieldTooLong);
        for (const QString &left : row.left) {
            if (left.size() > kMaxFieldUtf16)
                return failFeatures(FeatureError::FieldTooLong);
        }
        if (!isCleanField(row.target))
            return failFeatures(FeatureError::BadCharacter);
        for (const QString &left : row.left) {
            if (!isCleanField(left))
                return failFeatures(FeatureError::BadCharacter);
        }
        if (fieldIs(fields[6], "prose"))
            row.field = FieldKind::Prose;
        else if (fieldIs(fields[6], "protected"))
            row.field = FieldKind::Protected;
        else if (fieldIs(fields[6], "unknown"))
            row.field = FieldKind::Unknown;
        else
            return failFeatures(FeatureError::BadField);
        if (!parseFlag(fields[7], row.fullObserved) || !parseFlag(fields[8], row.fullAlternative)
            || !parseFlag(fields[9], row.sharedAmbiguous))
            return failFeatures(FeatureError::BadFlag);
        if (!parseInputAction(fields[10], row.tokenModelAction) || !parseInputAction(fields[11], row.tokenFullAction))
            return failFeatures(FeatureError::BadAction);

        if (row.fullObserved && row.fullAlternative && !row.sharedAmbiguous)
            return failFeatures(FeatureError::InconsistentAmbiguity);
        if (row.tokenFullAction
            != expectedTokenFullAction(row.target, row.layout, row.fullObserved, row.fullAlternative))
            return failFeatures(FeatureError::InconsistentFullAction);
        if (!isTokenGuardTarget(row.target, row.layout) && row.tokenModelAction != PolicyAction::Abstain)
            return failFeatures(FeatureError::InconsistentModelAction);
        result.rows.push_back(std::move(row));
    }
    if (qint64(result.rows.size()) != expectedRows)
        return failFeatures(FeatureError::RowCountMismatch);
    return result;
}

// ---- Контекст ----

char contextVoteCode(ContextVote vote)
{
    switch (vote) {
    case ContextVote::En:
        return 'U';
    case ContextVote::Ru:
        return 'R';
    case ContextVote::None:
        return '-';
    }
    return '-';
}

ContextVote anchorScript(QStringView word)
{
    if (word.size() < kMinAnchorLetters || word.size() > kMaxAnchorLetters)
        return ContextVote::None;
    bool en = true;
    bool ru = true;
    for (const QChar ch : word) {
        const char16_t c = ch.unicode();
        en = en && isAsciiLetter(c);
        ru = ru && isRu33Letter(c);
        if (!en && !ru)
            return ContextVote::None;
    }
    if (en)
        return ContextVote::En;
    if (ru)
        return ContextVote::Ru;
    return ContextVote::None;
}

ContextVote contextVote(const FeatureRow &row)
{
    // Только два непосредственно предшествующих слота; left1/left2 не читаются.
    const ContextVote third = anchorScript(row.left[2]);
    const ContextVote fourth = anchorScript(row.left[3]);
    if (third == ContextVote::None || third != fourth)
        return ContextVote::None;
    return third;
}

// ---- Решения ----

PolicyAction decideContextAuto(const FeatureRow &row)
{
    PolicyAction gated = PolicyAction::Abstain;
    if (contextGate(row, gated))
        return gated;
    if (row.fullObserved && !row.fullAlternative)
        return PolicyAction::Keep;
    if (row.fullAlternative) {
        const bool matches = voteMatchesLayout(contextVote(row), row.layout);
        if (matches && row.fullObserved)
            return PolicyAction::Keep;
        if (!matches)
            return PolicyAction::Correct;
        return PolicyAction::Abstain;
    }
    return PolicyAction::Abstain;
}

PolicyAction decideGuardedContextAuto(const FeatureRow &row)
{
    PolicyAction gated = PolicyAction::Abstain;
    if (contextGate(row, gated))
        return gated;
    if (row.sharedAmbiguous)
        return PolicyAction::Abstain;
    return decideContextAuto(row);
}

PolicyAction decideContextSuggest(const FeatureRow &row)
{
    const PolicyAction action = decideContextAuto(row);
    return action == PolicyAction::Correct ? PolicyAction::Suggest : action;
}

PolicyAction decide(ContextMethod method, const FeatureRow &row)
{
    switch (method) {
    case ContextMethod::AlwaysKeep:
        return PolicyAction::Keep;
    case ContextMethod::TokenTwoScore:
        return row.tokenModelAction;
    case ContextMethod::TokenHunspell:
        return row.tokenFullAction;
    case ContextMethod::ContextAuto:
        return decideContextAuto(row);
    case ContextMethod::GuardedContextAuto:
        return decideGuardedContextAuto(row);
    case ContextMethod::ContextSuggest:
        return decideContextSuggest(row);
    }
    return PolicyAction::Abstain;
}

ContextRow evaluateRow(const FeatureRow &row)
{
    ContextRow out;
    out.vote = contextVote(row);
    for (std::size_t i = 0; i < kContextMethods.size(); ++i)
        out.actions[i] = decide(kContextMethods[i], row);
    return out;
}

void appendOutputRow(QByteArray &out, qint64 ordinal, const ContextRow &row)
{
    out.append(QByteArray::number(ordinal));
    out.append('\t');
    out.append(contextVoteCode(row.vote));
    out.append('\t');
    for (const PolicyAction action : row.actions)
        out.append(policyActionCode(action));
    out.append('\n');
}

QByteArray renderOutput(const std::vector<FeatureRow> &rows)
{
    if (qint64(rows.size()) > kMaxFeatureRows)
        return {};
    QByteArray out;
    // ordinal <= 6 цифр, '\t', голос, '\t', 6 кодов, '\n'.
    out.reserve(qsizetype(rows.size()) * (6 + 1 + 1 + 1 + kContextMethodCount + 1));
    for (std::size_t i = 0; i < rows.size(); ++i) {
        appendOutputRow(out, qint64(i), evaluateRow(rows[i]));
        if (qint64(out.size()) >= kMaxOutputBytes)
            return {};
    }
    return out;
}

} // namespace rasklad::context
