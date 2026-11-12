#include "profileverify.h"

#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QStringDecoder>
#include <QStringList>

#include <cmath>
#include <initializer_list>
#include <optional>
#include <utility>

namespace rasklad::profile {

namespace {

QString key(const char *name)
{
    return QString::fromLatin1(name);
}

QString latin(const char *text)
{
    return QString::fromLatin1(text);
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

// JSON integer in [min, max] without a fractional part.
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

bool integerEquals(const QJsonObject &o, const char *name, qint64 expected)
{
    qint64 value = 0;
    return exactInteger(o.value(key(name)), expected, expected, value);
}

bool stringEquals(const QJsonObject &o, const char *name, const QString &expected)
{
    const QJsonValue value = o.value(key(name));
    return !expected.isEmpty() && value.isString() && value.toString() == expected;
}

bool numberEquals(const QJsonObject &o, const char *name, double expected)
{
    const QJsonValue value = o.value(key(name));
    return value.isDouble() && value.toDouble() == expected;
}

bool releaseForbidden(const QJsonObject &o)
{
    const QJsonValue release = o.value(key("release_approved"));
    return release.isBool() && !release.toBool();
}

bool isSafeSegmentChar(QChar ch)
{
    const char16_t c = ch.unicode();
    return (c >= u'a' && c <= u'z') || (c >= u'A' && c <= u'Z') || (c >= u'0' && c <= u'9') || c == u'.'
        || c == u'_' || c == u'-';
}

bool sizeExceeds(const QString &path, qint64 limit)
{
    const QFileInfo info(path);
    return info.exists() && info.size() > limit;
}

// Resolve, read with a limit, compare size (if pinned) and SHA-256.
ProfileError readPinned(const QString &root, const FilePin &pin, qint64 limit, QByteArray &out)
{
    out.clear();
    const QString path = resolveContainedFile(root, pin.path);
    if (path.isEmpty())
        return ProfileError::MissingFile;
    if (sizeExceeds(path, limit))
        return ProfileError::SizeOrHash;
    if (!readBounded(path, limit, out))
        return ProfileError::MissingFile;
    if ((pin.bytes > 0 && qint64(out.size()) != pin.bytes) || sha256Hex(out) != pin.sha256) {
        out.clear();
        return ProfileError::SizeOrHash;
    }
    return ProfileError::None;
}

ProfileError verifyNotice(const QString &root, const NoticePin &notice)
{
    if (!notice.path.startsWith(QStringLiteral("data/notices/")))
        return ProfileError::NoticeMismatch;
    const QString path = resolveContainedFile(root, notice.path);
    if (path.isEmpty())
        return ProfileError::MissingFile;
    QByteArray bytes;
    if (!readBounded(path, kMaxNoticeBytes, bytes))
        return ProfileError::NoticeMismatch;
    return sha256Hex(bytes) == notice.sha256 ? ProfileError::None : ProfileError::NoticeMismatch;
}

bool noticeObjectEquals(const QJsonObject &o, const NoticePin &pin)
{
    return stringEquals(o, "notice", pin.path) && stringEquals(o, "notice_sha256", pin.sha256);
}

const char *languageCode(std::size_t index)
{
    return index == 0 ? "en" : "ru";
}

// Index of a languages[] entry ("en" -> 0, "ru" -> 1), or -1.
int languageIndex(const QJsonObject &o)
{
    const QJsonValue value = o.value(key("language"));
    if (!value.isString())
        return -1;
    if (value.toString() == QLatin1String("en"))
        return 0;
    if (value.toString() == QLatin1String("ru"))
        return 1;
    return -1;
}

bool filePinEquals(const QJsonObject &o, const char *path, const char *sha, const char *bytes, const FilePin &pin)
{
    return stringEquals(o, path, pin.path) && stringEquals(o, sha, pin.sha256) && integerEquals(o, bytes, pin.bytes);
}

bool pinnedObjectMatches(const QJsonValue &value, const FilePin &pin, const QString &artifactDir)
{
    if (!value.isObject())
        return false;
    const QJsonObject o = value.toObject();
    qint64 bytes = 0;
    if (!exactInteger(o.value(key("bytes")), 1, kMaxPinnedArtifactBytes, bytes) || bytes != pin.bytes)
        return false;
    if (!stringEquals(o, "sha256", pin.sha256))
        return false;
    if (!pin.path.startsWith(artifactDir))
        return false;
    const QString fileName = pin.path.mid(artifactDir.size());
    if (fileName.isEmpty() || fileName.contains(QLatin1Char('/')))
        return false;
    return stringEquals(o, "file", fileName);
}

ProfilePins makeProductionPins()
{
    ProfilePins p;
    p.morphManifest = {QStringLiteral("data/morphology-003-sources.json"),
                       QStringLiteral("590b2b69c75f73d6f5ebd817dc4e2f2798d877f090ac42a81fa0a02f5fbf2413"), 0};
    p.trainingManifest = {QStringLiteral("data/sources.json"),
                          QStringLiteral("29042aee03f871e0daa7da36e9401f35141594be798f6e9d411e878d8694cd20"), 0};
    p.protocol002 = {QStringLiteral("data/experiment-002-protocol.json"),
                     QStringLiteral("4e38458069c2aa0eb884392dc8eda841d2ecf1f27bcb7e2174c87d1e6c1889b8"), 0};
    p.hunspellVersion = QStringLiteral("1.7.5");
    p.library = {QStringLiteral("/usr/lib/libhunspell-1.7.so.0.2.1"),
                 QStringLiteral("6e4982568935899084a06b6b19b9399fc5b74e1de108285dfbe1bf028c063162"), 546936};
    p.libraryNotice = {QStringLiteral("data/notices/MORPHOLOGY-003-REVIEW.md"),
                       QStringLiteral("953d284b146176fb5482c6acbacf33eebc60ab37dbc9d7112cb900c752923d61")};
    p.additionalLibraryNotices = {
        {QStringLiteral("data/notices/Hunspell-COPYING.MPL.txt"),
         QStringLiteral("53692a2ed6c6a2c6ec9b32dd0b820dfae91e0a1fcdf625ca9ed0bdf8705fcc4f")},
        {QStringLiteral("data/notices/Hunspell-license.myspell.txt"),
         QStringLiteral("dcda061adeed22b26f276c01ec374f930789ae0d684e5aa8f8be309dd680ff20")},
    };
    p.dictionaryCommit = QStringLiteral("c0865a85eb67dc377f2cf8e8fe2c4fa9e53f0dcb");

    LanguagePins &en = p.languages[0];
    en.dic = {QStringLiteral("build/experiment-data/en.dic"),
              QStringLiteral("f0b1a234bd178bdd01875b2a392a9647f888b8fe879f79c52aae62c2759b3647"), 551762};
    en.aff = {QStringLiteral("build/experiment-003-data/en.aff"),
              QStringLiteral("e746c882dd6f303c2c46e7452804b9201115a6942cfeb15f18f8edf774d2e24e"), 3205};
    en.controlAff = {QStringLiteral("build/experiment-003-data/control/en.aff"),
                     QStringLiteral("60fde13d7b4856a2dcb4a70eb9320a7288e38f3cd11ca6a40509a1d22e81f08f"), 1460};
    en.notice = {QStringLiteral("data/notices/en_US.README.txt"),
                 QStringLiteral("168b4c01cb841f766a72f310b065c04d09cff46376c37a81d799593ce751c371")};
    en.artifactNoticePath = QStringLiteral("build/experiment-002-artifacts/en_US.NOTICE.txt");

    LanguagePins &ru = p.languages[1];
    ru.dic = {QStringLiteral("build/experiment-data/ru.dic"),
              QStringLiteral("f6047416a0204adbecf3a451b874ec8a97ee37e2cbc714466ef04d8dbcc0d6fc"), 3473191};
    ru.aff = {QStringLiteral("build/experiment-003-data/ru.aff"),
              QStringLiteral("38ce7d4af78e211e9bafe4bf7e3d6a2c420591136cb738ec6648f8fdf6524cd7"), 71236};
    ru.controlAff = {QStringLiteral("build/experiment-003-data/control/ru.aff"),
                     QStringLiteral("097549718b2cc53e0c597abb01a467fb8161ac9b3cac35287107717e6e71dd19"), 162};
    ru.notice = {QStringLiteral("data/notices/ru_RU.README.txt"),
                 QStringLiteral("262af2f6ad70a61e5ee1332ff44fa8ee50edca819cf33207d8ad6ba6a0c9be52")};
    ru.artifactNoticePath = QStringLiteral("build/experiment-002-artifacts/ru_RU.NOTICE.txt");

    p.artifactDir = QStringLiteral("build/experiment-002-artifacts/");
    p.model = {QStringLiteral("build/experiment-002-artifacts/detector-model.rskn"),
               QStringLiteral("e92e549e5f0853e4cd78d6d997653098b78550eb9911de11421434e26e3985d8"), 88290};
    p.stem = {QStringLiteral("build/experiment-002-artifacts/exact-full-lexicons.rskp"),
              QStringLiteral("3bdd740d4b39bf2b5fe361e0d3c670187563ec59f41913db3525340a30887871"), 1200379};
    p.artifactMetadataPath = QStringLiteral("build/experiment-002-artifacts/artifact-metadata.json");
    p.modificationsPath = QStringLiteral("build/experiment-002-artifacts/MODIFICATIONS.txt");
    return p;
}

} // namespace

const char *profileErrorName(ProfileError error)
{
    switch (error) {
    case ProfileError::None:
        return "ok";
    case ProfileError::NotBuilt:
        return "built without Hunspell";
    case ProfileError::BadRoot:
        return "project root is not a directory";
    case ProfileError::MissingFile:
        return "required file is missing or unsafe";
    case ProfileError::SizeOrHash:
        return "size or SHA-256 mismatch";
    case ProfileError::ManifestMismatch:
        return "manifest mismatch";
    case ProfileError::NoticeMismatch:
        return "notice mismatch";
    case ProfileError::NotUtf8:
        return "dictionary file is not strict UTF-8";
    case ProfileError::ControlMismatch:
        return "control aff is not the PFX/SFX removal of the aff";
    case ProfileError::MetadataMismatch:
        return "artifact metadata mismatch";
    case ProfileError::ModificationsMismatch:
        return "modification notice mismatch";
    case ProfileError::FormatMismatch:
        return "model or stem format mismatch";
    case ProfileError::LibraryMismatch:
        return "Hunspell library mismatch";
    case ProfileError::EngineFailed:
        return "Hunspell engine failed";
    case ProfileError::ChangedDuringLoad:
        return "files changed during load";
    case ProfileError::WrongThread:
        return "provider used outside its owner thread";
    }
    return "unknown";
}

const ProfilePins &productionPins()
{
    static const ProfilePins pins = makeProductionPins();
    return pins;
}

QString sha256Hex(const QByteArray &data)
{
    return QString::fromLatin1(QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex());
}

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

bool isStrictUtf8(const QByteArray &bytes)
{
    QStringDecoder decoder(QStringDecoder::Utf8, QStringDecoder::Flag::Stateless);
    const QString decoded = decoder(bytes);
    static_cast<void>(decoded);
    return !decoder.hasError();
}

bool isSafeRelativePath(const QString &relative, const QString &prefix)
{
    if (prefix.isEmpty() || !prefix.endsWith(QLatin1Char('/')) || !relative.startsWith(prefix)
        || relative.size() <= prefix.size() || relative.size() > kMaxPathChars)
        return false;
    const QStringList segments = relative.split(QLatin1Char('/'));
    for (const QString &segment : segments) {
        if (segment.isEmpty() || segment == QLatin1String(".") || segment == QLatin1String(".."))
            return false;
        for (const QChar ch : segment) {
            if (!isSafeSegmentChar(ch))
                return false;
        }
    }
    return true;
}

QString resolveContainedFile(const QString &canonicalRoot, const QString &relative)
{
    if (canonicalRoot.isEmpty() || !canonicalRoot.startsWith(QLatin1Char('/')))
        return {};
    const QFileInfo rootInfo(canonicalRoot);
    if (!rootInfo.isDir() || rootInfo.canonicalFilePath() != canonicalRoot)
        return {};
    const qsizetype slash = relative.indexOf(QLatin1Char('/'));
    if (slash <= 0)
        return {};
    const QString top = relative.left(slash);
    if (top != QLatin1String("data") && top != QLatin1String("build"))
        return {};
    if (!isSafeRelativePath(relative, top + QLatin1Char('/')))
        return {};
    const QString topPath = canonicalRoot + QLatin1Char('/') + top;
    const QFileInfo topInfo(topPath);
    if (topInfo.isSymLink() || !topInfo.isDir() || topInfo.canonicalFilePath() != topPath)
        return {};
    const QString joined = canonicalRoot + QLatin1Char('/') + relative;
    const QFileInfo info(joined);
    if (info.isSymLink() || !info.exists() || !info.isFile() || info.canonicalFilePath() != joined)
        return {};
    if (!joined.startsWith(topPath + QLatin1Char('/')))
        return {};
    return joined;
}

bool readBounded(const QString &path, qint64 limit, QByteArray &out)
{
    out.clear();
    const QFileInfo info(path);
    if (!info.exists() || !info.isFile() || info.size() > limit)
        return false;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return false;
    QByteArray bytes = file.read(limit + 1);
    if (file.error() != QFileDevice::NoError || qint64(bytes.size()) > limit || qint64(bytes.size()) != info.size())
        return false;
    out = std::move(bytes);
    return true;
}

bool removePfxSfxRows(const QByteArray &source, QByteArray &out)
{
    out.clear();
    if (qint64(source.size()) > kMaxAffBytes || source.isEmpty() || source.contains('\0'))
        return false;
    static const QByteArray bom("\xEF\xBB\xBF");
    auto spaceOrTab = [](char c) { return c == ' ' || c == '\t'; };
    QByteArray result;
    result.reserve(source.size());
    qint64 removed = 0;
    qsizetype start = 0;
    const qsizetype n = source.size();
    while (start < n) {
        const qsizetype newline = source.indexOf('\n', start);
        const qsizetype end = newline < 0 ? n : newline + 1;
        const QByteArrayView row(source.constData() + start, end - start);
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
        while (b < e && spaceOrTab(row.at(b)))
            ++b;
        qsizetype f = b;
        while (f < e && !spaceOrTab(row.at(f)))
            ++f;
        const QByteArrayView field = row.sliced(b, f - b);
        const bool pfx = field == QByteArrayView("PFX");
        const bool sfx = field == QByteArrayView("SFX");
        if (!pfx && !sfx && (field.startsWith(QByteArrayView("PFX")) || field.startsWith(QByteArrayView("SFX"))))
            return false;
        if ((pfx || sfx) && bomRow)
            return false;
        if (pfx || sfx)
            ++removed;
        else
            result.append(row);
        start = end;
    }
    if (removed == 0)
        return false;
    out = std::move(result);
    return true;
}

bool isQueryableToken(QStringView token)
{
    if (token.size() < 1 || token.size() > 32)
        return false;
    for (qsizetype i = 0; i < token.size(); ++i) {
        const QChar ch = token.at(i);
        if (ch.category() == QChar::Other_Control)
            return false;
        if (ch.isHighSurrogate()) {
            if (i + 1 >= token.size() || !token.at(i + 1).isLowSurrogate())
                return false;
            ++i;
        } else if (ch.isLowSurrogate()) {
            return false;
        }
    }
    return true;
}

bool verifyArtifactMetadata(const QByteArray &bytes, const ProfilePins &pins)
{
    if (qint64(bytes.size()) > kMaxMetadataBytes)
        return false;
    QJsonObject root;
    if (!parseObject(bytes, root))
        return false;
    const QJsonValue artifactsValue = root.value(key("artifacts"));
    if (!artifactsValue.isObject())
        return false;
    const QJsonObject artifacts = artifactsValue.toObject();
    if (!pinnedObjectMatches(artifacts.value(key("model")), pins.model, pins.artifactDir)
        || !pinnedObjectMatches(artifacts.value(key("exactFullLexicons")), pins.stem, pins.artifactDir))
        return false;
    if (!stringEquals(root, "protocolSha256", pins.protocol002.sha256)
        || !stringEquals(root, "trainingManifestSha256", pins.trainingManifest.sha256))
        return false;
    const QJsonValue trainingValue = root.value(key("trainingSources"));
    if (!trainingValue.isObject())
        return false;
    const QJsonObject training = trainingValue.toObject();
    for (std::size_t li = 0; li < pins.languages.size(); ++li) {
        const LanguagePins &l = pins.languages[li];
        const QJsonValue value = training.value(key(languageCode(li)));
        if (!value.isObject())
            return false;
        const QJsonObject o = value.toObject();
        if (!isLowerHex64(l.dic.sha256) || !isLowerHex64(l.notice.sha256) || l.notice.path.isEmpty()
            || !stringEquals(o, "sha256", l.dic.sha256) || !stringEquals(o, "notice", l.notice.path)
            || !stringEquals(o, "noticeSha256", l.notice.sha256))
            return false;
    }
    const QJsonValue scope = root.value(key("scope"));
    return scope.isString() && scope.toString().contains(QLatin1String("release_approved=false"))
        && scope.toString().contains(QLatin1String("not approved for distribution"));
}

bool verifyMorphManifest(const QByteArray &bytes, const ProfilePins &pins)
{
    if (qint64(bytes.size()) > kMaxManifestBytes || sha256Hex(bytes) != pins.morphManifest.sha256)
        return false;
    QJsonObject root;
    if (!parseObject(bytes, root))
        return false;
    if (!integerEquals(root, "schema_version", 1) || !stringEquals(root, "status", latin("approved-local-morphology"))
        || !stringEquals(root, "scope", latin("local-experiment-only")) || !releaseForbidden(root)
        || !stringEquals(root, "commit", pins.dictionaryCommit))
        return false;
    const QJsonValue hunspellValue = root.value(key("hunspell"));
    if (!hunspellValue.isObject())
        return false;
    const QJsonObject h = hunspellValue.toObject();
    if (!stringEquals(h, "version", pins.hunspellVersion)
        || !filePinEquals(h, "library_path", "library_sha256", "library_bytes", pins.library)
        || !noticeObjectEquals(h, pins.libraryNotice))
        return false;
    const QJsonValue additionalValue = h.value(key("additional_notices"));
    if (!additionalValue.isArray())
        return false;
    const QJsonArray additional = additionalValue.toArray();
    if (additional.size() != qsizetype(pins.additionalLibraryNotices.size()))
        return false;
    for (qsizetype i = 0; i < additional.size(); ++i) {
        if (!additional.at(i).isObject()
            || !noticeObjectEquals(additional.at(i).toObject(), pins.additionalLibraryNotices[std::size_t(i)]))
            return false;
    }
    const QJsonValue languagesValue = root.value(key("languages"));
    if (!languagesValue.isArray())
        return false;
    const QJsonArray languages = languagesValue.toArray();
    if (languages.size() != 2)
        return false;
    std::array<bool, 2> seen{{false, false}};
    for (const QJsonValue &value : languages) {
        if (!value.isObject())
            return false;
        const QJsonObject o = value.toObject();
        const int index = languageIndex(o);
        if (index < 0 || seen[std::size_t(index)])
            return false;
        seen[std::size_t(index)] = true;
        const LanguagePins &l = pins.languages[std::size_t(index)];
        if (!filePinEquals(o, "dic_path", "dic_sha256", "dic_bytes", l.dic)
            || !filePinEquals(o, "aff_path", "aff_sha256", "aff_bytes", l.aff)
            || !filePinEquals(o, "control_aff_path", "control_aff_sha256", "control_aff_bytes", l.controlAff)
            || !noticeObjectEquals(o, l.notice) || l.aff.sha256 == l.controlAff.sha256)
            return false;
    }
    return true;
}

bool verifyTrainingManifest(const QByteArray &bytes, const ProfilePins &pins)
{
    if (qint64(bytes.size()) > kMaxManifestBytes || sha256Hex(bytes) != pins.trainingManifest.sha256)
        return false;
    QJsonObject root;
    if (!parseObject(bytes, root))
        return false;
    if (!integerEquals(root, "schema_version", 1) || !stringEquals(root, "status", latin("approved-local-experiment"))
        || !stringEquals(root, "scope", latin("local-experiment-only")) || !releaseForbidden(root)
        || !stringEquals(root, "commit", pins.dictionaryCommit))
        return false;
    const QJsonValue sourcesValue = root.value(key("training_sources"));
    if (!sourcesValue.isArray())
        return false;
    const QJsonArray sources = sourcesValue.toArray();
    if (sources.size() != 2)
        return false;
    std::array<bool, 2> seen{{false, false}};
    for (const QJsonValue &value : sources) {
        if (!value.isObject())
            return false;
        const QJsonObject o = value.toObject();
        const int index = languageIndex(o);
        if (index < 0 || seen[std::size_t(index)])
            return false;
        seen[std::size_t(index)] = true;
        const LanguagePins &l = pins.languages[std::size_t(index)];
        if (!stringEquals(o, "sha256", l.dic.sha256) || !integerEquals(o, "bytes", l.dic.bytes)
            || !stringEquals(o, "cache_path", l.dic.path) || !noticeObjectEquals(o, l.notice))
            return false;
    }
    return true;
}

bool verifyProtocol002(const QByteArray &bytes, const ProfilePins &pins)
{
    if (qint64(bytes.size()) > kMaxManifestBytes || sha256Hex(bytes) != pins.protocol002.sha256)
        return false;
    QJsonObject root;
    if (!parseObject(bytes, root))
        return false;
    if (!integerEquals(root, "schema_version", 1) || !integerEquals(root, "short_token_minimum", kMinDecisionKeys))
        return false;
    const QJsonValue thresholdsValue = root.value(key("thresholds"));
    if (!thresholdsValue.isObject())
        return false;
    const QJsonValue twoScoreValue = thresholdsValue.toObject().value(key("twoScore"));
    if (!twoScoreValue.isObject())
        return false;
    const QJsonObject t = twoScoreValue.toObject();
    // Must equal routing::frozenTwoScore() (checked again by the provider).
    return numberEquals(t, "gap", 2.5) && numberEquals(t, "minAlternative", -13.0)
        && numberEquals(t, "maxObserved", -9.0);
}

ProfileError verifyProfileFiles(const QString &projectRoot, const ProfilePins &pins, VerifiedProfile &out)
{
    out = VerifiedProfile();
    const QFileInfo rootInfo(projectRoot);
    if (projectRoot.isEmpty() || !rootInfo.exists() || !rootInfo.isDir())
        return ProfileError::BadRoot;
    const QString root = rootInfo.canonicalFilePath();
    if (root.isEmpty())
        return ProfileError::BadRoot;

    // 1. Public manifests.
    {
        QByteArray bytes;
        if (const ProfileError e = readPinned(root, pins.morphManifest, kMaxManifestBytes, bytes);
            e != ProfileError::None)
            return e == ProfileError::SizeOrHash ? ProfileError::ManifestMismatch : e;
        if (!verifyMorphManifest(bytes, pins))
            return ProfileError::ManifestMismatch;
        if (const ProfileError e = readPinned(root, pins.trainingManifest, kMaxManifestBytes, bytes);
            e != ProfileError::None)
            return e == ProfileError::SizeOrHash ? ProfileError::ManifestMismatch : e;
        if (!verifyTrainingManifest(bytes, pins))
            return ProfileError::ManifestMismatch;
        if (const ProfileError e = readPinned(root, pins.protocol002, kMaxManifestBytes, bytes);
            e != ProfileError::None)
            return e == ProfileError::SizeOrHash ? ProfileError::ManifestMismatch : e;
        if (!verifyProtocol002(bytes, pins))
            return ProfileError::ManifestMismatch;
    }

    // 2. Notices.
    {
        std::vector<NoticePin> notices{pins.libraryNotice};
        notices.insert(notices.end(), pins.additionalLibraryNotices.begin(), pins.additionalLibraryNotices.end());
        for (const LanguagePins &l : pins.languages)
            notices.push_back(l.notice);
        for (const NoticePin &n : notices) {
            if (const ProfileError e = verifyNotice(root, n); e != ProfileError::None)
                return e;
        }
    }

    // 3. Morphology files and the control derivation.
    for (std::size_t li = 0; li < pins.languages.size(); ++li) {
        const LanguagePins &l = pins.languages[li];
        LanguageFiles &files = out.files[li];
        QByteArray dic;
        if (const ProfileError e = readPinned(root, l.dic, kMaxDicBytes, dic); e != ProfileError::None)
            return e;
        if (!isStrictUtf8(dic))
            return ProfileError::NotUtf8;
        dic.clear();
        dic.squeeze();
        QByteArray aff;
        QByteArray control;
        if (const ProfileError e = readPinned(root, l.aff, kMaxAffBytes, aff); e != ProfileError::None)
            return e;
        if (const ProfileError e = readPinned(root, l.controlAff, kMaxAffBytes, control); e != ProfileError::None)
            return e;
        if (!isStrictUtf8(aff) || !isStrictUtf8(control))
            return ProfileError::NotUtf8;
        QByteArray derived;
        if (!removePfxSfxRows(aff, derived) || derived != control)
            return ProfileError::ControlMismatch;
        files.dic = resolveContainedFile(root, l.dic.path);
        files.aff = resolveContainedFile(root, l.aff.path);
        files.controlAff = resolveContainedFile(root, l.controlAff.path);
        if (files.dic.isEmpty() || files.aff.isEmpty() || files.controlAff.isEmpty())
            return ProfileError::ChangedDuringLoad;
    }

    // 4. Artifact metadata, notice copies and the modification notice.
    {
        const QString metadataPath = resolveContainedFile(root, pins.artifactMetadataPath);
        if (metadataPath.isEmpty())
            return ProfileError::MissingFile;
        QByteArray metadata;
        if (!readBounded(metadataPath, kMaxMetadataBytes, metadata) || !verifyArtifactMetadata(metadata, pins))
            return ProfileError::MetadataMismatch;
        for (const LanguagePins &l : pins.languages) {
            const QString copy = resolveContainedFile(root, l.artifactNoticePath);
            if (copy.isEmpty())
                return ProfileError::MissingFile;
            QByteArray notice;
            if (!readBounded(copy, kMaxNoticeBytes, notice) || sha256Hex(notice) != l.notice.sha256)
                return ProfileError::NoticeMismatch;
        }
        const QString modificationsPath = resolveContainedFile(root, pins.modificationsPath);
        if (modificationsPath.isEmpty())
            return ProfileError::MissingFile;
        QByteArray modifications;
        if (!readBounded(modificationsPath, kMaxModificationsBytes, modifications) || modifications.isEmpty()
            || !isStrictUtf8(modifications) || !modifications.contains("release_approved=false"))
            return ProfileError::ModificationsMismatch;
    }

    // 5. Model and stem pair: pinned bytes and exact round trip.
    {
        QByteArray modelBytes;
        if (const ProfileError e = readPinned(root, pins.model, kMaxModelBytes, modelBytes); e != ProfileError::None)
            return e;
        const std::optional<NgramModel> model = NgramModel::deserialize(modelBytes);
        if (!model || !model->isValid() || model->serialize() != modelBytes)
            return ProfileError::FormatMismatch;
        QByteArray stemBytes;
        if (const ProfileError e = readPinned(root, pins.stem, kMaxStemBytes, stemBytes); e != ProfileError::None)
            return e;
        std::optional<std::array<benchmark::FrontCodedLexicon, 2>> stem = benchmark::deserializeLexiconPair(stemBytes);
        if (!stem || benchmark::serializeLexiconPair(stem->at(0), stem->at(1)) != stemBytes)
            return ProfileError::FormatMismatch;
        out.model = *model;
        out.stem = std::move(*stem);
    }
    return ProfileError::None;
}

ProfileError recheckMorphologyFiles(const QString &projectRoot, const ProfilePins &pins)
{
    const QString root = QFileInfo(projectRoot).canonicalFilePath();
    if (root.isEmpty())
        return ProfileError::ChangedDuringLoad;
    for (const LanguagePins &l : pins.languages) {
        for (const FilePin *pin : {&l.dic, &l.aff, &l.controlAff}) {
            QByteArray bytes;
            const qint64 limit = pin == &l.dic ? kMaxDicBytes : kMaxAffBytes;
            if (readPinned(root, *pin, limit, bytes) != ProfileError::None)
                return ProfileError::ChangedDuringLoad;
        }
    }
    return ProfileError::None;
}

} // namespace rasklad::profile
