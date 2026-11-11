// Тесты helper'ов опыта 004 (SYS-236). Только синтетические fixtures в
// памяти и вымышленные строки; корпус, словари, модель и артефакты не
// читаются. Из репозитория читается лишь замороженный протокол 004.

#include "benchmarkeval.h"
#include "benchmarkprotocol.h"
#include "domaindecisions.h"
#include "frontcodedlexicon.h"
#include "layoutconverter.h"
#include "morphologyeval.h"
#include "ngramdetector.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>

#include <optional>

using namespace rasklad;
using namespace rasklad::domain;
using rasklad::benchmark::CaseFeatures;
using rasklad::benchmark::FeatureContext;
using rasklad::morphology::MorphFeatures;
using rasklad::morphology::SpellChecker;
using rasklad::morphology::SpellPair;

namespace {

QByteArray readRepo(const char *relative)
{
    QFile file(QStringLiteral(RASKLAD_SOURCE_ROOT) + QLatin1Char('/') + QString::fromLatin1(relative));
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return file.readAll();
}

QString hex(char c)
{
    return QString(64, QLatin1Char(c));
}

class FakeSpell final : public SpellChecker
{
public:
    QStringList accepted;
    mutable int calls = 0;
    bool accepts(QStringView token) const override
    {
        ++calls;
        return accepted.contains(token.toString());
    }
};

QJsonObject goodCasesManifest()
{
    QJsonObject cases;
    cases.insert(QStringLiteral("path"), QStringLiteral("build/experiment-004-data/cases.tsv"));
    cases.insert(QStringLiteral("sha256"), hex('a'));
    cases.insert(QStringLiteral("bytes"), 120);
    cases.insert(QStringLiteral("rows"), 3);
    QJsonObject notice;
    notice.insert(QStringLiteral("notice"), QStringLiteral("data/notices/Fixture-LICENSE.txt"));
    notice.insert(QStringLiteral("notice_sha256"), hex('b'));
    QJsonObject o;
    o.insert(QStringLiteral("schema_version"), 1);
    o.insert(QStringLiteral("status"), QStringLiteral("approved-local-evaluation"));
    o.insert(QStringLiteral("scope"), QStringLiteral("local-evaluation-only"));
    o.insert(QStringLiteral("release_approved"), false);
    o.insert(QStringLiteral("description"), QStringLiteral("extra descriptive field"));
    o.insert(QStringLiteral("cases"), cases);
    o.insert(QStringLiteral("notices"), QJsonArray{notice});
    return o;
}

CasesManifestError manifestError(const QJsonObject &o)
{
    return parseCasesManifest(QJsonDocument(o).toJson()).error;
}

QJsonObject withCases(const char *field, const QJsonValue &value)
{
    QJsonObject o = goodCasesManifest();
    QJsonObject cases = o.value(QStringLiteral("cases")).toObject();
    cases.insert(QString::fromLatin1(field), value);
    o.insert(QStringLiteral("cases"), cases);
    return o;
}

QByteArray tsv(const QStringList &rows)
{
    QByteArray out;
    for (const QString &row : rows) {
        out.append(row.toUtf8());
        out.append('\n');
    }
    return out;
}

CasesTsvError tsvError(const QStringList &rows)
{
    return parseCasesTsv(tsv(rows), rows.size()).error;
}

// Три вымышленных строки: valid, protected (код) и wrong-копия.
QStringList goodRows()
{
    return {
        QStringLiteral("0\ten\tvalid\tus\tborrow\tborrow"),
        QStringLiteral("21\tru\tprotected\tus\tfn_main()\tfn_main()"),
        QStringLiteral("39\tru\twrong\tus\tghbdtn\tпривет"),
    };
}

QJsonObject goodMetadata()
{
    auto file = [](const char *name, qint64 bytes, const char *sha) {
        QJsonObject f;
        f.insert(QStringLiteral("file"), QString::fromLatin1(name));
        f.insert(QStringLiteral("bytes"), bytes);
        f.insert(QStringLiteral("sha256"), QString::fromLatin1(sha));
        f.insert(QStringLiteral("format"), QStringLiteral("descriptive"));
        return f;
    };
    QJsonObject artifacts;
    artifacts.insert(QStringLiteral("model"), file(kModelFile.fileName, kModelFile.bytes, kModelFile.sha256));
    artifacts.insert(QStringLiteral("exactFullLexicons"),
                     file(kStemFile.fileName, kStemFile.bytes, kStemFile.sha256));
    QJsonObject en;
    en.insert(QStringLiteral("sha256"), hex('1'));
    en.insert(QStringLiteral("notice"), QStringLiteral("data/notices/en.txt"));
    en.insert(QStringLiteral("noticeSha256"), hex('2'));
    QJsonObject ru;
    ru.insert(QStringLiteral("sha256"), hex('3'));
    ru.insert(QStringLiteral("notice"), QStringLiteral("data/notices/ru.txt"));
    ru.insert(QStringLiteral("noticeSha256"), hex('4'));
    QJsonObject training;
    training.insert(QStringLiteral("en"), en);
    training.insert(QStringLiteral("ru"), ru);
    QJsonObject o;
    o.insert(QStringLiteral("artifacts"), artifacts);
    o.insert(QStringLiteral("protocolSha256"), QString::fromLatin1(kProtocol002Sha256));
    o.insert(QStringLiteral("trainingManifestSha256"), QString::fromLatin1(kTrainingManifestSha256));
    o.insert(QStringLiteral("trainingSources"), training);
    o.insert(QStringLiteral("scope"),
             QStringLiteral("local-experiment-only; release_approved=false; not approved for distribution"));
    return o;
}

std::array<ExpectedTrainingSource, 2> metadataSources()
{
    return {{{hex('1'), QStringLiteral("data/notices/en.txt"), hex('2')},
             {hex('3'), QStringLiteral("data/notices/ru.txt"), hex('4')}}};
}

KeySequence keysOf(const char *ascii)
{
    return canonicalizeToken(QString::fromLatin1(ascii), SourceLayout::Us).keys;
}

} // namespace

class DomainTests : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void frozenProtocolHash()
    {
        const QByteArray raw = readRepo("data/experiment-004-protocol.json");
        QVERIFY(!raw.isEmpty());
        QCOMPARE(validateProtocol004(raw), ProtocolError::None);
        QByteArray changed = raw;
        changed[changed.size() - 2] = changed.at(changed.size() - 2) == ' ' ? '\t' : ' ';
        QCOMPARE(validateProtocol004(changed), ProtocolError::HashMismatch);
        QCOMPARE(validateProtocol004(raw + QByteArray(kMaxProtocolBytes, ' ')), ProtocolError::TooLarge);
        QCOMPARE(validateProtocol004(QByteArray()), ProtocolError::HashMismatch);
    }

    void methodOrderMatchesProtocol()
    {
        const QJsonObject protocol = QJsonDocument::fromJson(readRepo("data/experiment-004-protocol.json")).object();
        const QJsonArray methods = protocol.value(QStringLiteral("methods")).toArray();
        QCOMPARE(methods.size(), kDomainMethodCount);
        for (int i = 0; i < kDomainMethodCount; ++i)
            QCOMPARE(methods.at(i).toString(), QString::fromLatin1(domainMethodName(kDomainMethods[std::size_t(i)])));
    }

    void casesManifest()
    {
        const CasesManifestResult good = parseCasesManifest(QJsonDocument(goodCasesManifest()).toJson());
        QCOMPARE(good.error, CasesManifestError::None);
        QCOMPARE(good.manifest.casesRows, qint64(3));
        QCOMPARE(good.manifest.casesBytes, qint64(120));
        QCOMPARE(good.manifest.notices.size(), std::size_t(1));

        QCOMPARE(parseCasesManifest(QByteArray("[]")).error, CasesManifestError::InvalidJson);
        QCOMPARE(parseCasesManifest(QByteArray(kMaxCasesManifestBytes + 1, ' ')).error, CasesManifestError::TooLarge);

        QJsonObject o = goodCasesManifest();
        o.insert(QStringLiteral("schema_version"), 1.5);
        QCOMPARE(manifestError(o), CasesManifestError::SchemaVersion);
        o = goodCasesManifest();
        o.insert(QStringLiteral("status"), QStringLiteral("draft"));
        QCOMPARE(manifestError(o), CasesManifestError::StatusNotApproved);
        o = goodCasesManifest();
        o.insert(QStringLiteral("scope"), QStringLiteral("release"));
        QCOMPARE(manifestError(o), CasesManifestError::ScopeMismatch);
        o = goodCasesManifest();
        o.insert(QStringLiteral("release_approved"), QStringLiteral("false"));
        QCOMPARE(manifestError(o), CasesManifestError::ReleaseApproved);
        o = goodCasesManifest();
        o.remove(QStringLiteral("release_approved"));
        QCOMPARE(manifestError(o), CasesManifestError::ReleaseApproved);

        QCOMPARE(manifestError(withCases("path", QStringLiteral("build/experiment-004-data/../cases.tsv"))),
                 CasesManifestError::BadCases);
        QCOMPARE(manifestError(withCases("path", QStringLiteral("build/experiment-004-data/other.tsv"))),
                 CasesManifestError::BadCases);
        QCOMPARE(manifestError(withCases("sha256", hex('A'))), CasesManifestError::BadCases);
        QCOMPARE(manifestError(withCases("bytes", 0)), CasesManifestError::BadCases);
        QCOMPARE(manifestError(withCases("bytes", double(kMaxCasesBytes) + 1)), CasesManifestError::BadCases);
        QCOMPARE(manifestError(withCases("bytes", QStringLiteral("120"))), CasesManifestError::BadCases);
        QCOMPARE(manifestError(withCases("rows", 500001)), CasesManifestError::BadCases);
        QCOMPARE(manifestError(withCases("rows", 2.5)), CasesManifestError::BadCases);

        o = goodCasesManifest();
        o.insert(QStringLiteral("notices"), QJsonArray());
        QCOMPARE(manifestError(o), CasesManifestError::BadNotices);
        QJsonArray many;
        for (int i = 0; i <= kMaxCaseNotices; ++i) {
            QJsonObject n;
            n.insert(QStringLiteral("notice"), QStringLiteral("data/notices/n%1.txt").arg(i));
            n.insert(QStringLiteral("notice_sha256"), hex('c'));
            many.append(n);
        }
        o.insert(QStringLiteral("notices"), many);
        QCOMPARE(manifestError(o), CasesManifestError::BadNotices);
        many.removeLast();
        o.insert(QStringLiteral("notices"), many);
        QCOMPARE(manifestError(o), CasesManifestError::None);
        QJsonArray duplicate{many.at(0), many.at(0)};
        o.insert(QStringLiteral("notices"), duplicate);
        QCOMPARE(manifestError(o), CasesManifestError::BadNotices);
        QJsonObject escape;
        escape.insert(QStringLiteral("notice"), QStringLiteral("data/notices/../../secret"));
        escape.insert(QStringLiteral("notice_sha256"), hex('c'));
        o.insert(QStringLiteral("notices"), QJsonArray{escape});
        QCOMPARE(manifestError(o), CasesManifestError::BadNotices);
    }

    void tsvAccepted()
    {
        const CasesTsvResult r = parseCasesTsv(tsv(goodRows()), 3);
        QCOMPARE(r.error, CasesTsvError::None);
        QCOMPARE(r.cases.size(), std::size_t(3));
        QCOMPARE(r.cases[1].document, 21);
        QCOMPARE(r.cases[1].documentLanguage, DetectorLanguage::Ru);
        QCOMPARE(r.cases[1].role, CaseRole::Protected);
        // Язык документа может отличаться от раскладки (английский термин в RU).
        QCOMPARE(r.cases[1].layout, SourceLayout::Us);
        QCOMPARE(r.cases[2].role, CaseRole::Wrong);
        QCOMPARE(r.cases[2].expected, QStringLiteral("привет"));
        // Регистр сохраняется; EN-слово, набранное в ru.
        QCOMPARE(tsvError({QStringLiteral("3\ten\twrong\tru\tЕуые\tTest")}), CasesTsvError::None);
        QCOMPARE(tsvError({QStringLiteral("3\tru\twrong\tus\tRjn\tКот")}), CasesTsvError::None);
    }

    void tsvRawLimits()
    {
        QCOMPARE(parseCasesTsv(QByteArray(), 1).error, CasesTsvError::Empty);
        QByteArray noFinal = tsv(goodRows());
        noFinal.chop(1);
        QCOMPARE(parseCasesTsv(noFinal, 3).error, CasesTsvError::MissingFinalNewline);
        QCOMPARE(parseCasesTsv(tsv(goodRows()), 2).error, CasesTsvError::RowCountMismatch);
        QCOMPARE(parseCasesTsv(QByteArray(kMaxCasesRows + 1, '\n'), kMaxCasesRows + 1).error,
                 CasesTsvError::TooManyRows);
        QCOMPARE(parseCasesTsv(QByteArray(kMaxCasesBytes + 1, 'x'), 1).error, CasesTsvError::TooLarge);
        QCOMPARE(parseCasesTsv(tsv({goodRows().at(0), QString(), goodRows().at(1)}), 3).error,
                 CasesTsvError::EmptyLine);
        QCOMPARE(parseCasesTsv(QByteArray("0\ten\tvalid\tus\tx\tx\r\n"), 1).error, CasesTsvError::CarriageReturn);
        const QString longText(kMaxCaseLineBytes, QLatin1Char('a'));
        QCOMPARE(tsvError({QStringLiteral("0\ten\tvalid\tus\t") + longText + QLatin1Char('\t') + longText}),
                 CasesTsvError::LineTooLong);
        QByteArray badUtf8("0\ten\tvalid\tus\t\xC3\x28\t\xC3\x28\n");
        QCOMPARE(parseCasesTsv(badUtf8, 1).error, CasesTsvError::InvalidUtf8);
    }

    void tsvFields_data()
    {
        QTest::addColumn<QString>("row");
        QTest::addColumn<CasesTsvError>("error");
        QTest::newRow("five-fields") << QStringLiteral("0\ten\tvalid\tus\tx") << CasesTsvError::BadColumns;
        QTest::newRow("seven-fields") << QStringLiteral("0\ten\tvalid\tus\tx\tx\tx") << CasesTsvError::BadColumns;
        QTest::newRow("doc-40") << QStringLiteral("40\ten\tvalid\tus\tx\tx") << CasesTsvError::BadDocument;
        QTest::newRow("doc-leading-zero") << QStringLiteral("07\ten\tvalid\tus\tx\tx") << CasesTsvError::BadDocument;
        QTest::newRow("doc-sign") << QStringLiteral("-1\ten\tvalid\tus\tx\tx") << CasesTsvError::BadDocument;
        QTest::newRow("doc-empty") << QStringLiteral("\ten\tvalid\tus\tx\tx") << CasesTsvError::BadDocument;
        QTest::newRow("lang") << QStringLiteral("0\tEN\tvalid\tus\tx\tx") << CasesTsvError::BadLanguage;
        QTest::newRow("role") << QStringLiteral("0\ten\tcode\tus\tx\tx") << CasesTsvError::BadRole;
        QTest::newRow("layout") << QStringLiteral("0\ten\tvalid\ten\tx\tx") << CasesTsvError::BadLayout;
        QTest::newRow("empty-text") << QStringLiteral("0\ten\tvalid\tus\t\tx") << CasesTsvError::EmptyField;
        QTest::newRow("empty-expected") << QStringLiteral("0\ten\tvalid\tus\tx\t") << CasesTsvError::EmptyField;
        QTest::newRow("too-long") << QStringLiteral("0\ten\tvalid\tus\t") + QString(kMaxCaseFieldUtf16 + 1, u'я')
                                         + QStringLiteral("\tx")
                                  << CasesTsvError::FieldTooLong;
        QTest::newRow("control") << QStringLiteral("0\ten\tvalid\tus\ta\x01" "b\ta\x01" "b") << CasesTsvError::BadCharacter;
        QTest::newRow("nbsp") << QStringLiteral("0\ten\tvalid\tus\ta b\ta b") << CasesTsvError::BadCharacter;
        QTest::newRow("valid-differs") << QStringLiteral("0\ten\tvalid\tus\tabc\tabd") << CasesTsvError::ExpectedDiffers;
        QTest::newRow("protected-differs") << QStringLiteral("0\ten\tprotected\tus\tabc\tABC")
                                           << CasesTsvError::ExpectedDiffers;
        QTest::newRow("wrong-same-alphabet") << QStringLiteral("0\ten\twrong\tus\tghbdtn\tghbdtn")
                                             << CasesTsvError::BadWrongExpected;
        QTest::newRow("wrong-non-letter") << QStringLiteral("0\tru\twrong\tus\tghbd1n\tприв1т")
                                          << CasesTsvError::BadWrongExpected;
        QTest::newRow("wrong-too-long") << QStringLiteral("0\tru\twrong\tus\t") + QString(33, u'g')
                                               + QStringLiteral("\t") + QString(33, u'п')
                                        << CasesTsvError::BadWrongExpected;
        QTest::newRow("wrong-layout-mismatch") << QStringLiteral("0\tru\twrong\tru\tghbdtn\tпривет")
                                               << CasesTsvError::BadWrongExpected;
        QTest::newRow("wrong-conversion") << QStringLiteral("0\tru\twrong\tus\tghbdtm\tпривет")
                                          << CasesTsvError::WrongConversion;
        QTest::newRow("wrong-mixed-script") << QStringLiteral("0\tru\twrong\tus\tghиdtn\tпривет")
                                            << CasesTsvError::WrongNotGenerated;
    }

    void tsvFields()
    {
        QFETCH(QString, row);
        QFETCH(CasesTsvError, error);
        QCOMPARE(tsvError({row}), error);
        // Одна плохая строка среди хороших — отказ всего файла.
        const CasesTsvResult all = parseCasesTsv(tsv(goodRows() + QStringList{row}), 4);
        QCOMPARE(all.error, error);
        QVERIFY(all.cases.empty());
        QVERIFY(!casesTsvErrorText(error).contains(row));
    }

    void protectedVariants()
    {
        MorphFeatures f;
        f.base.supported = true;
        f.base.keyCount = 5;
        f.base.twoScore = DecisionAction::Correct;
        f.full = {false, true};
        for (const CaseRole role : {CaseRole::Valid, CaseRole::Protected, CaseRole::Wrong}) {
            const auto a = decideDomain(f, role);
            QCOMPARE(a[0], DecisionAction::Keep);
            QCOMPARE(a[1], morphology::decideMorph(morphology::MorphMethod::TwoScore, f));
            QCOMPARE(a[2], morphology::decideMorph(morphology::MorphMethod::HunspellFull, f));
            QCOMPARE(a[3], morphology::decideMorph(morphology::MorphMethod::ModelConfirmedMorph, f));
            QCOMPARE(a[4], morphology::decideMorph(morphology::MorphMethod::MorphThenModel, f));
            QCOMPARE(a[1], DecisionAction::Correct);
            for (std::size_t k = 0; k < 4; ++k)
                QCOMPARE(a[5 + k], role == CaseRole::Protected ? DecisionAction::Keep : a[1 + k]);
        }
        // Неподходящий случай: все, кроме alwaysKeep, воздерживаются; защита
        // всё равно даёт Keep только на protected.
        MorphFeatures shortCase;
        shortCase.base.supported = true;
        shortCase.base.keyCount = 3;
        const auto valid = decideDomain(shortCase, CaseRole::Valid);
        const auto prot = decideDomain(shortCase, CaseRole::Protected);
        for (std::size_t i = 1; i < 5; ++i)
            QCOMPARE(valid[i], DecisionAction::Abstain);
        for (std::size_t i = 5; i < 9; ++i) {
            QCOMPARE(valid[i], DecisionAction::Abstain);
            QCOMPARE(prot[i], DecisionAction::Keep);
        }
    }

    void evaluateWithSyntheticEngines()
    {
        // Крошечная синтетическая модель и словари из вымышленных ключей.
        const KeySequence shortBoth = keysOf("qzx");
        const KeySequence enWord = keysOf("test");
        const NgramModel model = NgramModel::train({enWord, keysOf("tset")}, {keysOf("ghbd"), shortBoth});
        QVERIFY(model.isValid());
        const std::optional<benchmark::FrontCodedLexicon> en = benchmark::FrontCodedLexicon::build({enWord, shortBoth});
        const std::optional<benchmark::FrontCodedLexicon> ru = benchmark::FrontCodedLexicon::build({shortBoth});
        QVERIFY(en && ru);
        FeatureContext context;
        context.model = &model;
        context.train = {{&*en, &*ru}};
        context.full = {{&*en, &*ru}};
        context.singleD = benchmark::frozenSingleD();
        context.twoScore = benchmark::frozenTwoScore();
        QVERIFY(context.isValid());
        FakeSpell fullEn;
        FakeSpell fullRu;
        FakeSpell controlEn;
        FakeSpell controlRu;
        fullEn.accepted = {QStringLiteral("Test"), QStringLiteral("test")};
        const SpellPair full{{{&fullEn, &fullRu}}};
        const SpellPair control{{{&controlEn, &controlRu}}};

        // Короткий supported токен: stem-both сохраняется, Hunspell не вызывается.
        DomainCase shortValid{1, DetectorLanguage::En, CaseRole::Valid, SourceLayout::Us, QStringLiteral("qzx"),
                              QStringLiteral("qzx")};
        DomainRow row = evaluateCase(shortValid, context, control, full);
        QVERIFY(!row.eligible);
        QVERIFY(row.ambiguous);
        QVERIFY(row.roundTrip);
        QCOMPARE(fullEn.calls + fullRu.calls + controlEn.calls + controlRu.calls, 0);
        QByteArray out;
        appendOutputRow(out, 0, row);
        QCOMPARE(out, QByteArray("0\t0\t1\t1\tKAAAAAAAA\n"));

        // Wrong-копия EN-слова, набранная в ru: hunspellFull исправляет.
        DomainCase wrong{2, DetectorLanguage::Ru, CaseRole::Wrong, SourceLayout::Ru, QStringLiteral("Еуые"),
                         QStringLiteral("Test")};
        row = evaluateCase(wrong, context, control, full);
        QVERIFY(row.eligible);
        QVERIFY(!row.ambiguous);
        QVERIFY(row.roundTrip);
        QCOMPARE(row.actions[2], DecisionAction::Correct);
        QCOMPARE(row.actions[6], DecisionAction::Correct);
        QVERIFY(fullEn.calls > 0);

        // Тот же текст как protected-код: защищённые варианты — Keep, базовые — без изменений.
        DomainCase prot{2, DetectorLanguage::Ru, CaseRole::Protected, SourceLayout::Ru, QStringLiteral("Еуые"),
                        QStringLiteral("Еуые")};
        const DomainRow protRow = evaluateCase(prot, context, control, full);
        for (std::size_t i = 0; i < 5; ++i)
            QCOMPARE(protRow.actions[i], row.actions[i]);
        for (std::size_t i = 5; i < 9; ++i)
            QCOMPARE(protRow.actions[i], DecisionAction::Keep);
        QVERIFY(protRow.roundTrip);

        // Неподдерживаемый (смешанный) токен: не eligible, не ambiguous.
        DomainCase mixed{3, DetectorLanguage::Ru, CaseRole::Valid, SourceLayout::Us, QStringLiteral("v2_api"),
                         QStringLiteral("v2_api")};
        row = evaluateCase(mixed, context, control, full);
        QVERIFY(!row.eligible);
        QVERIFY(!row.ambiguous);
    }

    void outputRowFormat()
    {
        DomainRow row;
        row.eligible = true;
        row.ambiguous = false;
        row.roundTrip = true;
        row.actions = {{DecisionAction::Keep, DecisionAction::Correct, DecisionAction::Abstain, DecisionAction::Keep,
                        DecisionAction::Correct, DecisionAction::Abstain, DecisionAction::Keep,
                        DecisionAction::Correct, DecisionAction::Abstain}};
        QByteArray out;
        appendOutputRow(out, 12, row);
        QCOMPARE(out, QByteArray("12\t1\t0\t1\tKCAKCAKCA\n"));
        out.clear();
        appendOutputRow(out, kMaxCasesRows - 1, row);
        QVERIFY(out.size() <= kMaxOutputRowBytes);
        QVERIFY(qint64(kMaxCasesRows) * kMaxOutputRowBytes < 15LL * 1024 * 1024);
    }

    void artifactMetadata()
    {
        const auto sources = metadataSources();
        QCOMPARE(verifyArtifactMetadata(QJsonDocument(goodMetadata()).toJson(), sources), ArtifactMetadataError::None);
        QCOMPARE(verifyArtifactMetadata(QByteArray("{"), sources), ArtifactMetadataError::InvalidJson);

        QJsonObject o = goodMetadata();
        QJsonObject artifacts = o.value(QStringLiteral("artifacts")).toObject();
        QJsonObject model = artifacts.value(QStringLiteral("model")).toObject();
        model.insert(QStringLiteral("sha256"), hex('0'));
        artifacts.insert(QStringLiteral("model"), model);
        o.insert(QStringLiteral("artifacts"), artifacts);
        QCOMPARE(verifyArtifactMetadata(QJsonDocument(o).toJson(), sources), ArtifactMetadataError::ArtifactMismatch);

        o = goodMetadata();
        o.insert(QStringLiteral("protocolSha256"), hex('0'));
        QCOMPARE(verifyArtifactMetadata(QJsonDocument(o).toJson(), sources), ArtifactMetadataError::ProtocolMismatch);
        o = goodMetadata();
        o.insert(QStringLiteral("trainingManifestSha256"), hex('0'));
        QCOMPARE(verifyArtifactMetadata(QJsonDocument(o).toJson(), sources),
                 ArtifactMetadataError::TrainingManifestMismatch);
        auto otherSources = sources;
        otherSources[1].noticeSha256 = hex('9');
        QCOMPARE(verifyArtifactMetadata(QJsonDocument(goodMetadata()).toJson(), otherSources),
                 ArtifactMetadataError::TrainingSourceMismatch);
        o = goodMetadata();
        o.insert(QStringLiteral("scope"), QStringLiteral("local-experiment-only"));
        QCOMPARE(verifyArtifactMetadata(QJsonDocument(o).toJson(), sources), ArtifactMetadataError::DistributionScope);
    }
};

QTEST_GUILESS_MAIN(DomainTests)
#include "domain_tests.moc"
