#include "evaluationdata.h"

#include "detectormanifest.h"

#include <QChar>
#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QList>
#include <QSet>
#include <QStringDecoder>

#include <algorithm>
#include <cmath>

namespace rasklad::benchmark {
namespace {

bool isLowerHex(const QString &text, qsizetype length)
{
    if (text.size() != length)
        return false;
    for (const QChar ch : text) {
        const char16_t c = ch.unicode();
        if (!((c >= u'0' && c <= u'9') || (c >= u'a' && c <= u'f')))
            return false;
    }
    return true;
}

bool readString(const QJsonObject &object, const char *key, QString &out)
{
    const QJsonValue value = object.value(QLatin1String(key));
    if (!value.isString())
        return false;
    out = value.toString();
    return true;
}

bool readInteger(const QJsonObject &object, const char *key, qint64 minimum, qint64 maximum,
                 qint64 &out)
{
    const QJsonValue value = object.value(QLatin1String(key));
    if (!value.isDouble())
        return false;
    const double d = value.toDouble();
    if (!std::isfinite(d) || std::floor(d) != d || d < double(minimum) || d > double(maximum))
        return false;
    out = qint64(d);
    return true;
}

EvalManifestError parseSource(const QJsonValue &value, EvalManifest &manifest, std::array<bool, 2> &seen)
{
    if (!value.isObject())
        return EvalManifestError::BadSources;
    const QJsonObject object = value.toObject();
    QString language;
    if (!readString(object, "language", language))
        return EvalManifestError::BadSources;
    std::size_t index = 0;
    DetectorLanguage detectorLanguage = DetectorLanguage::En;
    if (language == QLatin1String("en")) {
        detectorLanguage = DetectorLanguage::En;
    } else if (language == QLatin1String("ru")) {
        detectorLanguage = DetectorLanguage::Ru;
    } else {
        return EvalManifestError::BadSources;
    }
    index = std::size_t(detectorLanguage);
    if (seen[index])
        return EvalManifestError::BadSources;
    seen[index] = true;

    EvalSource &source = manifest.sources[index];
    if (!readString(object, "iso_language", source.isoLanguage)
        || source.isoLanguage != QLatin1String(isoLanguageFor(detectorLanguage)))
        return EvalManifestError::BadIsoLanguage;
    if (!readString(object, "sha256", source.sha256) || !isLowerHex(source.sha256, 64))
        return EvalManifestError::BadSourceField;
    if (!readString(object, "notice_sha256", source.noticeSha256)
        || !isLowerHex(source.noticeSha256, 64))
        return EvalManifestError::BadSourceField;
    if (!readInteger(object, "bytes", 1, kMaxEvalSourceBytes, source.bytes))
        return EvalManifestError::BadSourceField;
    if (!readString(object, "notice", source.notice))
        return EvalManifestError::BadSourceField;
    if (!experiment::isSafeNoticePath(source.notice))
        return EvalManifestError::BadNoticePath;
    return EvalManifestError::None;
}

bool isTrimCharacter(QChar ch, const QString &set)
{
    return set.contains(ch);
}

// Побайтовое беззнаковое сравнение дайджестов.
bool digestLess(const QByteArray &a, const QByteArray &b)
{
    const qsizetype common = std::min(a.size(), b.size());
    for (qsizetype i = 0; i < common; ++i) {
        const auto x = static_cast<unsigned char>(a.at(i));
        const auto y = static_cast<unsigned char>(b.at(i));
        if (x != y)
            return x < y;
    }
    return a.size() < b.size();
}

} // namespace

QString outerTrimCharacters()
{
    // . ! ? ; : , ( ) [ ] { } " « » “ ” „
    return QString::fromUtf16(u".!?;:,()[]{}\"«»“”„");
}

const char *isoLanguageFor(DetectorLanguage language)
{
    return language == DetectorLanguage::Ru ? "rus" : "eng";
}

QString evalManifestErrorText(EvalManifestError error)
{
    switch (error) {
    case EvalManifestError::None:
        return QStringLiteral("ok");
    case EvalManifestError::TooLarge:
        return QStringLiteral("evaluation manifest exceeds size limit");
    case EvalManifestError::InvalidJson:
        return QStringLiteral("evaluation manifest is not a JSON object");
    case EvalManifestError::SchemaVersion:
        return QStringLiteral("evaluation manifest schema_version is not 1");
    case EvalManifestError::StatusNotApproved:
        return QStringLiteral("evaluation manifest status is not approved-local-evaluation");
    case EvalManifestError::ScopeMismatch:
        return QStringLiteral("evaluation manifest scope is not local-evaluation-only");
    case EvalManifestError::ReleaseApproved:
        return QStringLiteral("evaluation manifest release_approved must be boolean false");
    case EvalManifestError::BadSources:
        return QStringLiteral("evaluation_sources must be exactly en and ru");
    case EvalManifestError::BadSourceField:
        return QStringLiteral("evaluation source sha256/bytes/notice fields are invalid");
    case EvalManifestError::BadIsoLanguage:
        return QStringLiteral("evaluation source iso_language must be eng for en and rus for ru");
    case EvalManifestError::BadNoticePath:
        return QStringLiteral("evaluation notice path is not a safe relative path");
    }
    return QStringLiteral("unknown evaluation manifest error");
}

EvalManifestResult parseEvaluationManifest(const QByteArray &bytes)
{
    EvalManifestResult result;
    if (bytes.size() > kMaxEvalManifestBytes) {
        result.error = EvalManifestError::TooLarge;
        return result;
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(bytes, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        result.error = EvalManifestError::InvalidJson;
        return result;
    }
    const QJsonObject root = document.object();
    EvalManifest &manifest = result.manifest;

    qint64 schema = 0;
    if (!readInteger(root, "schema_version", 1, 1, schema)) {
        result.error = EvalManifestError::SchemaVersion;
        return result;
    }
    if (!readString(root, "status", manifest.status)
        || manifest.status != QLatin1String(kEvalRequiredStatus)) {
        result.error = EvalManifestError::StatusNotApproved;
        return result;
    }
    if (!readString(root, "scope", manifest.scope)
        || manifest.scope != QLatin1String(kEvalRequiredScope)) {
        result.error = EvalManifestError::ScopeMismatch;
        return result;
    }
    const QJsonValue release = root.value(QLatin1String("release_approved"));
    if (!release.isBool() || release.toBool()) {
        result.error = EvalManifestError::ReleaseApproved;
        return result;
    }
    const QJsonValue sourcesValue = root.value(QLatin1String("evaluation_sources"));
    if (!sourcesValue.isArray()) {
        result.error = EvalManifestError::BadSources;
        return result;
    }
    const QJsonArray sources = sourcesValue.toArray();
    if (sources.size() != 2) {
        result.error = EvalManifestError::BadSources;
        return result;
    }
    std::array<bool, 2> seen{{false, false}};
    for (qsizetype i = 0; i < sources.size(); ++i) {
        const EvalManifestError error = parseSource(sources.at(i), manifest, seen);
        if (error != EvalManifestError::None) {
            result.error = error;
            return result;
        }
    }
    if (!seen[0] || !seen[1]) {
        result.error = EvalManifestError::BadSources;
        return result;
    }
    manifest.manifestSha256 = experiment::sha256Hex(bytes);
    return result;
}

QString tsvErrorText(TsvError error)
{
    switch (error) {
    case TsvError::None:
        return QStringLiteral("ok");
    case TsvError::TooLarge:
        return QStringLiteral("evaluation file exceeds size limit");
    case TsvError::Empty:
        return QStringLiteral("evaluation file has no records");
    case TsvError::InvalidUtf8:
        return QStringLiteral("evaluation file is not valid UTF-8");
    case TsvError::EmptyLine:
        return QStringLiteral("evaluation file contains an empty line");
    case TsvError::RowTooLong:
        return QStringLiteral("evaluation record exceeds byte limit");
    case TsvError::ControlCharacter:
        return QStringLiteral("evaluation record contains a control character");
    case TsvError::BadColumns:
        return QStringLiteral("evaluation record does not have exactly 3 tab-separated columns");
    case TsvError::BadId:
        return QStringLiteral("evaluation record ID is not a positive canonical decimal");
    case TsvError::DuplicateId:
        return QStringLiteral("evaluation record ID is duplicated");
    case TsvError::LanguageMismatch:
        return QStringLiteral("evaluation record language does not match the manifest");
    case TsvError::EmptySentence:
        return QStringLiteral("evaluation record sentence is empty");
    case TsvError::SentenceTooLong:
        return QStringLiteral("evaluation record sentence exceeds length limit");
    case TsvError::TooManyRecords:
        return QStringLiteral("evaluation file exceeds record count limit");
    }
    return QStringLiteral("unknown evaluation file error");
}

qint64 utf8Length(QStringView text)
{
    qint64 bytes = 0;
    for (qsizetype i = 0; i < text.size(); ++i) {
        const char16_t c = text.at(i).unicode();
        if (c < 0x80) {
            bytes += 1;
        } else if (c < 0x800) {
            bytes += 2;
        } else if (QChar::isHighSurrogate(c) && i + 1 < text.size()
                   && QChar::isLowSurrogate(text.at(i + 1).unicode())) {
            bytes += 4;
            ++i;
        } else {
            bytes += 3;
        }
    }
    return bytes;
}

TsvParseResult parseTatoebaTsv(const QByteArray &data, QStringView isoLanguage)
{
    TsvParseResult result;
    auto failWith = [&result](TsvError error) {
        result.error = error;
        result.records.clear();
        return result;
    };
    if (data.size() > kMaxEvalSourceBytes)
        return failWith(TsvError::TooLarge);
    if (data.isEmpty())
        return failWith(TsvError::Empty);
    // До декодирования, split(), reserve() и QSet: байт 0x0A в UTF-8
    // встречается только как '\n'. Финальный '\n' запись не добавляет.
    const qint64 newlines = data.count('\n');
    const qint64 recordLines = data.endsWith('\n') ? newlines : newlines + 1;
    if (recordLines > kMaxEvalRecords)
        return failWith(TsvError::TooManyRecords);

    // ConvertInitialBom: BOM не скрывается, а попадает в первый ID и даёт отказ.
    QStringDecoder decoder(QStringConverter::Utf8,
                           QStringConverter::Flag::Stateless | QStringConverter::Flag::ConvertInitialBom);
    const QString text = decoder.decode(data);
    if (decoder.hasError())
        return failWith(TsvError::InvalidUtf8);

    const QList<QStringView> lines = QStringView(text).split(u'\n');
    qsizetype lineCount = lines.size();
    if (lineCount > 0 && lines.at(lineCount - 1).isEmpty())
        --lineCount;
    if (lineCount == 0)
        return failWith(TsvError::Empty);

    QSet<qint64> ids;
    result.records.reserve(std::size_t(lineCount));
    for (qsizetype i = 0; i < lineCount; ++i) {
        const QStringView line = lines.at(i);
        if (line.isEmpty())
            return failWith(TsvError::EmptyLine);
        if (utf8Length(line) > kMaxRowBytes)
            return failWith(TsvError::RowTooLong);
        int tabs = 0;
        for (const QChar ch : line) {
            if (ch == u'\t') {
                ++tabs;
                continue;
            }
            if (ch.category() == QChar::Other_Control)
                return failWith(TsvError::ControlCharacter);
        }
        if (tabs != 2)
            return failWith(TsvError::BadColumns);
        const qsizetype firstTab = line.indexOf(u'\t');
        const qsizetype secondTab = line.indexOf(u'\t', firstTab + 1);
        const QStringView idField = line.left(firstTab);
        const QStringView isoField = line.mid(firstTab + 1, secondTab - firstTab - 1);
        const QStringView sentence = line.mid(secondTab + 1);

        if (idField.isEmpty() || idField.size() > kMaxIdDigits || idField.at(0) == u'0')
            return failWith(TsvError::BadId);
        qint64 id = 0;
        for (const QChar ch : idField) {
            const char16_t c = ch.unicode();
            if (c < u'0' || c > u'9')
                return failWith(TsvError::BadId);
            id = id * 10 + qint64(c - u'0');
        }
        if (id <= 0)
            return failWith(TsvError::BadId);
        if (isoField != isoLanguage)
            return failWith(TsvError::LanguageMismatch);
        if (sentence.isEmpty())
            return failWith(TsvError::EmptySentence);
        if (sentence.size() > kMaxSentenceUtf16)
            return failWith(TsvError::SentenceTooLong);
        if (ids.contains(id))
            return failWith(TsvError::DuplicateId);
        ids.insert(id);
        result.records.push_back({id, sentence.toString()});
    }
    return result;
}

QByteArray sampleRankDigest(const QByteArray &seed, qint64 id)
{
    QByteArray input = seed;
    input.append('\0');
    input.append(QByteArray::number(id));
    return QCryptographicHash::hash(input, QCryptographicHash::Sha256);
}

SelectionResult selectSentences(std::vector<SentenceRecord> records, const QByteArray &seed, int limit)
{
    SelectionResult result;
    SelectionReport &report = result.report;
    report.records = qint64(records.size());

    std::sort(records.begin(), records.end(),
              [](const SentenceRecord &a, const SentenceRecord &b) { return a.id < b.id; });
    std::vector<SentenceRecord> unique;
    unique.reserve(records.size());
    QSet<QString> seen;
    for (SentenceRecord &record : records) {
        if (seen.contains(record.text)) {
            ++report.duplicateTextRecords;
            continue;
        }
        seen.insert(record.text);
        unique.push_back(std::move(record));
    }
    records.clear();
    seen.clear();
    report.uniqueTexts = qint64(unique.size());

    struct Ranked {
        QByteArray digest;
        qint64 id = 0;
        std::size_t index = 0;
    };
    std::vector<Ranked> ranked;
    ranked.reserve(unique.size());
    for (std::size_t i = 0; i < unique.size(); ++i)
        ranked.push_back({sampleRankDigest(seed, unique[i].id), unique[i].id, i});
    std::sort(ranked.begin(), ranked.end(), [](const Ranked &a, const Ranked &b) {
        if (a.digest != b.digest)
            return digestLess(a.digest, b.digest);
        return a.id < b.id;
    });

    const std::size_t take = limit > 0 ? std::min(ranked.size(), std::size_t(limit)) : 0;
    QByteArray idList;
    result.selected.reserve(take);
    for (std::size_t i = 0; i < take; ++i) {
        idList.append(QByteArray::number(ranked[i].id));
        idList.append('\n');
        result.selected.push_back(std::move(unique[ranked[i].index]));
    }
    report.selected = qint64(take);
    report.selectedIdsSha256 = experiment::sha256Hex(idList);
    return result;
}

std::vector<QString> tokenizeSentence(QStringView sentence, TokenizeStats *stats)
{
    static const QString trimSet = outerTrimCharacters();
    std::vector<QString> tokens;
    qsizetype i = 0;
    const qsizetype n = sentence.size();
    while (i < n) {
        while (i < n && sentence.at(i).isSpace())
            ++i;
        if (i >= n)
            break;
        const qsizetype start = i;
        while (i < n && !sentence.at(i).isSpace())
            ++i;
        const QStringView piece = sentence.mid(start, i - start);
        if (stats)
            ++stats->pieces;
        qsizetype b = 0;
        qsizetype e = piece.size();
        while (b < e && isTrimCharacter(piece.at(b), trimSet))
            ++b;
        while (e > b && isTrimCharacter(piece.at(e - 1), trimSet))
            --e;
        if (b == e) {
            if (stats)
                ++stats->emptyAfterTrim;
            continue;
        }
        if (stats && (b > 0 || e < piece.size()))
            ++stats->trimmed;
        tokens.push_back(piece.mid(b, e - b).toString());
    }
    return tokens;
}

bool isNativeLetterOnly(QStringView token, DetectorLanguage language)
{
    if (token.size() < kMinTokenKeys || token.size() > kMaxTokenKeys)
        return false;
    for (const QChar ch : token) {
        const char16_t c = ch.unicode();
        bool ok = false;
        if (language == DetectorLanguage::En)
            ok = (c >= u'a' && c <= u'z') || (c >= u'A' && c <= u'Z');
        else if (language == DetectorLanguage::Ru)
            ok = (c >= u'а' && c <= u'я') || (c >= u'А' && c <= u'Я') || c == u'ё' || c == u'Ё';
        if (!ok)
            return false;
    }
    return true;
}

} // namespace rasklad::benchmark
