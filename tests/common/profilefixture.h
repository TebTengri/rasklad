#pragma once

// Synthetic local-profile tree for SYS-239 tests. Builds tiny artificial
// manifests, notices, .aff/.dic, control .aff, a trained toy model and stem
// pair inside a temporary directory and returns pins for exactly those bytes.
// No generated research data, corpus or real dictionaries are read.

#include "frontcodedlexicon.h"
#include "ngramdetector.h"
#include "profileverify.h"

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>
#include <QTemporaryDir>

#include <array>
#include <memory>
#include <vector>

namespace rasklad::testfixture {

inline bool writeBytes(const QString &root, const QString &relative, const QByteArray &bytes)
{
    const QString path = root + QLatin1Char('/') + relative;
    if (!QDir().mkpath(QFileInfo(path).absolutePath()))
        return false;
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    return file.write(bytes) == bytes.size();
}

inline profile::FilePin pinOf(const QString &relative, const QByteArray &bytes)
{
    return {relative, profile::sha256Hex(bytes), qint64(bytes.size())};
}

inline QByteArray json(const QJsonObject &o)
{
    return QJsonDocument(o).toJson(QJsonDocument::Indented);
}

inline KeySequence keysOf(const QString &usText)
{
    return canonicalizeToken(usText, SourceLayout::Us).keys;
}

struct ProfileFixture {
    std::unique_ptr<QTemporaryDir> dir;
    QString root;
    profile::ProfilePins pins;

    // en.dic accepts "hello", "world" (+ "hellos" via SFX); ru.dic accepts
    // "привет", "мир". Control .aff files have no PFX/SFX rows.
    bool build()
    {
        dir = std::make_unique<QTemporaryDir>();
        if (!dir->isValid())
            return false;
        root = QFileInfo(dir->path()).canonicalFilePath();
        profile::ProfilePins &p = pins;
        p = profile::ProfilePins();
        p.hunspellVersion = QStringLiteral("1.7.5");
        p.library = {QStringLiteral("/usr/lib/libhunspell-fixture.so"), QString(64, QLatin1Char('0')), 1};
        p.dictionaryCommit = QStringLiteral("0123456789abcdef0123456789abcdef01234567");
        p.artifactDir = QStringLiteral("build/artifacts/");

        auto notice = [&](const char *relative, const char *text, profile::NoticePin &out) {
            const QString rel = QString::fromLatin1(relative);
            const QByteArray bytes(text);
            out = {rel, profile::sha256Hex(bytes)};
            return writeBytes(root, rel, bytes);
        };
        profile::NoticePin mpl;
        profile::NoticePin myspell;
        if (!notice("data/notices/LIB-REVIEW.md", "fixture library review\n", p.libraryNotice)
            || !notice("data/notices/MPL.txt", "fixture MPL notice\n", mpl)
            || !notice("data/notices/MYSPELL.txt", "fixture MySpell notice\n", myspell)
            || !notice("data/notices/en.README.txt", "fixture en notice\n", p.languages[0].notice)
            || !notice("data/notices/ru.README.txt", "fixture ru notice\n", p.languages[1].notice))
            return false;
        p.additionalLibraryNotices = {mpl, myspell};

        const std::array<QByteArray, 2> aff{{
            QByteArray("SET UTF-8\nTRY esianrtolcdugmphbyfvkwz\nSFX S Y 1\nSFX S 0 s .\n"),
            QByteArray("SET UTF-8\nPFX P Y 1\nPFX P 0 \xd0\xbf\xd0\xbe .\n"),
        }};
        const std::array<QByteArray, 2> dic{{
            QByteArray("2\nhello/S\nworld\n"),
            QString::fromUtf8("2\nпривет\nмир\n").toUtf8(),
        }};
        const std::array<const char *, 2> code{{"en", "ru"}};
        for (std::size_t li = 0; li < 2; ++li) {
            QByteArray control;
            if (!profile::removePfxSfxRows(aff[li], control))
                return false;
            const QString c = QString::fromLatin1(code[li]);
            profile::LanguagePins &l = p.languages[li];
            l.dic = pinOf(QStringLiteral("build/dicts/") + c + QStringLiteral(".dic"), dic[li]);
            l.aff = pinOf(QStringLiteral("build/morph/") + c + QStringLiteral(".aff"), aff[li]);
            l.controlAff = pinOf(QStringLiteral("build/morph/control/") + c + QStringLiteral(".aff"), control);
            l.artifactNoticePath = QStringLiteral("build/artifacts/") + c + QStringLiteral(".NOTICE.txt");
            QFile source(root + QLatin1Char('/') + l.notice.path);
            if (!source.open(QIODevice::ReadOnly))
                return false;
            const QByteArray noticeBytes = source.readAll();
            if (!writeBytes(root, l.dic.path, dic[li]) || !writeBytes(root, l.aff.path, aff[li])
                || !writeBytes(root, l.controlAff.path, control)
                || !writeBytes(root, l.artifactNoticePath, noticeBytes))
                return false;
        }

        // Toy model and stem pair (canonical keys of a few words).
        const std::vector<KeySequence> en{keysOf(QStringLiteral("hello")), keysOf(QStringLiteral("world")),
                                          keysOf(QStringLiteral("water"))};
        const std::vector<KeySequence> ru{keysOf(QStringLiteral("ghbdtn")), keysOf(QStringLiteral("vbh")),
                                          keysOf(QStringLiteral("djlf"))};
        const NgramModel model = NgramModel::train(en, ru);
        const QByteArray modelBytes = model.serialize();
        const auto enLex = benchmark::FrontCodedLexicon::build(en);
        const auto ruLex = benchmark::FrontCodedLexicon::build(ru);
        if (modelBytes.isEmpty() || !enLex || !ruLex)
            return false;
        const QByteArray stemBytes = benchmark::serializeLexiconPair(*enLex, *ruLex);
        p.model = pinOf(QStringLiteral("build/artifacts/model.rskn"), modelBytes);
        p.stem = pinOf(QStringLiteral("build/artifacts/stem.rskp"), stemBytes);
        if (!writeBytes(root, p.model.path, modelBytes) || !writeBytes(root, p.stem.path, stemBytes))
            return false;

        // Protocol 002 subset.
        QJsonObject twoScore{{QStringLiteral("gap"), 2.5}, {QStringLiteral("minAlternative"), -13},
                             {QStringLiteral("maxObserved"), -9}};
        QJsonObject protocol{{QStringLiteral("schema_version"), 1},
                             {QStringLiteral("short_token_minimum"), 4},
                             {QStringLiteral("thresholds"), QJsonObject{{QStringLiteral("twoScore"), twoScore}}}};
        if (!writeManifest(QStringLiteral("data/protocol-002.json"), json(protocol), p.protocol002))
            return false;
        if (!writeTraining() || !writeMorph() || !writeMetadata())
            return false;
        p.modificationsPath = QStringLiteral("build/artifacts/MODIFICATIONS.txt");
        return writeBytes(root, p.modificationsPath, QByteArray("fixture derivative; release_approved=false\n"));
    }

    bool writeManifest(const QString &relative, const QByteArray &bytes, profile::FilePin &pin)
    {
        pin = {relative, profile::sha256Hex(bytes), 0};
        return writeBytes(root, relative, bytes);
    }

    QJsonObject languageEntry(std::size_t li) const
    {
        const profile::LanguagePins &l = pins.languages[li];
        return QJsonObject{
            {QStringLiteral("language"), li == 0 ? QStringLiteral("en") : QStringLiteral("ru")},
            {QStringLiteral("dic_path"), l.dic.path},
            {QStringLiteral("dic_sha256"), l.dic.sha256},
            {QStringLiteral("dic_bytes"), double(l.dic.bytes)},
            {QStringLiteral("aff_path"), l.aff.path},
            {QStringLiteral("aff_sha256"), l.aff.sha256},
            {QStringLiteral("aff_bytes"), double(l.aff.bytes)},
            {QStringLiteral("control_aff_path"), l.controlAff.path},
            {QStringLiteral("control_aff_sha256"), l.controlAff.sha256},
            {QStringLiteral("control_aff_bytes"), double(l.controlAff.bytes)},
            {QStringLiteral("notice"), l.notice.path},
            {QStringLiteral("notice_sha256"), l.notice.sha256},
        };
    }

    QJsonObject morphObject() const
    {
        const profile::ProfilePins &p = pins;
        QJsonArray additional;
        for (const profile::NoticePin &n : p.additionalLibraryNotices)
            additional.append(QJsonObject{{QStringLiteral("notice"), n.path}, {QStringLiteral("notice_sha256"), n.sha256}});
        QJsonObject hunspell{{QStringLiteral("version"), p.hunspellVersion},
                             {QStringLiteral("library_path"), p.library.path},
                             {QStringLiteral("library_sha256"), p.library.sha256},
                             {QStringLiteral("library_bytes"), double(p.library.bytes)},
                             {QStringLiteral("notice"), p.libraryNotice.path},
                             {QStringLiteral("notice_sha256"), p.libraryNotice.sha256},
                             {QStringLiteral("additional_notices"), additional}};
        return QJsonObject{{QStringLiteral("schema_version"), 1},
                           {QStringLiteral("status"), QStringLiteral("approved-local-morphology")},
                           {QStringLiteral("scope"), QStringLiteral("local-experiment-only")},
                           {QStringLiteral("release_approved"), false},
                           {QStringLiteral("commit"), p.dictionaryCommit},
                           {QStringLiteral("hunspell"), hunspell},
                           {QStringLiteral("languages"), QJsonArray{languageEntry(0), languageEntry(1)}}};
    }

    bool writeMorph() { return writeManifest(QStringLiteral("data/morphology.json"), json(morphObject()), pins.morphManifest); }

    QJsonObject trainingObject() const
    {
        QJsonArray sources;
        for (std::size_t li = 0; li < 2; ++li) {
            const profile::LanguagePins &l = pins.languages[li];
            sources.append(QJsonObject{{QStringLiteral("language"), li == 0 ? QStringLiteral("en") : QStringLiteral("ru")},
                                       {QStringLiteral("sha256"), l.dic.sha256},
                                       {QStringLiteral("bytes"), double(l.dic.bytes)},
                                       {QStringLiteral("cache_path"), l.dic.path},
                                       {QStringLiteral("notice"), l.notice.path},
                                       {QStringLiteral("notice_sha256"), l.notice.sha256}});
        }
        return QJsonObject{{QStringLiteral("schema_version"), 1},
                           {QStringLiteral("status"), QStringLiteral("approved-local-experiment")},
                           {QStringLiteral("scope"), QStringLiteral("local-experiment-only")},
                           {QStringLiteral("release_approved"), false},
                           {QStringLiteral("commit"), pins.dictionaryCommit},
                           {QStringLiteral("training_sources"), sources}};
    }

    bool writeTraining()
    {
        return writeManifest(QStringLiteral("data/training.json"), json(trainingObject()), pins.trainingManifest);
    }

    QJsonObject metadataObject() const
    {
        const profile::ProfilePins &p = pins;
        auto artifact = [&p](const profile::FilePin &pin) {
            return QJsonObject{{QStringLiteral("file"), pin.path.mid(p.artifactDir.size())},
                               {QStringLiteral("bytes"), double(pin.bytes)},
                               {QStringLiteral("sha256"), pin.sha256}};
        };
        QJsonObject training;
        for (std::size_t li = 0; li < 2; ++li) {
            const profile::LanguagePins &l = p.languages[li];
            training.insert(li == 0 ? QStringLiteral("en") : QStringLiteral("ru"),
                            QJsonObject{{QStringLiteral("sha256"), l.dic.sha256},
                                        {QStringLiteral("notice"), l.notice.path},
                                        {QStringLiteral("noticeSha256"), l.notice.sha256}});
        }
        return QJsonObject{
            {QStringLiteral("artifacts"),
             QJsonObject{{QStringLiteral("model"), artifact(p.model)},
                         {QStringLiteral("exactFullLexicons"), artifact(p.stem)}}},
            {QStringLiteral("protocolSha256"), p.protocol002.sha256},
            {QStringLiteral("trainingManifestSha256"), p.trainingManifest.sha256},
            {QStringLiteral("trainingSources"), training},
            {QStringLiteral("scope"),
             QStringLiteral("fixture; release_approved=false; not approved for distribution")}};
    }

    bool writeMetadata()
    {
        pins.artifactMetadataPath = QStringLiteral("build/artifacts/metadata.json");
        return writeBytes(root, pins.artifactMetadataPath, json(metadataObject()));
    }

    // Rewrites a pinned file and updates its pin (size and SHA-256).
    bool replacePinned(profile::FilePin &pin, const QByteArray &bytes)
    {
        pin = pinOf(pin.path, bytes);
        return writeBytes(root, pin.path, bytes);
    }
};

} // namespace rasklad::testfixture
