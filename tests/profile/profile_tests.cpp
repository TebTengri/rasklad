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

QJsonObject productionMetadata()
{
    const profile::ProfilePins &p = profile::productionPins();
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
                        QStringLiteral("local experiment; release_approved=false; not approved for distribution")}};
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
        const profile::ProfilePins &pins = profile::productionPins();
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

        int accepted = 0;
        for (std::size_t i = 0; i < variants.size(); ++i) {
            const bool research = domain::verifyArtifactMetadata(variants[i], expected)
                == domain::ArtifactMetadataError::None;
            const bool production = profile::verifyArtifactMetadata(variants[i], pins);
            QVERIFY2(research == production, QByteArray::number(qulonglong(i)).constData());
            accepted += production ? 1 : 0;
        }
        QCOMPARE(accepted, 2); // valid and valid+extra field
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
        profile::ProfilePins wrongLibrary = f.pins;
        wrongLibrary.library.sha256 = QString(64, QLatin1Char('0'));
        ProfileError error = ProfileError::None;
        QVERIFY(!profile::LocalEvidenceProvider::load(f.root, wrongLibrary, error));
        QCOMPARE(error, ProfileError::LibraryMismatch);

        QVERIFY(QFile::remove(f.root + QLatin1Char('/') + f.pins.stem.path));
        QVERIFY(!profile::LocalEvidenceProvider::load(f.root, f.pins, error));
        QCOMPARE(error, ProfileError::MissingFile);
    }
};

QTEST_GUILESS_MAIN(ProfileTests)
#include "profile_tests.moc"
