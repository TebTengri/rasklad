#include "domaindecisions.h"

#include "detectormanifest.h"
#include "evaluationdata.h"
#include "layoutconverter.h"
#include "morphologydata.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QSet>

#include <cmath>
#include <cstring>

namespace rasklad::domain {

using benchmark::correctionDirection;
using benchmark::otherLayout;
using morphology::MorphFeatures;
using morphology::MorphMethod;

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

bool pinnedObjectMatches(const QJsonValue &value, const PinnedFile &pin, bool withFileName)
{
    if (!value.isObject())
        return false;
    const QJsonObject o = value.toObject();
    qint64 bytes = 0;
    if (!exactInteger(o.value(key("bytes")), 1, kMaxCasesBytes, bytes) || bytes != pin.bytes)
        return false;
    if (!stringEquals(o, "sha256", pin.sha256))
        return false;
    if (withFileName) {
        if (!stringEquals(o, "file", pin.fileName))
            return false;
        if (QString::fromLatin1(kArtifactDir) + QString::fromLatin1(pin.fileName) != QLatin1String(pin.path))
            return false;
    } else if (!stringEquals(o, "path", pin.path)) {
        return false;
    }
    return true;
}

// Символ, недопустимый внутри text/expected: Cc или пробельный символ Unicode
// (токены по протоколу отделены пробельными символами).
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

bool parseDocument(QByteArrayView field, int &out)
{
    if (field.isEmpty() || field.size() > 2)
        return false;
    if (field.size() == 2 && field.at(0) == '0')
        return false;
    int value = 0;
    for (const char c : field) {
        if (c < '0' || c > '9')
            return false;
        value = value * 10 + (c - '0');
    }
    if (value > kMaxDocumentIndex)
        return false;
    out = value;
    return true;
}

CasesTsvResult failTsv(CasesTsvError error)
{
    CasesTsvResult result;
    result.error = error;
    return result;
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

// ---- Протокол ----

QString protocolErrorText(ProtocolError error)
{
    switch (error) {
    case ProtocolError::None:
        return QStringLiteral("ok");
    case ProtocolError::TooLarge:
        return QStringLiteral("protocol exceeds size limit");
    case ProtocolError::HashMismatch:
        return QStringLiteral("protocol SHA-256 differs from the frozen experiment-004 protocol");
    case ProtocolError::InvalidJson:
        return QStringLiteral("protocol is not a JSON object");
    case ProtocolError::FieldMismatch:
        return QStringLiteral("protocol fields differ from the constants of this tool");
    }
    return QStringLiteral("unknown protocol error");
}

const char *domainMethodName(DomainMethod method)
{
    switch (method) {
    case DomainMethod::AlwaysKeep:
        return "alwaysKeep";
    case DomainMethod::TwoScore:
        return "twoScore";
    case DomainMethod::HunspellFull:
        return "hunspellFull";
    case DomainMethod::ModelConfirmedMorph:
        return "modelConfirmedMorph";
    case DomainMethod::MorphThenModel:
        return "morphThenModel";
    case DomainMethod::ProtectedTwoScore:
        return "protectedTwoScore";
    case DomainMethod::ProtectedHunspellFull:
        return "protectedHunspellFull";
    case DomainMethod::ProtectedConfirmedMorph:
        return "protectedConfirmedMorph";
    case DomainMethod::ProtectedMorphThenModel:
        return "protectedMorphThenModel";
    }
    return "unknown";
}

ProtocolError validateProtocol004(const QByteArray &bytes)
{
    if (qint64(bytes.size()) > kMaxProtocolBytes)
        return ProtocolError::TooLarge;
    if (experiment::sha256Hex(bytes) != QLatin1String(kProtocolSha256))
        return ProtocolError::HashMismatch;
    QJsonObject root;
    if (!parseObject(bytes, root))
        return ProtocolError::InvalidJson;
    qint64 schema = 0;
    if (!exactInteger(root.value(key("schema_version")), 1, 1, schema))
        return ProtocolError::FieldMismatch;
    if (!pinnedObjectMatches(root.value(key("model")), kModelFile, false)
        || !pinnedObjectMatches(root.value(key("stem")), kStemFile, false))
        return ProtocolError::FieldMismatch;
    if (!stringEquals(root, "morphology_manifest", kMorphManifestPath))
        return ProtocolError::FieldMismatch;
    const QJsonValue methods = root.value(key("methods"));
    if (!methods.isArray() || methods.toArray().size() != kDomainMethodCount)
        return ProtocolError::FieldMismatch;
    const QJsonArray array = methods.toArray();
    for (int i = 0; i < kDomainMethodCount; ++i) {
        const QJsonValue v = array.at(i);
        if (!v.isString() || v.toString() != QLatin1String(domainMethodName(kDomainMethods[std::size_t(i)])))
            return ProtocolError::FieldMismatch;
    }
    return ProtocolError::None;
}

// ---- Паспорт случаев ----

QString casesManifestErrorText(CasesManifestError error)
{
    switch (error) {
    case CasesManifestError::None:
        return QStringLiteral("ok");
    case CasesManifestError::TooLarge:
        return QStringLiteral("cases manifest exceeds size limit");
    case CasesManifestError::InvalidJson:
        return QStringLiteral("cases manifest is not a JSON object");
    case CasesManifestError::SchemaVersion:
        return QStringLiteral("cases manifest schema_version must be 1");
    case CasesManifestError::StatusNotApproved:
        return QStringLiteral("cases manifest status is not approved-local-evaluation");
    case CasesManifestError::ScopeMismatch:
        return QStringLiteral("cases manifest scope is not local-evaluation-only");
    case CasesManifestError::ReleaseApproved:
        return QStringLiteral("cases manifest release_approved must be false");
    case CasesManifestError::BadCases:
        return QStringLiteral("cases manifest has an invalid cases entry");
    case CasesManifestError::BadNotices:
        return QStringLiteral("cases manifest has an invalid notices list");
    }
    return QStringLiteral("unknown cases manifest error");
}

CasesManifestResult parseCasesManifest(const QByteArray &bytes)
{
    CasesManifestResult result;
    auto failWith = [&result](CasesManifestError error) {
        result.error = error;
        result.manifest = CasesManifest();
        return result;
    };
    if (qint64(bytes.size()) > kMaxCasesManifestBytes)
        return failWith(CasesManifestError::TooLarge);
    QJsonObject root;
    if (!parseObject(bytes, root))
        return failWith(CasesManifestError::InvalidJson);
    qint64 schema = 0;
    if (!exactInteger(root.value(key("schema_version")), 1, 1, schema))
        return failWith(CasesManifestError::SchemaVersion);
    if (!stringEquals(root, "status", kCasesRequiredStatus))
        return failWith(CasesManifestError::StatusNotApproved);
    if (!stringEquals(root, "scope", kCasesRequiredScope))
        return failWith(CasesManifestError::ScopeMismatch);
    const QJsonValue release = root.value(key("release_approved"));
    if (!release.isBool() || release.toBool())
        return failWith(CasesManifestError::ReleaseApproved);

    CasesManifest &m = result.manifest;
    const QJsonValue casesValue = root.value(key("cases"));
    if (!casesValue.isObject())
        return failWith(CasesManifestError::BadCases);
    const QJsonObject cases = casesValue.toObject();
    const QJsonValue path = cases.value(key("path"));
    const QJsonValue sha = cases.value(key("sha256"));
    if (!path.isString() || path.toString() != QLatin1String(kCasesPath)
        || !morphology::isSafeProjectPath(path.toString(), kCasesPrefix) || !sha.isString()
        || !isLowerHex64(sha.toString())
        || !exactInteger(cases.value(key("bytes")), 1, kMaxCasesBytes, m.casesBytes)
        || !exactInteger(cases.value(key("rows")), 1, kMaxCasesRows, m.casesRows))
        return failWith(CasesManifestError::BadCases);
    m.casesPath = path.toString();
    m.casesSha256 = sha.toString();

    const QJsonValue noticesValue = root.value(key("notices"));
    if (!noticesValue.isArray())
        return failWith(CasesManifestError::BadNotices);
    const QJsonArray notices = noticesValue.toArray();
    if (notices.isEmpty() || notices.size() > kMaxCaseNotices)
        return failWith(CasesManifestError::BadNotices);
    QSet<QString> seen;
    for (const QJsonValue &value : notices) {
        if (!value.isObject())
            return failWith(CasesManifestError::BadNotices);
        const QJsonObject o = value.toObject();
        const QJsonValue notice = o.value(key("notice"));
        const QJsonValue noticeSha = o.value(key("notice_sha256"));
        if (!notice.isString() || !experiment::isSafeNoticePath(notice.toString()) || !noticeSha.isString()
            || !isLowerHex64(noticeSha.toString()) || seen.contains(notice.toString()))
            return failWith(CasesManifestError::BadNotices);
        seen.insert(notice.toString());
        m.notices.push_back({notice.toString(), noticeSha.toString()});
    }
    m.manifestSha256 = experiment::sha256Hex(bytes);
    return result;
}

// ---- TSV случаев ----

QString casesTsvErrorText(CasesTsvError error)
{
    switch (error) {
    case CasesTsvError::None:
        return QStringLiteral("ok");
    case CasesTsvError::TooLarge:
        return QStringLiteral("cases file exceeds size limit");
    case CasesTsvError::Empty:
        return QStringLiteral("cases file is empty");
    case CasesTsvError::MissingFinalNewline:
        return QStringLiteral("cases file is not LF-terminated");
    case CasesTsvError::TooManyRows:
        return QStringLiteral("cases file has too many rows");
    case CasesTsvError::RowCountMismatch:
        return QStringLiteral("cases row count differs from the manifest");
    case CasesTsvError::EmptyLine:
        return QStringLiteral("cases file contains an empty line");
    case CasesTsvError::LineTooLong:
        return QStringLiteral("cases file contains a line exceeding the byte limit");
    case CasesTsvError::CarriageReturn:
        return QStringLiteral("cases file contains a carriage return");
    case CasesTsvError::InvalidUtf8:
        return QStringLiteral("cases file is not strict UTF-8");
    case CasesTsvError::BadColumns:
        return QStringLiteral("cases row does not have exactly six fields");
    case CasesTsvError::BadDocument:
        return QStringLiteral("cases row has an invalid document index");
    case CasesTsvError::BadLanguage:
        return QStringLiteral("cases row has an invalid document language");
    case CasesTsvError::BadRole:
        return QStringLiteral("cases row has an invalid role");
    case CasesTsvError::BadLayout:
        return QStringLiteral("cases row has an invalid typed layout");
    case CasesTsvError::EmptyField:
        return QStringLiteral("cases row has an empty text or expected field");
    case CasesTsvError::FieldTooLong:
        return QStringLiteral("cases row has a text or expected field exceeding the UTF-16 limit");
    case CasesTsvError::BadCharacter:
        return QStringLiteral("cases row has a control or whitespace character in text or expected");
    case CasesTsvError::ExpectedDiffers:
        return QStringLiteral("valid or protected row has expected different from text");
    case CasesTsvError::BadWrongExpected:
        return QStringLiteral("wrong row expected is not letters of the language opposite to the typed layout");
    case CasesTsvError::WrongConversion:
        return QStringLiteral("wrong row text does not convert to expected");
    case CasesTsvError::WrongNotGenerated:
        return QStringLiteral("wrong row text is not the typed-layout copy of expected");
    }
    return QStringLiteral("unknown cases error");
}

CasesTsvResult parseCasesTsv(const QByteArray &data, qint64 expectedRows)
{
    // ---- Сырые байты: размер, строки, пределы до декодирования ----
    if (qint64(data.size()) > kMaxCasesBytes)
        return failTsv(CasesTsvError::TooLarge);
    if (data.isEmpty())
        return failTsv(CasesTsvError::Empty);
    if (data.back() != '\n')
        return failTsv(CasesTsvError::MissingFinalNewline);
    const qint64 rows = data.count('\n');
    if (rows > kMaxCasesRows)
        return failTsv(CasesTsvError::TooManyRows);
    if (rows != expectedRows)
        return failTsv(CasesTsvError::RowCountMismatch);
    if (data.contains('\r'))
        return failTsv(CasesTsvError::CarriageReturn);
    {
        qsizetype start = 0;
        while (start < data.size()) {
            const qsizetype end = data.indexOf('\n', start);
            const qsizetype length = end - start;
            if (length == 0)
                return failTsv(CasesTsvError::EmptyLine);
            if (length > kMaxCaseLineBytes)
                return failTsv(CasesTsvError::LineTooLong);
            start = end + 1;
        }
    }
    if (!morphology::isStrictUtf8(data))
        return failTsv(CasesTsvError::InvalidUtf8);

    // ---- Поля ----
    CasesTsvResult result;
    result.cases.reserve(std::size_t(rows));
    const QByteArrayView all(data);
    qsizetype start = 0;
    while (start < data.size()) {
        const qsizetype end = data.indexOf('\n', start);
        const QByteArrayView line = all.sliced(start, end - start);
        start = end + 1;

        std::array<QByteArrayView, 6> fields;
        qsizetype fieldStart = 0;
        int count = 0;
        for (qsizetype i = 0; i <= line.size(); ++i) {
            if (i < line.size() && line.at(i) != '\t')
                continue;
            if (count >= 6)
                return failTsv(CasesTsvError::BadColumns);
            fields[std::size_t(count++)] = line.sliced(fieldStart, i - fieldStart);
            fieldStart = i + 1;
        }
        if (count != 6)
            return failTsv(CasesTsvError::BadColumns);

        DomainCase c;
        if (!parseDocument(fields[0], c.document))
            return failTsv(CasesTsvError::BadDocument);
        if (fieldIs(fields[1], "en"))
            c.documentLanguage = DetectorLanguage::En;
        else if (fieldIs(fields[1], "ru"))
            c.documentLanguage = DetectorLanguage::Ru;
        else
            return failTsv(CasesTsvError::BadLanguage);
        if (fieldIs(fields[2], "valid"))
            c.role = CaseRole::Valid;
        else if (fieldIs(fields[2], "protected"))
            c.role = CaseRole::Protected;
        else if (fieldIs(fields[2], "wrong"))
            c.role = CaseRole::Wrong;
        else
            return failTsv(CasesTsvError::BadRole);
        if (fieldIs(fields[3], "us"))
            c.layout = SourceLayout::Us;
        else if (fieldIs(fields[3], "ru"))
            c.layout = SourceLayout::Ru;
        else
            return failTsv(CasesTsvError::BadLayout);
        if (fields[4].isEmpty() || fields[5].isEmpty())
            return failTsv(CasesTsvError::EmptyField);
        c.text = QString::fromUtf8(fields[4]);
        c.expected = QString::fromUtf8(fields[5]);
        if (c.text.size() > kMaxCaseFieldUtf16 || c.expected.size() > kMaxCaseFieldUtf16)
            return failTsv(CasesTsvError::FieldTooLong);
        if (!isCleanField(c.text) || !isCleanField(c.expected))
            return failTsv(CasesTsvError::BadCharacter);

        if (c.role != CaseRole::Wrong) {
            if (c.expected != c.text)
                return failTsv(CasesTsvError::ExpectedDiffers);
        } else {
            if (!benchmark::isNativeLetterOnly(c.expected, alternativeLanguage(c.layout)))
                return failTsv(CasesTsvError::BadWrongExpected);
            if (convertLayout(c.text, correctionDirection(c.layout)) != c.expected)
                return failTsv(CasesTsvError::WrongConversion);
            if (convertLayout(c.expected, correctionDirection(otherLayout(c.layout))) != c.text)
                return failTsv(CasesTsvError::WrongNotGenerated);
        }
        result.cases.push_back(std::move(c));
    }
    if (qint64(result.cases.size()) != expectedRows)
        return failTsv(CasesTsvError::RowCountMismatch);
    return result;
}

// ---- Решения ----

std::array<DecisionAction, kDomainMethodCount> decideDomain(const MorphFeatures &features, CaseRole role)
{
    static constexpr std::array<MorphMethod, 5> kBase{{
        MorphMethod::AlwaysKeep,
        MorphMethod::TwoScore,
        MorphMethod::HunspellFull,
        MorphMethod::ModelConfirmedMorph,
        MorphMethod::MorphThenModel,
    }};
    std::array<DecisionAction, kDomainMethodCount> actions{};
    for (std::size_t i = 0; i < kBase.size(); ++i)
        actions[i] = morphology::decideMorph(kBase[i], features);
    // protectedX (индекс 5 + k) соответствует базовому методу 1 + k.
    for (std::size_t k = 0; k < 4; ++k)
        actions[5 + k] = role == CaseRole::Protected ? DecisionAction::Keep : actions[1 + k];
    return actions;
}

DomainRow evaluateCase(const DomainCase &c, const benchmark::FeatureContext &context,
                       const morphology::SpellPair &control, const morphology::SpellPair &full)
{
    MorphFeatures f;
    f.base = benchmark::computeFeatures(canonicalizeToken(c.text, c.layout), c.layout, context);
    f.control = morphology::queryPair(control, c.text, c.layout, f.base);
    f.full = morphology::queryPair(full, c.text, c.layout, f.base);
    DomainRow row;
    row.eligible = benchmark::isDecisionEligible(f.base);
    row.ambiguous = morphology::sharedAmbiguous(f);
    row.roundTrip = c.role != CaseRole::Wrong || convertLayout(c.text, correctionDirection(c.layout)) == c.expected;
    row.actions = decideDomain(f, c.role);
    return row;
}

char actionCode(DecisionAction action)
{
    switch (action) {
    case DecisionAction::Keep:
        return 'K';
    case DecisionAction::Correct:
        return 'C';
    case DecisionAction::Abstain:
        return 'A';
    }
    return 'A';
}

void appendOutputRow(QByteArray &out, qint64 ordinal, const DomainRow &row)
{
    out.append(QByteArray::number(ordinal));
    out.append('\t');
    out.append(row.eligible ? '1' : '0');
    out.append('\t');
    out.append(row.ambiguous ? '1' : '0');
    out.append('\t');
    out.append(row.roundTrip ? '1' : '0');
    out.append('\t');
    for (const DecisionAction action : row.actions)
        out.append(actionCode(action));
    out.append('\n');
}

// ---- Метаданные артефактов ----

QString artifactMetadataErrorText(ArtifactMetadataError error)
{
    switch (error) {
    case ArtifactMetadataError::None:
        return QStringLiteral("ok");
    case ArtifactMetadataError::TooLarge:
        return QStringLiteral("artifact metadata exceeds size limit");
    case ArtifactMetadataError::InvalidJson:
        return QStringLiteral("artifact metadata is not a JSON object");
    case ArtifactMetadataError::ArtifactMismatch:
        return QStringLiteral("artifact metadata does not list the pinned model and stem files");
    case ArtifactMetadataError::ProtocolMismatch:
        return QStringLiteral("artifact metadata protocol is not the frozen experiment-002 protocol");
    case ArtifactMetadataError::TrainingManifestMismatch:
        return QStringLiteral("artifact metadata training manifest is not the approved experiment-001 manifest");
    case ArtifactMetadataError::TrainingSourceMismatch:
        return QStringLiteral("artifact metadata training sources or notices differ from the approved manifest");
    case ArtifactMetadataError::DistributionScope:
        return QStringLiteral("artifact metadata scope does not forbid distribution");
    }
    return QStringLiteral("unknown artifact metadata error");
}

ArtifactMetadataError verifyArtifactMetadata(const QByteArray &bytes,
                                             const std::array<ExpectedTrainingSource, 2> &sources)
{
    if (qint64(bytes.size()) > kMaxArtifactMetadataBytes)
        return ArtifactMetadataError::TooLarge;
    QJsonObject root;
    if (!parseObject(bytes, root))
        return ArtifactMetadataError::InvalidJson;
    const QJsonValue artifactsValue = root.value(key("artifacts"));
    if (!artifactsValue.isObject())
        return ArtifactMetadataError::ArtifactMismatch;
    const QJsonObject artifacts = artifactsValue.toObject();
    if (!pinnedObjectMatches(artifacts.value(key("model")), kModelFile, true)
        || !pinnedObjectMatches(artifacts.value(key("exactFullLexicons")), kStemFile, true))
        return ArtifactMetadataError::ArtifactMismatch;
    if (!stringEquals(root, "protocolSha256", kProtocol002Sha256))
        return ArtifactMetadataError::ProtocolMismatch;
    if (!stringEquals(root, "trainingManifestSha256", kTrainingManifestSha256))
        return ArtifactMetadataError::TrainingManifestMismatch;
    const QJsonValue trainingValue = root.value(key("trainingSources"));
    if (!trainingValue.isObject())
        return ArtifactMetadataError::TrainingSourceMismatch;
    const QJsonObject training = trainingValue.toObject();
    for (const DetectorLanguage language : {DetectorLanguage::En, DetectorLanguage::Ru}) {
        const ExpectedTrainingSource &expected = sources[std::size_t(language)];
        const QJsonValue value = training.value(key(benchmark::languageName(language)));
        if (!value.isObject())
            return ArtifactMetadataError::TrainingSourceMismatch;
        const QJsonObject o = value.toObject();
        if (o.value(key("sha256")).toString() != expected.sha256 || !isLowerHex64(expected.sha256)
            || o.value(key("notice")).toString() != expected.notice || expected.notice.isEmpty()
            || o.value(key("noticeSha256")).toString() != expected.noticeSha256
            || !isLowerHex64(expected.noticeSha256))
            return ArtifactMetadataError::TrainingSourceMismatch;
    }
    const QJsonValue scope = root.value(key("scope"));
    if (!scope.isString() || !scope.toString().contains(QLatin1String("release_approved=false"))
        || !scope.toString().contains(QLatin1String("not approved for distribution")))
        return ArtifactMetadataError::DistributionScope;
    return ArtifactMetadataError::None;
}

} // namespace rasklad::domain
