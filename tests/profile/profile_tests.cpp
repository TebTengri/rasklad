// SYS-239: production local-profile code against the research helpers, and
// the real provider loaded from a synthetic fixture (system Hunspell only).

#include "decisionrouter.h"
#include "domaindecisions.h"
#include "hunspellengine.h"
#include "localprofile.h"
#include "morphologydata.h"
#include "profilefixture.h"
#include "profileverify.h"
#include "spellengine.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>
#include <QThread>

#include <memory>
#include <optional>
#include <vector>

using namespace rasklad;
using rasklad::profile::ProfileError;

namespace {

// SYS-256: the research verifier knows the experiment artifacts (bare RSKN/RSKP);
// the product pins the same data in the RSKX notice container. The parity is about
// the checking logic, so compare both against the experiment values.
profile::ProfilePins researchPins()
{
    profile::ProfilePins p = profile::productionPins();
    p.model.sha256 = QStringLiteral("e92e549e5f0853e4cd78d6d997653098b78550eb9911de11421434e26e3985d8");
    p.model.bytes = 88290;
    p.stem.sha256 = QStringLiteral("3bdd740d4b39bf2b5fe361e0d3c670187563ec59f41913db3525340a30887871");
    p.stem.bytes = 1200379;
    // The research verifier pins the frozen training passport; after a dictionary
    // update the product's pin moves, the research one does not.
    p.trainingManifest.sha256 = QString::fromLatin1(domain::kTrainingManifestSha256);
    return p;
}

QJsonObject productionMetadata()
{
    const profile::ProfilePins p = researchPins();
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
    return QJsonObject{{QStringLiteral("artifacts"),
                        QJsonObject{{QStringLiteral("model"), artifact(p.model)},
                                    {QStringLiteral("exactFullLexicons"), artifact(p.stem)}}},
                       {QStringLiteral("protocolSha256"), p.protocol002.sha256},
                       {QStringLiteral("trainingManifestSha256"), p.trainingManifest.sha256},
                       {QStringLiteral("trainingSources"), training},
                       {QStringLiteral("scope"),
                        QString::fromLatin1(profile::kReleaseScope)}};
}

QByteArray bytesOf(const QJsonObject &o)
{
    return QJsonDocument(o).toJson(QJsonDocument::Compact);
}

std::optional<routing::TokenEvidence> query(const profile::LocalEvidenceProvider &provider, const QString &token,
                                             SourceLayout layout)
{
    routing::EvidenceQuery q;
    q.token = token;
    q.layout = layout;
    q.keys = canonicalizeToken(token, layout).keys;
    return provider.evidence(q);
}

// Pins the library that is actually loaded into this test process.
bool pinLoadedLibrary(testfixture::ProfileFixture &f)
{
    const QString path = profile::loadedHunspellLibraryPath();
    if (!path.startsWith(QLatin1String("/usr/lib/")))
        return false;
    QByteArray bytes;
    if (!profile::readBounded(path, profile::kMaxLibraryBytes, bytes))
        return false;
    f.pins.library = {path, profile::sha256Hex(bytes), qint64(bytes.size())};
    f.pins.hunspellVersion = profile::runtimeHunspellVersion();
    return f.writeMorph();
}

} // namespace

class ProfileTests : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void removePfxSfxParity()
    {
        const std::vector<QByteArray> inputs{
            QByteArray("SET UTF-8\nPFX A Y 1\nPFX A 0 re .\nSFX B Y 1\nSFX B 0 s .\n"),
            QByteArray("SET UTF-8\r\n  SFX A Y 1\r\nKEEP\r\n"),
            QByteArray("SFX\tA Y 1\nlast"),
            QByteArray("PFXA Y 1\n"),
            QByteArray("SFX1 Y\n"),
            QByteArray("\xEF\xBB\xBFPFX A Y 1\n"),
            QByteArray("\xEF\xBB\xBFSET UTF-8\nPFX A Y 1\n"),
            QByteArray("SET UTF-8\nTRY abc\n"),
            QByteArray(),
            QByteArray("PFX\0A Y\n", 8),
            QByteArray("# PFX comment\nPFX A Y 1\n"),
            QByteArray("PFX"),
        };
        for (std::size_t i = 0; i < inputs.size(); ++i) {
            const morphology::AffControlResult research = morphology::removePfxSfxRows(inputs[i]);
            QByteArray out;
            const bool ok = profile::removePfxSfxRows(inputs[i], out);
            const QByteArray where = QByteArray::number(qulonglong(i));
            QVERIFY2(ok == (research.error == morphology::AffControlError::None), where.constData());
            if (ok)
                QVERIFY2(out == research.bytes, where.constData());
        }
    }

    void noticeContainer()
    {
        using profile::unwrapNoticeContainer;
        using profile::wrapNoticeContainer;
        QByteArray notice, payload;
        const QByteArray file = wrapNoticeContainer(QString::fromUtf8("Лебедев\n").toUtf8(), "RSKN-payload");
        QVERIFY(unwrapNoticeContainer(file, notice, payload));
        QCOMPARE(notice, QString::fromUtf8("Лебедев\n").toUtf8());
        QCOMPARE(payload, QByteArray("RSKN-payload"));
        QVERIFY(!unwrapNoticeContainer(QByteArray("RSKN-payload"), notice, payload)); // unwrapped file
        // The writer refuses what the reader would reject.
        QVERIFY(wrapNoticeContainer(QByteArray(), "p").isEmpty());
        QVERIFY(wrapNoticeContainer("n", QByteArray()).isEmpty());
        QVERIFY(wrapNoticeContainer("\xff\xfe", "p").isEmpty());
        QVERIFY(wrapNoticeContainer(QByteArray("a\0b", 3), "p").isEmpty());
        QVERIFY(wrapNoticeContainer(QByteArray(profile::kMaxEmbeddedNoticeBytes + 1, 'n'), "p").isEmpty());
        QVERIFY(!wrapNoticeContainer(QByteArray(profile::kMaxEmbeddedNoticeBytes, 'n'), "p").isEmpty());
        // Hand-made containers the reader rejects.
        auto raw = [](const QByteArray &magic, quint32 n, const QByteArray &rest) {
            QByteArray b = magic;
            for (int i = 0; i < 4; ++i)
                b.append(char((n >> (8 * i)) & 0xff));
            return b + rest;
        };
        QVERIFY(!unwrapNoticeContainer(raw("RSKX", 0, "p"), notice, payload));         // empty notice
        QVERIFY(!unwrapNoticeContainer(raw("RSKX", 1, "n"), notice, payload));         // no payload
        QVERIFY(!unwrapNoticeContainer(raw("RSKX", 2, "\xff\xfep"), notice, payload)); // not UTF-8
        QVERIFY(!unwrapNoticeContainer(raw("RSKY", 1, "np"), notice, payload));        // another magic
        QVERIFY(!unwrapNoticeContainer(raw("RSKX", quint32(profile::kMaxEmbeddedNoticeBytes + 1),
                                           QByteArray(profile::kMaxEmbeddedNoticeBytes + 2, 'n')),
                                       notice, payload)); // too large
        QByteArray truncated = wrapNoticeContainer("notice", "p");
        truncated.chop(2); // the length now points past the end
        QVERIFY(!unwrapNoticeContainer(truncated, notice, payload));
    }

    void controlAffWithoutMarkRejected()
    {
        testfixture::ProfileFixture f;
        QVERIFY(f.build());
        profile::LanguagePins &l = f.pins.languages[1];
        QFile file(f.root + QLatin1Char('/') + l.controlAff.path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QByteArray marked = file.readAll();
        file.close();
        const QByteArray header(profile::kControlAffHeader);
        QVERIFY(marked.startsWith(header));
        const QByteArray unmarked = marked.mid(header.size()); // the pre-SYS-256 control file
        QVERIFY(testfixture::writeBytes(f.root, l.controlAff.path, unmarked));
        l.controlAff = testfixture::pinOf(l.controlAff.path, unmarked);
        QVERIFY(f.writeMorph());
        profile::VerifiedProfile verified;
        QCOMPARE(profile::verifyProfileFiles(f.root, f.pins, verified), ProfileError::ControlMismatch);
    }

    void bareModelRejected()
    {
        // The pre-SYS-256 format: an RSKN model without the notice container, even
        // with a matching pin, is refused (as by tests/profile_gate in PKGBUILD).
        testfixture::ProfileFixture f;
        QVERIFY(f.build());
        QFile file(f.root + QLatin1Char('/') + f.pins.model.path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        QByteArray notice, bare;
        QVERIFY(profile::unwrapNoticeContainer(file.readAll(), notice, bare));
        file.close();
        QVERIFY(testfixture::writeBytes(f.root, f.pins.model.path, bare));
        f.pins.model = testfixture::pinOf(f.pins.model.path, bare);
        QVERIFY(f.writeMetadata());
        profile::VerifiedProfile verified;
        QCOMPARE(profile::verifyProfileFiles(f.root, f.pins, verified), ProfileError::FormatMismatch);
    }

    void queryableParity()
    {
        const std::vector<QString> tokens{
            QStringLiteral("hello"),
            QString(),
            QString(32, QLatin1Char('a')),
            QString(33, QLatin1Char('a')),
            QStringLiteral("a\tb"),
            QString::fromUtf8("😀"),
            QString(QChar(0xD800)),
            QString(QChar(0xDC00)) + QStringLiteral("a"),
            QString::fromUtf8("привет"),
            QStringLiteral("a") + QChar(0x007F),
        };
        for (const QString &token : tokens)
            QCOMPARE(profile::isQueryableToken(token), morphology::isQueryableToken(token));
    }

    void artifactMetadataParity()
    {
        const profile::ProfilePins pins = researchPins();
        std::array<domain::ExpectedTrainingSource, 2> expected;
        for (std::size_t li = 0; li < 2; ++li)
            expected[li] = {pins.languages[li].dic.sha256, pins.languages[li].notice.path,
                            pins.languages[li].notice.sha256};

        std::vector<QByteArray> variants;
        const QJsonObject valid = productionMetadata();
        variants.push_back(bytesOf(valid));
        {
            QJsonObject o = valid;
            o.insert(QStringLiteral("scope"), QStringLiteral("release_approved=false"));
            variants.push_back(bytesOf(o));
        }
        {
            QJsonObject o = valid; // approved without naming the review
            o.insert(QStringLiteral("scope"), QStringLiteral("release_approved=true"));
            variants.push_back(bytesOf(o));
        }
        {
            QJsonObject o = valid; // contradicting statement appended
            o.insert(QStringLiteral("scope"), QString::fromLatin1(profile::kReleaseScope) + QStringLiteral("; release_approved=false"));
            variants.push_back(bytesOf(o));
        }
        {
            QJsonObject o = valid; // another review file
            o.insert(QStringLiteral("scope"), QString::fromLatin1(profile::kReleaseScope)
                                                  .replace(QString::fromLatin1(profile::kDistributionReview),
                                                           QString::fromLatin1(profile::kDistributionReview) + QStringLiteral(".bak")));
            variants.push_back(bytesOf(o));
        }
        {
            QJsonObject o = valid;
            QJsonObject artifacts = o.value(QStringLiteral("artifacts")).toObject();
            QJsonObject model = artifacts.value(QStringLiteral("model")).toObject();
            model.insert(QStringLiteral("bytes"), 88290.5);
            artifacts.insert(QStringLiteral("model"), model);
            o.insert(QStringLiteral("artifacts"), artifacts);
            variants.push_back(bytesOf(o));
            model.insert(QStringLiteral("bytes"), 88290);
            model.insert(QStringLiteral("file"), QStringLiteral("other.rskn"));
            artifacts.insert(QStringLiteral("model"), model);
            o.insert(QStringLiteral("artifacts"), artifacts);
            variants.push_back(bytesOf(o));
            model.insert(QStringLiteral("file"), QStringLiteral("detector-model.rskn"));
            model.insert(QStringLiteral("sha256"), QString(64, QLatin1Char('0')));
            artifacts.insert(QStringLiteral("model"), model);
            o.insert(QStringLiteral("artifacts"), artifacts);
            variants.push_back(bytesOf(o));
        }
        {
            QJsonObject o = valid;
            o.insert(QStringLiteral("protocolSha256"), QString(64, QLatin1Char('1')));
            variants.push_back(bytesOf(o));
        }
        {
            QJsonObject o = valid;
            QJsonObject training = o.value(QStringLiteral("trainingSources")).toObject();
            QJsonObject en = training.value(QStringLiteral("en")).toObject();
            en.insert(QStringLiteral("notice"), QStringLiteral("data/notices/other.txt"));
            training.insert(QStringLiteral("en"), en);
            o.insert(QStringLiteral("trainingSources"), training);
            variants.push_back(bytesOf(o));
        }
        {
            QJsonObject o = valid;
            o.remove(QStringLiteral("artifacts"));
            variants.push_back(bytesOf(o));
        }
        {
            QJsonObject o = valid;
            o.insert(QStringLiteral("extra"), QStringLiteral("ignored"));
            variants.push_back(bytesOf(o));
        }
        variants.push_back(QByteArray("[]"));
        variants.push_back(QByteArray("{"));
        variants.push_back(bytesOf(valid) + QByteArray(64 * 1024, ' '));

        // SYS-256: the two differ only in the scope statement. The product requires the
        // distribution approval; research artifacts keep their local-only scope. Every
        // other field is compared on the same bytes with the research scope put back.
        const QString approved = valid.value(QStringLiteral("scope")).toString();
        const QString researchScope =
            QStringLiteral("local experiment; release_approved=false; not approved for distribution");
        int accepted = 0;
        for (std::size_t i = 0; i < variants.size(); ++i) {
            const bool production = profile::verifyArtifactMetadata(variants[i], pins);
            const QJsonObject o = QJsonDocument::fromJson(variants[i]).object();
            if (o.value(QStringLiteral("scope")).toString() == approved && variants[i].size() < 64 * 1024) {
                QJsonObject r = o;
                r.insert(QStringLiteral("scope"), researchScope);
                const bool research = domain::verifyArtifactMetadata(bytesOf(r), expected)
                    == domain::ArtifactMetadataError::None;
                QVERIFY2(research == production, QByteArray::number(qulonglong(i)).constData());
            }
            accepted += production ? 1 : 0;
        }
        QCOMPARE(accepted, 2); // valid and valid+extra field
        QJsonObject research = valid;
        research.insert(QStringLiteral("scope"), researchScope);
        QVERIFY(!profile::verifyArtifactMetadata(bytesOf(research), pins)); // not approved: refused
        // Qt keeps the last duplicate member: an unapproved scope followed by the
        // approved one, in the object or nested, is refused (SYS-256).
        QByteArray compact = bytesOf(valid);
        QVERIFY(profile::verifyArtifactMetadata(compact, pins));
        QByteArray dup = compact;
        dup.insert(1, "\"scope\":\"release_approved=false; not approved for distribution\",");
        QVERIFY(QJsonDocument::fromJson(dup).isObject());
        QVERIFY(!profile::verifyArtifactMetadata(dup, pins));
        QByteArray escaped = compact;
        escaped.insert(1, "\"sc\\u006fpe\":\"x\",");
        QVERIFY(QJsonDocument::fromJson(escaped).isObject());
        QVERIFY(!profile::verifyArtifactMetadata(escaped, pins));
        QByteArray nested = compact;
        const qsizetype at = nested.indexOf("\"model\":{") + 9;
        nested.insert(at, "\"file\":\"other\",");
        QVERIFY(QJsonDocument::fromJson(nested).isObject());
        QVERIFY(!profile::verifyArtifactMetadata(nested, pins));
        QByteArray strings = compact; // braces and quotes inside strings are not structure
        strings.insert(1, "\"note\":\"{\\\"scope\\\":[}\",");
        QVERIFY(profile::verifyArtifactMetadata(strings, pins));
    }

    void spellEngineParity()
    {
        testfixture::ProfileFixture f;
        QVERIFY(f.build());
        profile::VerifiedProfile verified;
        QCOMPARE(profile::verifyProfileFiles(f.root, f.pins, verified), ProfileError::None);
        for (std::size_t li = 0; li < 2; ++li) {
            const profile::LanguageFiles &files = verified.files[li];
            auto production = profile::SpellEngine::create(files.aff, files.dic);
            morphology::HunspellEngine::Error error = morphology::HunspellEngine::Error::None;
            auto research = morphology::HunspellEngine::create(files.aff, files.dic, error);
            QVERIFY(production);
            QVERIFY(research);
            for (const QString &token :
                 {QStringLiteral("hello"), QStringLiteral("hellos"), QStringLiteral("Hello"), QStringLiteral("world"),
                  QString::fromUtf8("привет"), QString::fromUtf8("мир"), QString::fromUtf8("мирs"), QString(),
                  QString(33, QLatin1Char('a')), QStringLiteral("a\nb")})
                QCOMPARE(production->accepts(token), research->accepts(token));
        }
    }

    void providerEndToEndOnFixture()
    {
        testfixture::ProfileFixture f;
        QVERIFY(f.build());
        if (!pinLoadedLibrary(f))
            QSKIP("loaded Hunspell library is not under /usr/lib");
        ProfileError error = ProfileError::None;
        std::unique_ptr<profile::LocalEvidenceProvider> provider =
            profile::LocalEvidenceProvider::load(f.root, f.pins, error);
        QCOMPARE(error, ProfileError::None);
        QVERIFY(provider);

        std::optional<routing::TokenEvidence> e = query(*provider, QStringLiteral("hello"), SourceLayout::Us);
        QVERIFY(e);
        QVERIFY(e->full.observed);
        QVERIFY(e->control.observed);
        QVERIFY(e->stem.observed);

        // The suffix rule exists only in the full .aff.
        e = query(*provider, QStringLiteral("hellos"), SourceLayout::Us);
        QVERIFY(e);
        QVERIFY(e->full.observed);
        QVERIFY(!e->control.observed);

        e = query(*provider, QStringLiteral("ghbdtn"), SourceLayout::Us);
        QVERIFY(e);
        QVERIFY(!e->full.observed);
        QVERIFY(e->full.alternative);
        QVERIFY(e->control.alternative);
        QVERIFY(e->stem.alternative);
        QVERIFY(!e->stem.observed);

        e = query(*provider, QString::fromUtf8("руддщ"), SourceLayout::Ru);
        QVERIFY(e);
        QVERIFY(e->full.alternative);

        // Through the router: protected and unknown never reach the provider,
        // and the observed word is kept.
        routing::RouteRequest request;
        request.field = routing::FieldContext::Prose;
        request.layout = routing::InputLayout::KnownUs;
        const QString hello = QStringLiteral("hello");
        request.token = hello;
        QCOMPARE(routing::route(request, routing::RouteProfile::SafeConfirmed, provider.get()).action,
                 routing::RouteAction::Keep);
        const QString wrong = QStringLiteral("ghbdtn");
        request.token = wrong;
        const routing::RouteDecision hybrid =
            routing::route(request, routing::RouteProfile::RequestedHybrid, provider.get());
        QVERIFY(hybrid.action == routing::RouteAction::Correct || hybrid.action == routing::RouteAction::Suggest);
        QCOMPARE(hybrid.candidate, QString::fromUtf8("привет"));

        // Direct provider callers cannot pair unrelated text and keys or
        // allocate an oversized token through an otherwise valid key query.
        const QString oversized(65537, QLatin1Char('a'));
        routing::EvidenceQuery bad{QStringView(oversized), SourceLayout::Us,
                                  canonicalizeToken(QStringLiteral("hello"), SourceLayout::Us).keys};
        QVERIFY(!provider->evidence(bad));
        const QString helloText = QStringLiteral("hello");
        bad.token = QStringView(helloText);
        bad.keys = canonicalizeToken(QStringLiteral("world"), SourceLayout::Us).keys;
        QVERIFY(!provider->evidence(bad));

        // Single processing owner: another thread is refused.
        bool refused = false;
        std::unique_ptr<QThread> other(QThread::create([&provider, &refused]() {
            refused = !query(*provider, QStringLiteral("hello"), SourceLayout::Us).has_value();
        }));
        other->start();
        QVERIFY(other->wait(5000));
        QVERIFY(refused);
    }

    void providerRejectsMismatches()
    {
        testfixture::ProfileFixture f;
        QVERIFY(f.build());
        if (!pinLoadedLibrary(f))
            QSKIP("loaded Hunspell library is not under /usr/lib");
        ProfileError error = ProfileError::None;
        // SYS-249: a pacman update of Hunspell within the same minor series. The pins
        // and the manifest still record the evaluated build (another hash and patch
        // release); only the installed library differs, and the profile still loads.
        const profile::ProfilePins installed = f.pins;
        f.pins.library.sha256 = QString(64, QLatin1Char('0'));
        f.pins.hunspellVersion = installed.hunspellVersion.section(QLatin1Char('.'), 0, 1)
            + QStringLiteral(".0");
        QVERIFY(f.pins.hunspellVersion != installed.hunspellVersion || installed.hunspellVersion.endsWith(QLatin1String(".0")));
        QVERIFY(f.writeMorph());
        QVERIFY(profile::LocalEvidenceProvider::load(f.root, f.pins, error));
        QCOMPARE(error, ProfileError::None);
        f.pins = installed;
        QVERIFY(f.writeMorph());
        // Another minor series does not.
        profile::ProfilePins wrongLibrary = f.pins;
        wrongLibrary.hunspellVersion = QStringLiteral("1.6.2");
        QVERIFY(!profile::LocalEvidenceProvider::load(f.root, wrongLibrary, error));
        QCOMPARE(error, ProfileError::LibraryMismatch);
        wrongLibrary.hunspellVersion = QStringLiteral("1.7");
        QVERIFY(!profile::LocalEvidenceProvider::load(f.root, wrongLibrary, error));
        QCOMPARE(error, ProfileError::LibraryMismatch);

        QVERIFY(QFile::remove(f.root + QLatin1Char('/') + f.pins.stem.path));
        QVERIFY(!profile::LocalEvidenceProvider::load(f.root, f.pins, error));
        QCOMPARE(error, ProfileError::MissingFile);
    }
};

QTEST_GUILESS_MAIN(ProfileTests)
#include "profile_tests.moc"
