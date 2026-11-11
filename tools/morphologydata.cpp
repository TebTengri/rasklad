#include "morphologydata.h"

#include "benchmarkio.h"
#include "detectormanifest.h"

#include <QCryptographicHash>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QJsonValue>
#include <QStringDecoder>
#include <QStringList>

#include <cmath>
#include <initializer_list>

namespace rasklad::morphology {
namespace {

void put(QJsonObject &object, const char *key, const QJsonValue &value)
{
    object.insert(QString::fromLatin1(key), value);
}

QJsonValue text(const char *latin1)
{
    return QJsonValue(QString::fromLatin1(latin1));
}

bool isLowerHex(const QString &value, qsizetype length)
{
    if (value.size() != length)
        return false;
    for (const QChar ch : value) {
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

bool readInteger(const QJsonObject &object, const char *key, qint64 minimum, qint64 maximum, qint64 &out)
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

bool readNotice(const QJsonObject &object, NoticeRef &out)
{
    return readString(object, "notice", out.notice) && readString(object, "notice_sha256", out.sha256)
        && isLowerHex(out.sha256, 64);
}

bool hasSafeSegments(const QString &path, bool allowPlus)
{
    if (path.isEmpty() || path.size() > kMaxPathChars || path.endsWith(QLatin1Char('/')))
        return false;
    for (const QChar ch : path) {
        const char16_t c = ch.unicode();
        const bool ok = (c >= u'a' && c <= u'z') || (c >= u'A' && c <= u'Z') || (c >= u'0' && c <= u'9')
            || c == u'.' || c == u'_' || c == u'-' || c == u'/' || (allowPlus && c == u'+');
        if (!ok)
            return false;
    }
    return true;
}

bool segmentsClean(const QString &path)
{
    const QStringList segments = path.split(QLatin1Char('/'));
    for (const QString &segment : segments) {
        if (segment.isEmpty() || segment == QLatin1String(".") || segment == QLatin1String(".."))
            return false;
    }
    return true;
}

bool readFileRef(const QJsonObject &object, const char *pathKey, const char *shaKey, const char *bytesKey,
                 qint64 maxBytes, FileRef &out)
{
    return readString(object, pathKey, out.path) && readString(object, shaKey, out.sha256)
        && isLowerHex(out.sha256, 64) && readInteger(object, bytesKey, 1, maxBytes, out.bytes);
}

MorphManifestError parseLanguage(const QJsonValue &value, MorphManifest &manifest, std::array<bool, 2> &seen)
{
    if (!value.isObject())
        return MorphManifestError::BadLanguages;
    const QJsonObject object = value.toObject();
    QString language;
    if (!readString(object, "language", language))
        return MorphManifestError::BadLanguages;
    std::size_t index = 0;
    if (language == QLatin1String("en"))
        index = std::size_t(DetectorLanguage::En);
    else if (language == QLatin1String("ru"))
        index = std::size_t(DetectorLanguage::Ru);
    else
        return MorphManifestError::BadLanguages;
    if (seen[index])
        return MorphManifestError::BadLanguages;
    seen[index] = true;

    MorphLanguage &l = manifest.languages[index];
    if (!readFileRef(object, "dic_path", "dic_sha256", "dic_bytes", kMaxMorphDicBytes, l.dic)
        || !readFileRef(object, "aff_path", "aff_sha256", "aff_bytes", kMaxAffBytes, l.aff)
        || !readFileRef(object, "control_aff_path", "control_aff_sha256", "control_aff_bytes", kMaxAffBytes,
                        l.controlAff)
        || !readNotice(object, l.notice))
        return MorphManifestError::BadLanguageField;
    if (l.aff.sha256 == l.controlAff.sha256)
        return MorphManifestError::BadLanguageField;
    if (!isSafeProjectPath(l.dic.path, kDicPrefix) || !isSafeProjectPath(l.aff.path, kAffPrefix)
        || l.aff.path.startsWith(QLatin1String(kControlAffPrefix))
        || !isSafeProjectPath(l.controlAff.path, kControlAffPrefix))
        return MorphManifestError::BadPath;
    if (!experiment::isSafeNoticePath(l.notice.notice))
        return MorphManifestError::BadNoticePath;
    return MorphManifestError::None;
}

MorphManifestError parseHunspell(const QJsonValue &value, MorphManifest &manifest)
{
    if (!value.isObject())
        return MorphManifestError::BadHunspell;
    const QJsonObject object = value.toObject();
    if (!readString(object, "version", manifest.hunspellVersion)
        || manifest.hunspellVersion != QLatin1String(kRequiredHunspellVersion))
        return MorphManifestError::BadHunspell;
    if (!readFileRef(object, "library_path", "library_sha256", "library_bytes", kMaxLibraryBytes,
                     manifest.library)
        || !readNotice(object, manifest.libraryNotice))
        return MorphManifestError::BadHunspell;
    if (!isSafeLibraryPath(manifest.library.path))
        return MorphManifestError::BadLibraryPath;
    if (!experiment::isSafeNoticePath(manifest.libraryNotice.notice))
        return MorphManifestError::BadNoticePath;
    if (object.contains(QLatin1String("additional_notices"))) {
        const QJsonValue extra = object.value(QLatin1String("additional_notices"));
        if (!extra.isArray() || extra.toArray().size() > kMaxAdditionalNotices)
            return MorphManifestError::BadHunspell;
        const QJsonArray array = extra.toArray();
        for (qsizetype i = 0; i < array.size(); ++i) {
            NoticeRef n;
            if (!array.at(i).isObject() || !readNotice(array.at(i).toObject(), n))
                return MorphManifestError::BadHunspell;
            if (!experiment::isSafeNoticePath(n.notice))
                return MorphManifestError::BadNoticePath;
            manifest.additionalLibraryNotices.push_back(n);
        }
    }
    return MorphManifestError::None;
}

bool isSpaceOrTab(char c)
{
    return c == ' ' || c == '\t';
}

} // namespace

// ---- Протокол ----

QJsonObject expectedProtocol003()
{
    QJsonObject root;
    put(root, "schema_version", 1);
    put(root, "experiment", text("SYS-235 experiment 003 morphology control"));
    put(root, "freeze_date", text("2026-11-11"));

    QJsonObject holdout;
    put(holdout, "seed", text(benchmark::kHoldoutSeed));
    put(holdout, "dedup", text("exact text keep smallest numeric ID"));
    put(holdout, "rule",
        text("rank exactly as experiment002; reserve first5000(language) as old diagnostic set; new test "
             "ranks5000..9999 zero-based"));
    put(holdout, "offset", kHoldoutOffset);
    put(holdout, "count_per_language", kHoldoutCount);
    put(holdout, "source",
        text("same pinned Tatoeba CC0 snapshot as002; distinct sentence IDs/texts, correlated "
             "source/templates; not new-domain independence"));
    put(root, "holdout", holdout);

    put(root, "tokenization",
        text("same fixed tokenizer and wrong generation as experiment002; case preserved; >=4 decision gate"));
    put(root, "model", text("original experiment001 train only, no algorithm or parameter changes"));
    put(root, "model_sha256", text(kModelSha256));

    QJsonObject thresholds;
    put(thresholds, "gap", benchmark::kTwoScoreGap);
    put(thresholds, "minAlternative", benchmark::kTwoScoreMinAlternative);
    put(thresholds, "maxObserved", benchmark::kTwoScoreMaxObserved);
    put(root, "thresholds", thresholds);

    QJsonObject morphology;
    put(morphology, "engine",
        text("installed Hunspell1.7.5 C API spell only; UTF-8; no suggestions/analyze/generation"));
    put(morphology, "full", text("original pinned aff+dic; native case and flags retained"));
    put(morphology, "control",
        text("same dic and aff with all PFX/SFX directive rows removed; other directives, case and compound "
             "rules preserved"));
    put(morphology, "warning",
        text("control still has case, compounds and other semantics; call it noPfxSfx, not no-morphology"));
    put(root, "morphology", morphology);

    QJsonArray methods;
    for (const char *name : {"alwaysKeep", "stemExact", "twoScore", "stemThenModel", "modelConfirmedStem",
                             "hunspellNoPfxSfx", "hunspellFull", "morphThenModel", "modelConfirmedMorph"})
        methods.append(QString::fromLatin1(name));
    put(root, "methods", methods);

    QJsonObject rules;
    put(rules, "exact", text("observed hit Keep; else alternative hit Correct; else Abstain"));
    put(rules, "fallback", text("exact decision unless Abstain; then twoScore"));
    put(rules, "confirmation",
        text("observed hit Keep; otherwise Correct only if alt hit and twoScore Correct; otherwise Abstain"));
    put(root, "rules", rules);

    put(root, "ambiguity",
        text("union of stem-both, noPfxSfx-both, full-morph-both on actual case-preserved candidate pair; use "
             "same label for every method; any Correct unsafe, never recovered; separately show raw valid "
             "change totals"));

    QJsonObject hypotheses;
    put(hypotheses, "M1",
        text("full affix engine improves correct-word acceptance/recovery over noPfxSfx control; count added "
             "false changes"));
    put(hypotheses, "M2",
        text("morphFull closes recovery gap to compact twoScore without extra unsafe changes; explicit "
             "paired deltas"));
    put(hypotheses, "M3", text("twoScore confirmation reduces morphFull unsafe changes at recovery cost"));
    put(hypotheses, "M4", text("morphThenModel still adds useful OOV recovery; quantify unsafe additions"));
    put(root, "hypotheses", hypotheses);

    put(root, "challenge",
        text("same original fixtures and noise sequence as002 (exclude old full stem lexicons only, not new "
             "morph acceptance); same645 cases; also report new morph ambiguity labels; do not choose new "
             "negatives using test results"));
    // Match the case-sensitive clarification frozen before evaluation.
    put(root, "metrics", text("occurrences plus exact case-preserved unique types for all methods (Hunspell case semantics); not directly comparable to002 canonical-folded types; language/length; word acceptance and pairwise changes vs control, stems, model, morph; full coverage denominators and unsafe counts; illustrative PPV at0.1%,1%,5% wrong prevalence"));
    put(root, "resources",
        text("fresh probe pair of full Hunspell engines using only approved aff/dic; no corpus/model/train "
             "lists; actual child RSS and load wall, warm uncached membership, explicit .aff+.dic and "
             "shared-lib sizes; no expansion to all forms"));
    put(root, "decision",
        text("no system input; no threshold or exception tuning after viewing new block; "
             "supported/refuted/inconclusive for each hypothesis only on sample; no world novelty or "
             "deployment safety claims"));
    put(root, "release_approved", false);
    return root;
}

benchmark::ProtocolResult validateProtocol003(const QByteArray &bytes)
{
    using benchmark::ProtocolError;
    benchmark::ProtocolResult result;
    if (bytes.size() > benchmark::kMaxProtocolBytes) {
        result.error = ProtocolError::TooLarge;
        return result;
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(bytes, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        result.error = ProtocolError::InvalidJson;
        return result;
    }
    const QJsonObject actual = document.object();
    const QJsonObject expected = expectedProtocol003();
    for (auto it = expected.constBegin(); it != expected.constEnd(); ++it) {
        if (!actual.contains(it.key()) || !benchmark::jsonEqual(it.value(), actual.value(it.key()))) {
            result.error = ProtocolError::Mismatch;
            result.mismatchedField = it.key();
            return result;
        }
    }
    if (actual.size() != expected.size()) {
        result.error = ProtocolError::Mismatch;
        result.mismatchedField = QStringLiteral("unexpected field");
        return result;
    }
    result.sha256 = experiment::sha256Hex(bytes);
    return result;
}

// ---- Паспорт ----

QString morphManifestErrorText(MorphManifestError error)
{
    switch (error) {
    case MorphManifestError::None:
        return QStringLiteral("ok");
    case MorphManifestError::TooLarge:
        return QStringLiteral("morphology manifest exceeds size limit");
    case MorphManifestError::InvalidJson:
        return QStringLiteral("morphology manifest is not a JSON object");
    case MorphManifestError::SchemaVersion:
        return QStringLiteral("morphology manifest schema_version is not 1");
    case MorphManifestError::StatusNotApproved:
        return QStringLiteral("morphology manifest status is not approved-local-morphology");
    case MorphManifestError::ScopeMismatch:
        return QStringLiteral("morphology manifest scope is not local-experiment-only");
    case MorphManifestError::ReleaseApproved:
        return QStringLiteral("morphology manifest release_approved must be boolean false");
    case MorphManifestError::BadHunspell:
        return QStringLiteral("morphology manifest hunspell object is invalid or not version 1.7.5");
    case MorphManifestError::BadLibraryPath:
        return QStringLiteral("morphology manifest library_path is not a safe absolute path under /usr/lib");
    case MorphManifestError::BadLanguages:
        return QStringLiteral("morphology manifest languages must be exactly en and ru");
    case MorphManifestError::BadLanguageField:
        return QStringLiteral("morphology manifest language sha256/bytes/notice fields are invalid");
    case MorphManifestError::BadPath:
        return QStringLiteral("morphology manifest dic/aff/control path is outside its approved prefix");
    case MorphManifestError::BadNoticePath:
        return QStringLiteral("morphology manifest notice path is not a safe relative path");
    }
    return QStringLiteral("unknown morphology manifest error");
}

MorphManifestResult parseMorphManifest(const QByteArray &bytes)
{
    MorphManifestResult result;
    if (bytes.size() > kMaxMorphManifestBytes) {
        result.error = MorphManifestError::TooLarge;
        return result;
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(bytes, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        result.error = MorphManifestError::InvalidJson;
        return result;
    }
    const QJsonObject root = document.object();
    MorphManifest &manifest = result.manifest;
    qint64 schema = 0;
    QString status;
    QString scope;
    if (!readInteger(root, "schema_version", 1, 1, schema)) {
        result.error = MorphManifestError::SchemaVersion;
        return result;
    }
    if (!readString(root, "status", status) || status != QLatin1String(kMorphRequiredStatus)) {
        result.error = MorphManifestError::StatusNotApproved;
        return result;
    }
    if (!readString(root, "scope", scope) || scope != QLatin1String(kMorphRequiredScope)) {
        result.error = MorphManifestError::ScopeMismatch;
        return result;
    }
    const QJsonValue release = root.value(QLatin1String("release_approved"));
    if (!release.isBool() || release.toBool()) {
        result.error = MorphManifestError::ReleaseApproved;
        return result;
    }
    result.error = parseHunspell(root.value(QLatin1String("hunspell")), manifest);
    if (result.error != MorphManifestError::None)
        return result;

    const QJsonValue languagesValue = root.value(QLatin1String("languages"));
    if (!languagesValue.isArray() || languagesValue.toArray().size() != 2) {
        result.error = MorphManifestError::BadLanguages;
        return result;
    }
    const QJsonArray languages = languagesValue.toArray();
    std::array<bool, 2> seen{{false, false}};
    for (qsizetype i = 0; i < languages.size(); ++i) {
        result.error = parseLanguage(languages.at(i), manifest, seen);
        if (result.error != MorphManifestError::None)
            return result;
    }
    if (!seen[0] || !seen[1]) {
        result.error = MorphManifestError::BadLanguages;
        return result;
    }
    // Шесть файлов языков различны по пути.
    const std::array<const QString *, 6> paths{{&manifest.languages[0].dic.path, &manifest.languages[0].aff.path,
                                                &manifest.languages[0].controlAff.path,
                                                &manifest.languages[1].dic.path, &manifest.languages[1].aff.path,
                                                &manifest.languages[1].controlAff.path}};
    for (std::size_t a = 0; a < paths.size(); ++a) {
        for (std::size_t b = a + 1; b < paths.size(); ++b) {
            if (*paths[a] == *paths[b]) {
                result.error = MorphManifestError::BadPath;
                return result;
            }
        }
    }
    manifest.manifestSha256 = experiment::sha256Hex(bytes);
    return result;
}

bool isSafeProjectPath(const QString &relative, const char *prefix)
{
    if (!prefix || !relative.startsWith(QLatin1String(prefix)))
        return false;
    if (relative.size() <= qsizetype(qstrlen(prefix)))
        return false;
    return hasSafeSegments(relative, false) && segmentsClean(relative);
}

bool isSafeLibraryPath(const QString &absolute)
{
    if (!absolute.startsWith(QLatin1String(kLibraryPrefix))
        || absolute.size() <= qsizetype(qstrlen(kLibraryPrefix)))
        return false;
    // Ведущий '/' даёт пустой первый сегмент: проверяем остаток после него.
    return hasSafeSegments(absolute, true) && segmentsClean(absolute.mid(1));
}

QString resolveProjectFile(const QString &projectRoot, const QString &relative, QString &error)
{
    const QString rootCanonical = QFileInfo(projectRoot).canonicalFilePath();
    if (rootCanonical.isEmpty()) {
        error = QStringLiteral("project root is not available");
        return {};
    }
    const QString buildPath = rootCanonical + QStringLiteral("/build");
    const QFileInfo buildInfo(buildPath);
    if (buildInfo.isSymLink() || !buildInfo.isDir() || buildInfo.canonicalFilePath() != buildPath) {
        error = QStringLiteral("project build directory is missing or a symbolic link");
        return {};
    }
    if (!relative.startsWith(QLatin1String("build/")) || !hasSafeSegments(relative, false)
        || !segmentsClean(relative)) {
        error = QStringLiteral("path is not a safe project path under build/");
        return {};
    }
    const QString joined = rootCanonical + QLatin1Char('/') + relative;
    const QFileInfo info(joined);
    if (!info.exists() || !info.isFile()) {
        error = QStringLiteral("not a regular file");
        return {};
    }
    if (info.canonicalFilePath() != joined || !benchmark::isStrictlyInside(joined, buildPath)) {
        error = QStringLiteral("path resolves through a symbolic link or outside build/");
        return {};
    }
    return joined;
}

// ---- Контроль .aff ----

QString affControlErrorText(AffControlError error)
{
    switch (error) {
    case AffControlError::None:
        return QStringLiteral("ok");
    case AffControlError::TooLarge:
        return QStringLiteral("aff exceeds size limit");
    case AffControlError::Empty:
        return QStringLiteral("aff is empty");
    case AffControlError::NulByte:
        return QStringLiteral("aff contains a NUL byte");
    case AffControlError::NonExactAffixKeyword:
        return QStringLiteral("aff has a directive starting with PFX/SFX that is not exactly PFX/SFX");
    case AffControlError::BomBeforeAffixRow:
        return QStringLiteral("aff starts with a BOM followed by a PFX/SFX row");
    case AffControlError::NoAffixRows:
        return QStringLiteral("aff has no PFX/SFX rows; control would equal the source");
    }
    return QStringLiteral("unknown aff control error");
}

AffControlResult removePfxSfxRows(const QByteArray &source)
{
    AffControlResult result;
    if (source.size() > kMaxAffBytes) {
        result.error = AffControlError::TooLarge;
        return result;
    }
    if (source.isEmpty()) {
        result.error = AffControlError::Empty;
        return result;
    }
    if (source.contains('\0')) {
        result.error = AffControlError::NulByte;
        return result;
    }
    static const QByteArray bom("\xEF\xBB\xBF");
    QByteArray out;
    out.reserve(source.size());
    qsizetype start = 0;
    const qsizetype n = source.size();
    while (start < n) {
        const qsizetype newline = source.indexOf('\n', start);
        const qsizetype end = newline < 0 ? n : newline + 1;
        const QByteArrayView row(source.constData() + start, end - start);
        ++result.rows;

        qsizetype b = 0;
        qsizetype e = row.size();
        if (e > 0 && row.at(e - 1) == '\n')
            --e;
        if (e > 0 && row.at(e - 1) == '\r')
            --e;
        bool bomRow = false;
        if (start == 0 && row.startsWith(bom)) {
            b = bom.size();
            bomRow = true;
        }
        while (b < e && isSpaceOrTab(row.at(b)))
            ++b;
        qsizetype f = b;
        while (f < e && !isSpaceOrTab(row.at(f)))
            ++f;
        const QByteArrayView field = row.sliced(b, f - b);
        const bool pfx = field == QByteArrayView("PFX");
        const bool sfx = field == QByteArrayView("SFX");
        if (!pfx && !sfx && (field.startsWith(QByteArrayView("PFX")) || field.startsWith(QByteArrayView("SFX")))) {
            result = AffControlResult();
            result.error = AffControlError::NonExactAffixKeyword;
            return result;
        }
        if ((pfx || sfx) && bomRow) {
            result = AffControlResult();
            result.error = AffControlError::BomBeforeAffixRow;
            return result;
        }
        if (pfx)
            ++result.removedPfxRows;
        else if (sfx)
            ++result.removedSfxRows;
        else {
            ++result.keptRows;
            out.append(row);
        }
        start = end;
    }
    if (result.removedPfxRows + result.removedSfxRows == 0) {
        result = AffControlResult();
        result.error = AffControlError::NoAffixRows;
        return result;
    }
    result.bytes = std::move(out);
    return result;
}

bool isStrictUtf8(const QByteArray &bytes)
{
    QStringDecoder decoder(QStringDecoder::Utf8, QStringDecoder::Flag::Stateless);
    const QString decoded = decoder(bytes);
    static_cast<void>(decoded);
    return !decoder.hasError();
}

} // namespace rasklad::morphology
