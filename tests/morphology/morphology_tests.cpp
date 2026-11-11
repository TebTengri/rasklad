// Independent reviewer tests by Codex. Artificial fixtures only; no corpus.
#include "morphologydata.h"
#include "morphologyeval.h"
#include "hunspellengine.h"
#include "benchmarkeval.h"
#include "detectormanifest.h"
#include <QTest>
#include <QTemporaryDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
using namespace rasklad;
using namespace rasklad::morphology;
using namespace rasklad::benchmark;
namespace {
QByteArray readRepo(const char *relative) {
    QFile file(QStringLiteral(RASKLAD_SOURCE_ROOT)+QLatin1Char('/')+QString::fromLatin1(relative));
    if (!file.open(QIODevice::ReadOnly)) return {};
    return file.readAll();
}
bool write(const QString &path,const QByteArray &bytes) {
    QFile f(path);return f.open(QIODevice::WriteOnly)&&f.write(bytes)==bytes.size();
}
class FakeSpell final: public SpellChecker {
public:
    QString accepted;
    mutable int calls=0;
    bool accepts(QStringView token)const override {++calls;return token==accepted;}
};
}
class MorphologyReviewTests:public QObject {
 Q_OBJECT
private Q_SLOTS:
 void frozenProtocol() {
    const auto raw=readRepo("data/experiment-003-protocol.json");QVERIFY(!raw.isEmpty());
    QCOMPARE(validateProtocol003(raw).error,ProtocolError::None);
    auto obj=QJsonDocument::fromJson(raw).object();auto thresholds=obj.value(QStringLiteral("thresholds")).toObject();
    thresholds.insert(QStringLiteral("gap"),3.0);obj.insert(QStringLiteral("thresholds"),thresholds);
    QCOMPARE(validateProtocol003(QJsonDocument(obj).toJson()).error,ProtocolError::Mismatch);
    obj=QJsonDocument::fromJson(raw).object();obj.insert(QStringLiteral("extra"),QStringLiteral("not-to-be-echoed"));
    QCOMPARE(validateProtocol003(QJsonDocument(obj).toJson()).error,ProtocolError::Mismatch);
 }
 void sourceManifestGuards() {
    const auto raw=readRepo("data/morphology-003-sources.json");QVERIFY(!raw.isEmpty());
    QCOMPARE(parseMorphManifest(raw).error,MorphManifestError::None);
    auto obj=QJsonDocument::fromJson(raw).object();obj.insert(QStringLiteral("release_approved"),true);
    QCOMPARE(parseMorphManifest(QJsonDocument(obj).toJson()).error,MorphManifestError::ReleaseApproved);
    obj=QJsonDocument::fromJson(raw).object();auto langs=obj.value(QStringLiteral("languages")).toArray();
    auto first=langs.at(0).toObject();first.insert(QStringLiteral("control_aff_path"),QStringLiteral("build/experiment-003-data/control/../escape.aff"));langs.replace(0,first);obj.insert(QStringLiteral("languages"),langs);
    QVERIFY(parseMorphManifest(QJsonDocument(obj).toJson()).error!=MorphManifestError::None);
    QVERIFY(!isSafeLibraryPath(QStringLiteral("/usr/lib/../secret")));
    QVERIFY(!isSafeProjectPath(QStringLiteral("build/../secret"),kAffPrefix));
 }
 void controlRetainsOtherBytes() {
    const QByteArray raw("SET UTF-8\r\nKEEPCASE K\r\n# SFX comment\r\n PFX R Y 1\r\nPFX R 0 re .\r\nSFX S Y 1\r\nSFX S 0 s .\r\nNEEDAFFIX N");
    const auto c=removePfxSfxRows(raw);QCOMPARE(c.error,AffControlError::None);
    QCOMPARE(c.bytes,QByteArray("SET UTF-8\r\nKEEPCASE K\r\n# SFX comment\r\nNEEDAFFIX N"));
    QCOMPARE(c.removedPfxRows,qint64(2));QCOMPARE(c.removedSfxRows,qint64(2));
    QVERIFY(removePfxSfxRows(QByteArray("SET UTF-8\n")).error!=AffControlError::None);
    QVERIFY(removePfxSfxRows(QByteArray("PFXbad X\n")).error!=AffControlError::None);
    QVERIFY(removePfxSfxRows(QByteArray("SFX S Y 1\n\0bad",15)).error!=AffControlError::None);
 }
 void queryBoundaries() {
    QVERIFY(!isQueryableToken(QStringView()));
    QVERIFY(isQueryableToken(QString(32,QChar(u'я'))));
    QVERIFY(!isQueryableToken(QString(33,QChar(u'a'))));
    QVERIFY(!isQueryableToken(QStringLiteral("book\n")));
    QString bad=QStringLiteral("book");bad.append(QChar(0));QVERIFY(!isQueryableToken(bad));
    QVERIFY(!isQueryableToken(QString(1,QChar(0xD800))));
 }
 void actualSuffixPrefixAndCase() {
    QTemporaryDir dir;QVERIFY(dir.isValid());
    const QByteArray aff("SET UTF-8\nKEEPCASE K\nNEEDAFFIX N\nPFX R Y 1\nPFX R 0 re .\nSFX S Y 1\nSFX S 0 s .\n");
    const QByteArray dic("3\nbook/SR\niPhone/K\nstem/NS\n");
    const auto stripped=removePfxSfxRows(aff);QCOMPARE(stripped.error,AffControlError::None);
    const QString a=dir.filePath(QStringLiteral("full.aff")),c=dir.filePath(QStringLiteral("control.aff")),d=dir.filePath(QStringLiteral("words.dic"));
    QVERIFY(write(a,aff));QVERIFY(write(c,stripped.bytes));QVERIFY(write(d,dic));
    HunspellEngine::Error e;auto full=HunspellEngine::create(a,d,e);QCOMPARE(e,HunspellEngine::Error::None);QVERIFY(full);
    auto control=HunspellEngine::create(c,d,e);QCOMPARE(e,HunspellEngine::Error::None);QVERIFY(control);
    for(const auto &w:{QStringLiteral("book"),QStringLiteral("BOOK"),QStringLiteral("iPhone")}) {QVERIFY(full->accepts(w));QVERIFY(control->accepts(w));}
    for(const auto &w:{QStringLiteral("books"),QStringLiteral("rebook"),QStringLiteral("rebooks"),QStringLiteral("stems")}) {QVERIFY(full->accepts(w));QVERIFY(!control->accepts(w));}
    for(const auto &w:{QStringLiteral("stem"),QStringLiteral("iphone"),QStringLiteral("IPHONE"),QStringLiteral("zzzzzz")}) {QVERIFY(!full->accepts(w));QVERIFY(!control->accepts(w));}
    QVERIFY(write(c,QByteArray("SET ISO8859-1\n")));auto nonUtf8=HunspellEngine::create(c,d,e);QVERIFY(!nonUtf8);QCOMPARE(e,HunspellEngine::Error::NotUtf8);
 }
 void loadedLibraryIdentity() {
    QCOMPARE(compiledHunspellVersion(),QStringLiteral("1.7.5"));
    QCOMPARE(runtimeHunspellVersion(),compiledHunspellVersion());
    const QString p=loadedHunspellLibraryPath();QVERIFY(p.startsWith(QStringLiteral("/usr/lib/")));
    QFile f(p);QVERIFY(f.open(QIODevice::ReadOnly));const auto b=f.readAll();LoadedLibrary lib;QString err;
    QVERIFY(verifyLoadedHunspell(p,experiment::sha256Hex(b),b.size(),QStringLiteral("1.7.5"),lib,err));
    QVERIFY(!verifyLoadedHunspell(p,QString(64,QChar(u'0')),b.size(),QStringLiteral("1.7.5"),lib,err));
 }
 void methodRoutingAndSharedAmbiguity() {
    MorphFeatures f;f.base.supported=true;f.base.keyCount=4;f.base.twoScore=DecisionAction::Correct;f.full.alternative=true;
    QCOMPARE(decideMorph(MorphMethod::StemExact,f),DecisionAction::Abstain);
    QCOMPARE(decideMorph(MorphMethod::HunspellNoPfxSfx,f),DecisionAction::Abstain);
    QCOMPARE(decideMorph(MorphMethod::HunspellFull,f),DecisionAction::Correct);
    QCOMPARE(decideMorph(MorphMethod::MorphThenModel,f),DecisionAction::Correct);
    QCOMPARE(decideMorph(MorphMethod::ModelConfirmedMorph,f),DecisionAction::Correct);
    f.base.twoScore=DecisionAction::Abstain;QCOMPARE(decideMorph(MorphMethod::ModelConfirmedMorph,f),DecisionAction::Abstain);
    f.base.twoScore=DecisionAction::Correct;f.control={true,true};QVERIFY(sharedAmbiguous(f));
    for(auto m:kMorphMethods) {
        auto o=classifyOutcome(decideMorph(m,f),true,sharedAmbiguous(f),true);
        QVERIFY(o!=Outcome::Recovered);
        if(decideMorph(m,f)==DecisionAction::Correct)QVERIFY(isUnsafe(o));
    }
    f.full.observed=true;QCOMPARE(decideMorph(MorphMethod::HunspellFull,f),DecisionAction::Keep);
    for(int n:{0,1,2,3,33}) {f.base.keyCount=n;for(auto m:kMorphMethods)if(m!=MorphMethod::AlwaysKeep)QCOMPARE(decideMorph(m,f),DecisionAction::Abstain);}
 }
 void exactCaseTypesAndPairDirection() {
    QVERIFY(exactCaseTypeKey(DetectorLanguage::En,false,QStringLiteral("Book"))!=exactCaseTypeKey(DetectorLanguage::En,false,QStringLiteral("book")));
    QVERIFY(exactCaseTypeKey(DetectorLanguage::En,true,QStringLiteral("book"))!=exactCaseTypeKey(DetectorLanguage::En,false,QStringLiteral("book")));
    FakeSpell en,ru;en.accepted=QStringLiteral("abcd");ru.accepted=QStringLiteral("фисв");SpellPair pair{{{&en,&ru}}};
    CaseFeatures base;base.supported=true;base.keyCount=4;
    const auto a=queryPair(pair,QStringLiteral("abcd"),SourceLayout::Us,base);
    const auto b=queryPair(pair,QStringLiteral("фисв"),SourceLayout::Ru,base);
    QVERIFY(a.observed&&a.alternative&&b.observed&&b.alternative);
    const int calls=en.calls+ru.calls;base.keyCount=3;
    (void)queryPair(pair,QStringLiteral("abc"),SourceLayout::Us,base);QCOMPARE(en.calls+ru.calls,calls);
 }
};
QTEST_APPLESS_MAIN(MorphologyReviewTests)
#include "morphology_tests.moc"
