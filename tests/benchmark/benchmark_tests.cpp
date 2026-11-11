// Модульные тесты helper'ов независимой оценки SYS-233: front-coded точный
// словарь и его формат, замороженный протокол, паспорт оценки, строгий
// разбор Tatoeba TSV, выборка, токенизация, решения методов, исходы,
// аналитическая точность, парные разности, шум испытания и границы записи
// артефактов.
//
// Все данные искусственные и создаются в тесте. Корпус оценки, словари и
// сеть не используются; единственный читаемый файл репозитория — замороженный
// протокол. Файловые тесты работают во временном каталоге QTemporaryDir.

#include "benchmarkeval.h"
#include "benchmarkio.h"
#include "benchmarkprotocol.h"
#include "detectormanifest.h"
#include "evaluationdata.h"
#include "frontcodedlexicon.h"
#include "layoutconverter.h"
#include "ngramdetector.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QTemporaryDir>
#include <QTest>

#include <algorithm>
#include <array>
#include <initializer_list>
#include <limits>
#include <optional>
#include <vector>

using namespace rasklad;
using namespace rasklad::benchmark;

namespace {

KeySequence keys(std::initializer_list<int> values)
{
    KeySequence k;
    for (const int v : values)
        k.append(static_cast<char>(v));
    return k;
}

KeySequence canon(const QString &text, SourceLayout layout)
{
    const CanonicalToken token = canonicalizeToken(text, layout);
    return token.status == CanonicalStatus::Ok ? token.keys : KeySequence();
}

QString u8(const char *utf8)
{
    return QString::fromUtf8(utf8);
}

// Детерминированный набор ключей длины 1..32 с байтами 0..32, включая 0.
std::vector<KeySequence> generatedKeys(int count, int salt)
{
    std::vector<KeySequence> out;
    for (int i = 0; i < count; ++i) {
        const int length = 1 + (i * 7 + salt) % kMaxTokenKeys;
        KeySequence k;
        for (int j = 0; j < length; ++j)
            k.append(static_cast<char>((i * 3 + j * 11 + salt * 5 + (i * j) % 7) % NgramModel::kLetterKeys));
        out.push_back(k);
    }
    return out;
}

std::vector<KeySequence> edgeKeys()
{
    std::vector<KeySequence> out{keys({0}), keys({0, 0}), keys({0, 0, 0}), keys({0, 1}), keys({1, 0}),
                                 keys({5, 0, 3}), keys({32}), keys({32, 0}), keys({1}), keys({1, 2})};
    out.push_back(KeySequence(kMaxTokenKeys, '\0'));
    out.push_back(KeySequence(kMaxTokenKeys, static_cast<char>(32)));
    return out;
}

void appendU32(QByteArray &out, quint32 value)
{
    for (int shift = 0; shift < 32; shift += 8)
        out.append(static_cast<char>((value >> shift) & 0xFFU));
}

void setU32(QByteArray &data, qsizetype offset, quint32 value)
{
    for (int i = 0; i < 4; ++i)
        data[offset + i] = static_cast<char>((value >> (8 * i)) & 0xFFU);
}

QByteArray bytes(std::initializer_list<int> values)
{
    QByteArray out;
    for (const int v : values)
        out.append(static_cast<char>(v));
    return out;
}

// Ручная сборка сериализованного словаря для проверок отказа.
QByteArray rawLexicon(quint32 count, const std::vector<quint32> &offsets, const QByteArray &payload,
                      quint32 blockSize = 16, quint32 version = 1)
{
    QByteArray out("RSKF");
    appendU32(out, version);
    appendU32(out, count);
    appendU32(out, blockSize);
    appendU32(out, quint32(offsets.size()));
    appendU32(out, quint32(payload.size()));
    for (const quint32 offset : offsets)
        appendU32(out, offset);
    out.append(payload);
    return out;
}

QByteArray encode(const QJsonObject &object)
{
    return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

// ---- Fixture паспорта оценки ----

QJsonObject evalSource(const char *language, const char *iso)
{
    QJsonObject o;
    o.insert(QStringLiteral("language"), QString::fromLatin1(language));
    o.insert(QStringLiteral("iso_language"), QString::fromLatin1(iso));
    o.insert(QStringLiteral("sha256"), QString(64, QLatin1Char('a')));
    o.insert(QStringLiteral("bytes"), 1234);
    o.insert(QStringLiteral("notice"), QStringLiteral("data/notices/tatoeba.README.txt"));
    o.insert(QStringLiteral("notice_sha256"), QString(64, QLatin1Char('b')));
    o.insert(QStringLiteral("url"), QStringLiteral("https://example.invalid/fixture"));
    return o;
}

QJsonObject evalManifestObject()
{
    QJsonObject root;
    root.insert(QStringLiteral("schema_version"), 1);
    root.insert(QStringLiteral("status"), QStringLiteral("approved-local-evaluation"));
    root.insert(QStringLiteral("scope"), QStringLiteral("local-evaluation-only"));
    root.insert(QStringLiteral("release_approved"), false);
    QJsonArray sources;
    sources.append(evalSource("ru", "rus"));
    sources.append(evalSource("en", "eng"));
    root.insert(QStringLiteral("evaluation_sources"), sources);
    return root;
}

QJsonObject withEvalSource(int index, const char *key, const QJsonValue &value)
{
    QJsonObject root = evalManifestObject();
    QJsonArray sources = root.value(QStringLiteral("evaluation_sources")).toArray();
    QJsonObject source = sources.at(index).toObject();
    if (value.isUndefined())
        source.remove(QString::fromLatin1(key));
    else
        source.insert(QString::fromLatin1(key), value);
    sources.replace(index, source);
    root.insert(QStringLiteral("evaluation_sources"), sources);
    return root;
}

// ---- Fixture модели и словарей ----

struct DetectorFixture {
    NgramModel model;
    std::array<FrontCodedLexicon, 2> train;
    std::array<FrontCodedLexicon, 2> full;
    FeatureContext context;
};

std::vector<KeySequence> canonWords(std::initializer_list<const char *> words, SourceLayout layout)
{
    std::vector<KeySequence> out;
    for (const char *w : words)
        out.push_back(canon(u8(w), layout));
    return out;
}

// Искусственные словари. "hello" добавлен и в ru-полный словарь, чтобы
// получить неоднозначный ключ; "world" есть только в полном en-словаре.
void makeFixture(DetectorFixture &f)
{
    const std::vector<KeySequence> enTrain = canonWords({"hello", "house", "tom", "test"}, SourceLayout::Us);
    const std::vector<KeySequence> ruTrain =
        canonWords({"\xD0\xBC\xD0\xB8\xD1\x80", "\xD0\xB4\xD0\xBE\xD0\xBC",
                    "\xD0\xBA\xD0\xBE\xD1\x88\xD0\xBA\xD0\xB0"},
                   SourceLayout::Ru);
    std::vector<KeySequence> enFull = enTrain;
    enFull.push_back(canon(QStringLiteral("world"), SourceLayout::Us));
    std::vector<KeySequence> ruFull = ruTrain;
    ruFull.push_back(canon(QStringLiteral("hello"), SourceLayout::Us));
    f.model = NgramModel::train(enTrain, ruTrain);
    f.train = {{*FrontCodedLexicon::build(enTrain), *FrontCodedLexicon::build(ruTrain)}};
    f.full = {{*FrontCodedLexicon::build(enFull), *FrontCodedLexicon::build(ruFull)}};
    f.context.model = &f.model;
    f.context.train = {{&f.train[0], &f.train[1]}};
    f.context.full = {{&f.full[0], &f.full[1]}};
    f.context.singleD = frozenSingleD();
    f.context.twoScore = frozenTwoScore();
}

CaseFeatures features(bool supported, int keyCount, bool obsTrain, bool altTrain, bool obsFull, bool altFull,
                      DecisionAction single, DecisionAction two)
{
    CaseFeatures f;
    f.supported = supported;
    f.keyCount = keyCount;
    f.observedTrain = obsTrain;
    f.alternativeTrain = altTrain;
    f.observedFull = obsFull;
    f.alternativeFull = altFull;
    f.singleD = single;
    f.twoScore = two;
    return f;
}

constexpr DecisionAction K = DecisionAction::Keep;
constexpr DecisionAction C = DecisionAction::Correct;
constexpr DecisionAction A = DecisionAction::Abstain;

} // namespace

Q_DECLARE_METATYPE(rasklad::benchmark::CaseFeatures)

class BenchmarkTests : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    // ---- Front-coded словарь ----

    void frontCodedMatchesReferenceSet()
    {
        std::vector<KeySequence> input = generatedKeys(300, 1);
        for (const KeySequence &k : edgeKeys())
            input.push_back(k);
        const QSet<KeySequence> reference(input.begin(), input.end());
        const std::optional<FrontCodedLexicon> lexicon = FrontCodedLexicon::build(input);
        QVERIFY(lexicon.has_value());
        QCOMPARE(lexicon->keyCount(), qint64(reference.size()));
        QVERIFY(lexicon->blockCount() > 1);
        for (const KeySequence &k : input)
            QVERIFY(lexicon->contains(k));
        // Непредставленные ключи, в том числе соседние по порядку и с нулями.
        std::vector<KeySequence> probes = generatedKeys(600, 2);
        for (const KeySequence &k : input) {
            KeySequence longer = k;
            if (longer.size() < kMaxTokenKeys) {
                longer.append('\0');
                probes.push_back(longer);
            }
            if (k.size() > 1)
                probes.push_back(k.left(k.size() - 1));
            KeySequence bumped = k;
            bumped[bumped.size() - 1] = static_cast<char>((static_cast<unsigned char>(k.back()) + 1) % 33);
            probes.push_back(bumped);
        }
        qint64 negatives = 0;
        for (const KeySequence &k : probes) {
            QCOMPARE(lexicon->contains(k), reference.contains(k));
            if (!reference.contains(k))
                ++negatives;
        }
        QVERIFY(negatives > 100);
        // Ключи с нулевыми байтами не теряются и не путаются друг с другом.
        QVERIFY(lexicon->contains(keys({0})));
        QVERIFY(lexicon->contains(keys({0, 0, 0})));
        QVERIFY(!lexicon->contains(keys({0, 0, 0, 0})) || reference.contains(keys({0, 0, 0, 0})));
        // Недопустимые запросы.
        QVERIFY(!lexicon->contains(KeySequence()));
        QVERIFY(!lexicon->contains(KeySequence(kMaxTokenKeys + 1, '\0')));
        QVERIFY(!lexicon->contains(keys({33})));
        QVERIFY(!lexicon->contains(keys({1, 255})));
    }

    void frontCodedIsDeterministic()
    {
        std::vector<KeySequence> input = generatedKeys(100, 3);
        std::vector<KeySequence> shuffled = input;
        std::reverse(shuffled.begin(), shuffled.end());
        shuffled.insert(shuffled.end(), input.begin(), input.begin() + 10);
        const QByteArray a = FrontCodedLexicon::build(input)->serialize();
        const QByteArray b = FrontCodedLexicon::build(shuffled)->serialize();
        QCOMPARE(a, b);
        const std::optional<FrontCodedLexicon> restored = FrontCodedLexicon::deserialize(a);
        QVERIFY(restored.has_value());
        QCOMPARE(restored->serialize(), a);
        QCOMPARE(restored->serializedBytes(), qint64(a.size()));
        std::sort(input.begin(), input.end());
        input.erase(std::unique(input.begin(), input.end()), input.end());
        QVERIFY(restored->decodeAll() == input);
        QVERIFY(restored->residentStructureBytes() >= restored->payloadBytes());
    }

    void frontCodedBlockLayout()
    {
        // 33 ключа -> 3 блока по 16, 16 и 1; первая запись блока полная.
        std::vector<KeySequence> input;
        for (int i = 0; i < 33; ++i)
            input.push_back(keys({1, 2, i}));
        const std::optional<FrontCodedLexicon> lexicon = FrontCodedLexicon::build(input);
        QVERIFY(lexicon.has_value());
        QCOMPARE(lexicon->blockCount(), qint64(3));
        const QByteArray data = lexicon->serialize();
        const qsizetype payloadStart = FrontCodedLexicon::kHeaderBytes + 4 * 3;
        // Блок 0: [0,3,1,2,0], затем [2,1,1] ... 15 раз.
        QCOMPARE(data.mid(payloadStart, 5), bytes({0, 3, 1, 2, 0}));
        QCOMPARE(data.mid(payloadStart + 5, 3), bytes({2, 1, 1}));
        const qsizetype block1 = payloadStart + 5 + 15 * 3;
        QCOMPARE(data.mid(block1, 5), bytes({0, 3, 1, 2, 16}));
        QCOMPARE(data.size(), payloadStart + 3 * 5 + 30 * 3);
        for (const KeySequence &k : input)
            QVERIFY(lexicon->contains(k));
        QVERIFY(!lexicon->contains(keys({1, 2})));
        QVERIFY(!lexicon->contains(keys({1, 2, 0, 0})));
    }

    void frontCodedEmptyAndInvalidBuild()
    {
        const std::optional<FrontCodedLexicon> empty = FrontCodedLexicon::build({});
        QVERIFY(empty.has_value());
        QCOMPARE(empty->keyCount(), qint64(0));
        QVERIFY(!empty->contains(keys({0})));
        const std::optional<FrontCodedLexicon> restored = FrontCodedLexicon::deserialize(empty->serialize());
        QVERIFY(restored.has_value());
        QCOMPARE(restored->keyCount(), qint64(0));

        FrontCodedLexicon::BuildError error = FrontCodedLexicon::BuildError::None;
        QVERIFY(!FrontCodedLexicon::build({KeySequence()}, &error).has_value());
        QCOMPARE(error, FrontCodedLexicon::BuildError::InvalidKey);
        QVERIFY(!FrontCodedLexicon::build({KeySequence(kMaxTokenKeys + 1, '\1')}).has_value());
        QVERIFY(!FrontCodedLexicon::build({keys({1}), keys({33})}).has_value());
    }

    void frontCodedAcceptsHandBuiltCanonicalData()
    {
        // {1}, {1,2}, {1,3}
        const QByteArray valid = rawLexicon(3, {0}, bytes({0, 1, 1, 1, 1, 2, 1, 1, 3}));
        const std::optional<FrontCodedLexicon> lexicon = FrontCodedLexicon::deserialize(valid);
        QVERIFY(lexicon.has_value());
        QVERIFY(lexicon->contains(keys({1})));
        QVERIFY(lexicon->contains(keys({1, 2})));
        QVERIFY(lexicon->contains(keys({1, 3})));
        QVERIFY(!lexicon->contains(keys({1, 4})));
        QCOMPARE(lexicon->serialize(), valid);
    }

    void frontCodedRejectsMalformedRecords_data()
    {
        QTest::addColumn<QByteArray>("data");
        QTest::newRow("prefix longer than previous key")
            << rawLexicon(2, {0}, bytes({0, 1, 1, 2, 1, 2}));
        QTest::newRow("first record has prefix") << rawLexicon(1, {0}, bytes({1, 1, 1}));
        QTest::newRow("zero suffix") << rawLexicon(2, {0}, bytes({0, 1, 1, 1, 0, 0}));
        QByteArray tooLong = bytes({0, 33});
        tooLong.append(QByteArray(33, '\1'));
        QTest::newRow("key longer than 32") << rawLexicon(1, {0}, tooLong);
        QTest::newRow("key byte out of range") << rawLexicon(1, {0}, bytes({0, 1, 33}));
        QTest::newRow("unsorted") << rawLexicon(2, {0}, bytes({0, 1, 2, 0, 1, 1}));
        QTest::newRow("duplicate") << rawLexicon(2, {0}, bytes({0, 1, 1, 0, 1, 1}));
        // Неканоничная запись: {1,3} после {1,2} сохранена целиком (prefixLen 0 < 1).
        QTest::newRow("prefix shorter than common prefix")
            << rawLexicon(2, {0}, bytes({0, 2, 1, 2, 0, 2, 1, 3}));
        QTest::newRow("suffix beyond block") << rawLexicon(1, {0}, bytes({0, 5, 1, 2}));
        QTest::newRow("slack bytes after block") << rawLexicon(1, {0}, bytes({0, 1, 1, 0}));
        QTest::newRow("missing record header") << rawLexicon(2, {0}, bytes({0, 1, 1, 0}));
        QTest::newRow("payload too short for count") << rawLexicon(3, {0}, bytes({0, 1, 1, 1, 1}));
        QTest::newRow("empty with payload") << rawLexicon(0, {}, bytes({0, 1, 1}));
        QTest::newRow("block count mismatch") << rawLexicon(1, {0, 3}, bytes({0, 1, 1, 0, 1, 2}));
        QTest::newRow("first offset not zero") << rawLexicon(1, {1}, bytes({9, 0, 1, 1}));
        QTest::newRow("block size 32") << rawLexicon(1, {0}, bytes({0, 1, 1}), 32);
        QTest::newRow("version 2") << rawLexicon(1, {0}, bytes({0, 1, 1}), 16, 2);
        QByteArray badMagic = rawLexicon(1, {0}, bytes({0, 1, 1}));
        badMagic[3] = 'X';
        QTest::newRow("bad magic") << badMagic;
        QTest::newRow("header only") << QByteArray("RSKF");
    }

    void frontCodedRejectsMalformedRecords()
    {
        QFETCH(QByteArray, data);
        QVERIFY(!FrontCodedLexicon::deserialize(data).has_value());
    }

    void frontCodedRejectsBadOffsetsAndTruncation()
    {
        std::vector<KeySequence> input;
        for (int i = 0; i < 40; ++i)
            input.push_back(keys({i % 33, (i * 5) % 33, 7}));
        const QByteArray valid = FrontCodedLexicon::build(input)->serialize();
        QVERIFY(FrontCodedLexicon::deserialize(valid).has_value());
        const qsizetype offsets = FrontCodedLexicon::kHeaderBytes;

        QByteArray nonIncreasing = valid;
        setU32(nonIncreasing, offsets + 8, 0);
        QVERIFY(!FrontCodedLexicon::deserialize(nonIncreasing).has_value());
        QByteArray beyond = valid;
        setU32(beyond, offsets + 8, 0x7FFFFFFF);
        QVERIFY(!FrontCodedLexicon::deserialize(beyond).has_value());
        QByteArray shifted = valid;
        setU32(shifted, offsets + 4, quint32(static_cast<unsigned char>(valid.at(offsets + 4))) + 1);
        QVERIFY(!FrontCodedLexicon::deserialize(shifted).has_value());
        QByteArray countMismatch = valid;
        setU32(countMismatch, 8, 49);
        QVERIFY(!FrontCodedLexicon::deserialize(countMismatch).has_value());

        for (qsizetype size = 0; size < valid.size(); ++size)
            QVERIFY2(!FrontCodedLexicon::deserialize(valid.left(size)).has_value(), "truncated data accepted");
        QVERIFY(!FrontCodedLexicon::deserialize(valid + QByteArray(1, '\0')).has_value());
    }

    void lexiconPairRoundTripAndRejects()
    {
        const FrontCodedLexicon en = *FrontCodedLexicon::build(generatedKeys(50, 4));
        const FrontCodedLexicon ru = *FrontCodedLexicon::build(edgeKeys());
        const QByteArray pair = serializeLexiconPair(en, ru);
        const std::optional<std::array<FrontCodedLexicon, 2>> restored = deserializeLexiconPair(pair);
        QVERIFY(restored.has_value());
        QCOMPARE(restored->at(0).serialize(), en.serialize());
        QCOMPARE(restored->at(1).serialize(), ru.serialize());
        QVERIFY(restored->at(1).contains(keys({0, 0})));
        QVERIFY(!restored->at(0).contains(keys({0, 0})) || en.contains(keys({0, 0})));

        QVERIFY(!deserializeLexiconPair(pair + QByteArray(1, '\0')).has_value());
        for (qsizetype size = 0; size < pair.size(); size += 7)
            QVERIFY(!deserializeLexiconPair(pair.left(size)).has_value());
        QByteArray swappedSizes = pair;
        setU32(swappedSizes, 12, quint32(ru.serializedBytes()));
        setU32(swappedSizes, 16, quint32(en.serializedBytes()));
        QVERIFY(!deserializeLexiconPair(swappedSizes).has_value());
        QByteArray threeLanguages = pair;
        setU32(threeLanguages, 8, 3);
        QVERIFY(!deserializeLexiconPair(threeLanguages).has_value());
        QByteArray badMagic = pair;
        badMagic[0] = 'X';
        QVERIFY(!deserializeLexiconPair(badMagic).has_value());
    }

    // ---- Протокол ----

    void protocolRepositoryFileMatchesFrozenDocument()
    {
        QFile file(QStringLiteral(RASKLAD_SOURCE_ROOT) + QStringLiteral("/data/experiment-002-protocol.json"));
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QByteArray data = file.read(kMaxProtocolBytes + 1);
        const ProtocolResult result = validateProtocol(data);
        QCOMPARE(result.error, ProtocolError::None);
        QCOMPARE(result.sha256,
                 QString::fromLatin1(QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex()));
    }

    void protocolRejectsAnyEdit()
    {
        const QJsonObject expected = expectedProtocol();
        QCOMPARE(validateProtocol(encode(expected)).error, ProtocolError::None);

        auto mutated = [&](const char *key, const QJsonValue &value) {
            QJsonObject o = expected;
            if (value.isUndefined())
                o.remove(QString::fromLatin1(key));
            else
                o.insert(QString::fromLatin1(key), value);
            return validateProtocol(encode(o));
        };
        QJsonObject thresholds = expected.value(QStringLiteral("thresholds")).toObject();
        QJsonObject two = thresholds.value(QStringLiteral("twoScore")).toObject();
        two.insert(QStringLiteral("gap"), 2.0);
        thresholds.insert(QStringLiteral("twoScore"), two);
        const ProtocolResult gap = mutated("thresholds", thresholds);
        QCOMPARE(gap.error, ProtocolError::Mismatch);
        QCOMPARE(gap.mismatchedField, QStringLiteral("thresholds"));

        QJsonArray methods = expected.value(QStringLiteral("methods")).toArray();
        const QJsonValue fourth = methods.at(3);
        methods.replace(3, methods.at(4));
        methods.replace(4, fourth);
        QCOMPARE(mutated("methods", methods).error, ProtocolError::Mismatch);
        QCOMPARE(mutated("methods", QJsonValue(QJsonValue::Undefined)).error, ProtocolError::Mismatch);
        QCOMPARE(mutated("seed", QStringLiteral("other")).error, ProtocolError::Mismatch);
        QCOMPARE(mutated("schema_version", QStringLiteral("1")).error, ProtocolError::Mismatch);
        QCOMPARE(mutated("short_token_minimum", 3).error, ProtocolError::Mismatch);
        const ProtocolResult extra = mutated("extra", true);
        QCOMPARE(extra.error, ProtocolError::Mismatch);
        QCOMPARE(extra.mismatchedField, QStringLiteral("unexpected field"));

        QJsonObject tokenization = expected.value(QStringLiteral("tokenization")).toObject();
        tokenization.insert(QStringLiteral("outer_trim_characters"), QStringLiteral(".,"));
        QCOMPARE(mutated("tokenization", tokenization).error, ProtocolError::Mismatch);
        QJsonObject challenge = expected.value(QStringLiteral("challenge")).toObject();
        challenge.insert(QStringLiteral("noise_per_layout"), 255);
        QCOMPARE(mutated("challenge", challenge).error, ProtocolError::Mismatch);

        QCOMPARE(validateProtocol(QByteArray("[]")).error, ProtocolError::InvalidJson);
        QCOMPARE(validateProtocol(QByteArray("{")).error, ProtocolError::InvalidJson);
        QCOMPARE(validateProtocol(QByteArray(kMaxProtocolBytes + 1, ' ')).error, ProtocolError::TooLarge);
    }

    void protocolNumbersCompareByValue()
    {
        QVERIFY(jsonEqual(QJsonValue(-13), QJsonValue(-13.0)));
        QVERIFY(!jsonEqual(QJsonValue(-13), QJsonValue(QStringLiteral("-13"))));
        QVERIFY(!jsonEqual(QJsonValue(QJsonArray{1, 2}), QJsonValue(QJsonArray{2, 1})));
        const SingleDThresholds single = frozenSingleD();
        QVERIFY(single.enabled);
        QCOMPARE(single.gap, 2.5);
        const TwoScoreThresholds two = frozenTwoScore();
        QVERIFY(two.enabled);
        QCOMPARE(two.gap, 2.5);
        QCOMPARE(two.minAlternative, -13.0);
        QCOMPARE(two.maxObserved, -9.0);
    }

    // ---- Паспорт оценки ----

    void evaluationManifestAcceptsFixture()
    {
        const EvalManifestResult result = parseEvaluationManifest(encode(evalManifestObject()));
        QCOMPARE(result.error, EvalManifestError::None);
        QCOMPARE(result.manifest.sources[0].isoLanguage, QStringLiteral("eng"));
        QCOMPARE(result.manifest.sources[1].isoLanguage, QStringLiteral("rus"));
        QCOMPARE(result.manifest.sources[0].bytes, qint64(1234));
        QCOMPARE(result.manifest.manifestSha256.size(), qsizetype(64));
    }

    void evaluationManifestRejects_data()
    {
        QTest::addColumn<QByteArray>("data");
        QTest::addColumn<int>("error");
        auto root = [](const char *key, const QJsonValue &value) {
            QJsonObject o = evalManifestObject();
            if (value.isUndefined())
                o.remove(QString::fromLatin1(key));
            else
                o.insert(QString::fromLatin1(key), value);
            return encode(o);
        };
        const QJsonValue undefined(QJsonValue::Undefined);
        QTest::newRow("not json") << QByteArray("[") << int(EvalManifestError::InvalidJson);
        QTest::newRow("too large") << QByteArray(kMaxEvalManifestBytes + 1, ' ')
                                   << int(EvalManifestError::TooLarge);
        QTest::newRow("schema 2") << root("schema_version", 2) << int(EvalManifestError::SchemaVersion);
        QTest::newRow("training status") << root("status", QStringLiteral("approved-local-experiment"))
                                         << int(EvalManifestError::StatusNotApproved);
        QTest::newRow("scope") << root("scope", QStringLiteral("release")) << int(EvalManifestError::ScopeMismatch);
        QTest::newRow("release true") << root("release_approved", true) << int(EvalManifestError::ReleaseApproved);
        QTest::newRow("release missing") << root("release_approved", undefined)
                                         << int(EvalManifestError::ReleaseApproved);
        QTest::newRow("no sources") << root("evaluation_sources", undefined) << int(EvalManifestError::BadSources);
        QJsonArray one;
        one.append(evalSource("en", "eng"));
        QTest::newRow("one source") << root("evaluation_sources", one) << int(EvalManifestError::BadSources);
        QJsonArray dup;
        dup.append(evalSource("en", "eng"));
        dup.append(evalSource("en", "eng"));
        QTest::newRow("duplicate") << root("evaluation_sources", dup) << int(EvalManifestError::BadSources);
        QJsonArray three = evalManifestObject().value(QStringLiteral("evaluation_sources")).toArray();
        three.append(evalSource("en", "eng"));
        QTest::newRow("three") << root("evaluation_sources", three) << int(EvalManifestError::BadSources);
        QTest::newRow("swapped iso") << encode(withEvalSource(1, "iso_language", QStringLiteral("rus")))
                                     << int(EvalManifestError::BadIsoLanguage);
        QTest::newRow("missing iso") << encode(withEvalSource(0, "iso_language", undefined))
                                     << int(EvalManifestError::BadIsoLanguage);
        QTest::newRow("upper hex") << encode(withEvalSource(0, "sha256", QString(64, QLatin1Char('A'))))
                                   << int(EvalManifestError::BadSourceField);
        QTest::newRow("bytes zero") << encode(withEvalSource(0, "bytes", 0)) << int(EvalManifestError::BadSourceField);
        QTest::newRow("bytes over limit") << encode(withEvalSource(0, "bytes", double(kMaxEvalSourceBytes + 1)))
                                          << int(EvalManifestError::BadSourceField);
        QTest::newRow("bytes fraction") << encode(withEvalSource(0, "bytes", 1.5))
                                        << int(EvalManifestError::BadSourceField);
        QTest::newRow("notice traversal")
            << encode(withEvalSource(0, "notice", QStringLiteral("data/notices/../../secret")))
            << int(EvalManifestError::BadNoticePath);
        QTest::newRow("notice outside")
            << encode(withEvalSource(0, "notice", QStringLiteral("build/notice.txt")))
            << int(EvalManifestError::BadNoticePath);
    }

    void evaluationManifestRejects()
    {
        QFETCH(QByteArray, data);
        QFETCH(int, error);
        QCOMPARE(int(parseEvaluationManifest(data).error), error);
    }

    // ---- Tatoeba TSV ----

    void tsvAcceptsStrictRecords()
    {
        const QByteArray data = u8("7\teng\tTom said \"hi\".\n12\teng\tA non-breaking space.\n3\teng\tLast").toUtf8();
        const TsvParseResult result = parseTatoebaTsv(data, u"eng");
        QCOMPARE(result.error, TsvError::None);
        QCOMPARE(result.records.size(), std::size_t(3));
        QCOMPARE(result.records[0].id, qint64(7));
        QCOMPARE(result.records[0].text, QStringLiteral("Tom said \"hi\"."));
        QCOMPARE(result.records[2].id, qint64(3));
        // Предложение ровно 4096 кодовых единиц ASCII укладывается в 8192 байта.
        const QByteArray maxSentence = QByteArray("1\teng\t") + QByteArray(kMaxSentenceUtf16, 'a');
        QCOMPARE(parseTatoebaTsv(maxSentence, u"eng").error, TsvError::None);
    }

    void tsvRejects_data()
    {
        QTest::addColumn<QByteArray>("data");
        QTest::addColumn<QString>("iso");
        QTest::addColumn<int>("error");
        const QString eng = QStringLiteral("eng");
        const QString rus = QStringLiteral("rus");
        QTest::newRow("empty") << QByteArray() << eng << int(TsvError::Empty);
        QTest::newRow("only newline") << QByteArray("\n") << eng << int(TsvError::EmptyLine);
        QTest::newRow("invalid utf8") << QByteArray("1\teng\t\xC3\x28\n") << eng << int(TsvError::InvalidUtf8);
        QTest::newRow("truncated utf8") << QByteArray("1\teng\tab\xD0") << eng << int(TsvError::InvalidUtf8);
        QTest::newRow("bom") << QByteArray("\xEF\xBB\xBF" "1\teng\tx\n") << eng << int(TsvError::BadId);
        QTest::newRow("crlf") << QByteArray("1\teng\tx\r\n") << eng << int(TsvError::ControlCharacter);
        QTest::newRow("empty line") << QByteArray("1\teng\tx\n\n2\teng\ty\n") << eng << int(TsvError::EmptyLine);
        QTest::newRow("double trailing newline") << QByteArray("1\teng\tx\n\n") << eng << int(TsvError::EmptyLine);
        QTest::newRow("two columns") << QByteArray("1\teng\n") << eng << int(TsvError::BadColumns);
        QTest::newRow("four columns") << QByteArray("1\teng\tx\ty\n") << eng << int(TsvError::BadColumns);
        QTest::newRow("leading zero") << QByteArray("01\teng\tx\n") << eng << int(TsvError::BadId);
        QTest::newRow("zero id") << QByteArray("0\teng\tx\n") << eng << int(TsvError::BadId);
        QTest::newRow("negative id") << QByteArray("-1\teng\tx\n") << eng << int(TsvError::BadId);
        QTest::newRow("empty id") << QByteArray("\teng\tx\n") << eng << int(TsvError::BadId);
        QTest::newRow("19 digits") << QByteArray("1234567890123456789\teng\tx\n") << eng << int(TsvError::BadId);
        QTest::newRow("non-ascii digit") << u8("\xD9\xA1\teng\tx\n").toUtf8() << eng << int(TsvError::BadId);
        QTest::newRow("duplicate id") << QByteArray("5\teng\tx\n5\teng\ty\n") << eng << int(TsvError::DuplicateId);
        QTest::newRow("language") << QByteArray("1\trus\tx\n") << eng << int(TsvError::LanguageMismatch);
        QTest::newRow("language case") << QByteArray("1\tENG\tx\n") << eng << int(TsvError::LanguageMismatch);
        QTest::newRow("empty sentence") << QByteArray("1\teng\t\n") << eng << int(TsvError::EmptySentence);
        // 10 байт: "1", TAB, "eng", TAB, "a", NUL, "b", LF.
        QTest::newRow("nul") << QByteArray("1\teng\ta\0b\n", 10) << eng << int(TsvError::ControlCharacter);
        QTest::newRow("c0") << QByteArray("1\teng\ta\x01" "b\n") << eng << int(TsvError::ControlCharacter);
        QTest::newRow("del") << QByteArray("1\teng\ta\x7F\n") << eng << int(TsvError::ControlCharacter);
        QTest::newRow("c1 nel") << QByteArray("1\teng\ta\xC2\x85\n") << eng << int(TsvError::ControlCharacter);
        QTest::newRow("sentence too long")
            << QByteArray("1\teng\t") + QByteArray(kMaxSentenceUtf16 + 1, 'a') << eng
            << int(TsvError::SentenceTooLong);
        // 4096 кириллических букв = 8192 байта; с ID и языком строка длиннее предела.
        QByteArray cyrillic("1\trus\t");
        for (int i = 0; i < kMaxSentenceUtf16; ++i)
            cyrillic.append("\xD0\xB0");
        QTest::newRow("row bytes") << cyrillic << rus << int(TsvError::RowTooLong);
    }

    void tsvRejects()
    {
        QFETCH(QByteArray, data);
        QFETCH(QString, iso);
        QFETCH(int, error);
        const TsvParseResult result = parseTatoebaTsv(data, iso);
        QCOMPARE(int(result.error), error);
        QVERIFY(result.records.empty());
    }

    void tsvRejectsOversizedFile()
    {
        const QByteArray huge(kMaxEvalSourceBytes + 1, 'a');
        QCOMPARE(parseTatoebaTsv(huge, u"eng").error, TsvError::TooLarge);
    }

    void tsvRejectsNewlineFloodBeforeSplit()
    {
        const qsizetype limit = qsizetype(kMaxEvalRecords);
        // Ровно kMaxEvalRecords записей проходят подсчёт и отклоняются уже
        // построчной проверкой: с финальным '\n' и без него (хвост — запись).
        QCOMPARE(parseTatoebaTsv(QByteArray(limit, '\n'), u"eng").error, TsvError::EmptyLine);
        QCOMPARE(parseTatoebaTsv(QByteArray(limit - 1, '\n') + "x", u"eng").error, TsvError::EmptyLine);
        // На одну запись больше — отказ по числу записей, а не по пустой строке.
        const TsvParseResult flood = parseTatoebaTsv(QByteArray(limit + 1, '\n'), u"eng");
        QCOMPARE(flood.error, TsvError::TooManyRecords);
        QVERIFY(flood.records.empty());
        QCOMPARE(parseTatoebaTsv(QByteArray(limit, '\n') + "x", u"eng").error, TsvError::TooManyRecords);
        // Подсчёт идёт до декодирования: некорректный UTF-8 не меняет отказ.
        QCOMPARE(parseTatoebaTsv(QByteArray(limit + 1, '\n') + "\xC3", u"eng").error,
                 TsvError::TooManyRecords);
        QVERIFY(!tsvErrorText(TsvError::TooManyRecords).isEmpty());
    }

    void tsvAcceptsRecordLimit()
    {
        QByteArray data;
        for (qint64 id = 1; id <= kMaxEvalRecords; ++id) {
            data.append(QByteArray::number(id));
            data.append("\teng\tx\n");
        }
        const TsvParseResult atLimit = parseTatoebaTsv(data, u"eng");
        QCOMPARE(atLimit.error, TsvError::None);
        QCOMPARE(atLimit.records.size(), std::size_t(kMaxEvalRecords));
        // Ещё одна корректная запись без финального '\n' превышает предел.
        data.append(QByteArray::number(kMaxEvalRecords + 1));
        data.append("\teng\tx");
        const TsvParseResult over = parseTatoebaTsv(data, u"eng");
        QCOMPARE(over.error, TsvError::TooManyRecords);
        QVERIFY(over.records.empty());
    }

    void utf8LengthCountsBytes()
    {
        QCOMPARE(utf8Length(u"a"), qint64(1));
        QCOMPARE(utf8Length(u"é"), qint64(2));
        QCOMPARE(utf8Length(u"ж"), qint64(2));
        QCOMPARE(utf8Length(u"€"), qint64(3));
        QCOMPARE(utf8Length(u"\U0001F600"), qint64(4));
        for (const char *sample : {"abc", "\xD0\xB6\xD0\xB8", "\xE2\x82\xAC!", "\xF0\x9F\x98\x80x"})
            QCOMPARE(utf8Length(u8(sample)), qint64(qstrlen(sample)));
    }

    // ---- Выборка ----

    void selectionDeduplicatesAndRanks()
    {
        const QByteArray seed("fixture-seed");
        std::vector<SentenceRecord> records;
        for (qint64 id = 1; id <= 40; ++id)
            records.push_back({id * 3, QStringLiteral("s%1").arg(id % 30)});
        // Повтор текста с меньшим ID в конце входа: остаётся меньший ID.
        records.push_back({2, QStringLiteral("s5")});

        const SelectionResult result = selectSentences(records, seed, 10);
        QCOMPARE(result.report.records, qint64(41));
        QCOMPARE(result.report.uniqueTexts, qint64(30));
        QCOMPARE(result.report.duplicateTextRecords, qint64(11));
        QCOMPARE(result.report.selected, qint64(10));

        // Независимый расчёт ожидаемого порядка.
        QHash<QString, qint64> smallest;
        for (const SentenceRecord &r : records) {
            if (!smallest.contains(r.text) || r.id < smallest.value(r.text))
                smallest.insert(r.text, r.id);
        }
        QCOMPARE(smallest.value(QStringLiteral("s5")), qint64(2));
        std::vector<std::pair<QByteArray, qint64>> ranked;
        for (auto it = smallest.cbegin(); it != smallest.cend(); ++it) {
            QByteArray input = seed;
            input.append('\0');
            input.append(QByteArray::number(it.value()));
            ranked.push_back({QCryptographicHash::hash(input, QCryptographicHash::Sha256), it.value()});
        }
        std::sort(ranked.begin(), ranked.end());
        QByteArray idList;
        for (int i = 0; i < 10; ++i) {
            QCOMPARE(result.selected[std::size_t(i)].id, ranked[std::size_t(i)].second);
            idList.append(QByteArray::number(ranked[std::size_t(i)].second));
            idList.append('\n');
        }
        QCOMPARE(result.report.selectedIdsSha256,
                 QString::fromLatin1(QCryptographicHash::hash(idList, QCryptographicHash::Sha256).toHex()));
        QCOMPARE(sampleRankDigest(seed, ranked.front().second), ranked.front().first);

        // Порядок входа не влияет на результат.
        std::vector<SentenceRecord> reversed = records;
        std::reverse(reversed.begin(), reversed.end());
        const SelectionResult again = selectSentences(reversed, seed, 10);
        QCOMPARE(again.report.selectedIdsSha256, result.report.selectedIdsSha256);
        // Предел больше числа уникальных текстов: берутся все.
        QCOMPARE(selectSentences(records, seed, 1000).report.selected, qint64(30));
        QCOMPARE(selectSentences(records, seed, 0).report.selected, qint64(0));
    }

    // ---- Токенизация ----

    void tokenizationSplitsAndTrimsOnlyOuterSet()
    {
        TokenizeStats stats;
        const std::vector<QString> tokens = tokenizeSentence(
            u8("\xC2\xAB\xD0\x9F\xD1\x80\xD0\xB8\xD0\xB2\xD0\xB5\xD1\x82\xC2\xBB,\xC2\xA0"
               "don't  (stop)!\te.g. ... \xE2\x80\x94 k.,jdm \xE2\x80\x9E" "a\xE2\x80\x9C x-y z\xE2\x80\x83" "end."),
            &stats);
        const std::vector<QString> expected{u8("\xD0\x9F\xD1\x80\xD0\xB8\xD0\xB2\xD0\xB5\xD1\x82"),
                                            QStringLiteral("don't"),
                                            QStringLiteral("stop"),
                                            QStringLiteral("e.g"),
                                            u8("\xE2\x80\x94"),
                                            QStringLiteral("k.,jdm"),
                                            QStringLiteral("a"),
                                            QStringLiteral("x-y"),
                                            QStringLiteral("z"),
                                            QStringLiteral("end")};
        QCOMPARE(tokens.size(), expected.size());
        for (std::size_t i = 0; i < expected.size(); ++i)
            QCOMPARE(tokens[i], expected[i]);
        QCOMPARE(stats.pieces, qint64(11));
        QCOMPARE(stats.emptyAfterTrim, qint64(1));
        // «Привет», (stop)! e.g. „a“ end.
        QCOMPARE(stats.trimmed, qint64(5));
        // Регистр и внутренние символы сохраняются; апостроф и дефис не снимаются.
        QCOMPARE(tokenizeSentence(u"'Tom'-").front(), QStringLiteral("'Tom'-"));
        QVERIFY(tokenizeSentence(u"  \t ").empty());
        QCOMPARE(outerTrimCharacters().size(), qsizetype(18));
    }

    void nativeLetterOnly()
    {
        QVERIFY(isNativeLetterOnly(u"Tom", DetectorLanguage::En));
        QVERIFY(!isNativeLetterOnly(u"don't", DetectorLanguage::En));
        QVERIFY(!isNativeLetterOnly(u"x1", DetectorLanguage::En));
        QVERIFY(!isNativeLetterOnly(u8("\xD0\x81\xD0\xB6"), DetectorLanguage::En));
        QVERIFY(isNativeLetterOnly(u8("\xD0\x81\xD0\xB6"), DetectorLanguage::Ru));
        QVERIFY(!isNativeLetterOnly(u"Tom", DetectorLanguage::Ru));
        QVERIFY(!isNativeLetterOnly(u"", DetectorLanguage::En));
        QVERIFY(isNativeLetterOnly(QString(kMaxTokenKeys, QLatin1Char('a')), DetectorLanguage::En));
        QVERIFY(!isNativeLetterOnly(QString(kMaxTokenKeys + 1, QLatin1Char('a')), DetectorLanguage::En));
    }

    // ---- Решения методов ----

    void methodDecisions_data()
    {
        QTest::addColumn<int>("method");
        QTest::addColumn<CaseFeatures>("f");
        QTest::addColumn<int>("expected");
        const CaseFeatures shortHit = features(true, 3, true, true, true, true, C, C);
        const CaseFeatures unsupported = features(false, 0, false, false, false, false, A, A);
        const CaseFeatures observed = features(true, 5, false, false, true, false, C, C);
        const CaseFeatures alternative = features(true, 5, false, true, false, true, K, K);
        const CaseFeatures both = features(true, 5, false, false, true, true, C, C);
        const CaseFeatures oovCorrect = features(true, 5, false, false, false, false, C, C);
        const CaseFeatures oovKeep = features(true, 5, false, false, false, false, K, K);
        const CaseFeatures altTwoCorrect = features(true, 5, false, false, false, true, A, C);

        for (const Method m : kMethods) {
            QTest::addRow("%s short", methodName(m)) << int(m) << shortHit << int(m == Method::AlwaysKeep ? K : A);
            QTest::addRow("%s unsupported", methodName(m))
                << int(m) << unsupported << int(m == Method::AlwaysKeep ? K : A);
        }
        QTest::newRow("exactTrain uses train only") << int(Method::ExactTrain) << alternative << int(C);
        QTest::newRow("exactTrain full-only hit abstains") << int(Method::ExactTrain) << observed << int(A);
        QTest::newRow("exactFull observed") << int(Method::ExactFull) << observed << int(K);
        QTest::newRow("exactFull alternative") << int(Method::ExactFull) << alternative << int(C);
        QTest::newRow("exactFull both keeps") << int(Method::ExactFull) << both << int(K);
        QTest::newRow("exactFull oov") << int(Method::ExactFull) << oovCorrect << int(A);
        QTest::newRow("singleD passes through") << int(Method::SingleD) << oovCorrect << int(C);
        QTest::newRow("twoScore passes through") << int(Method::TwoScore) << oovKeep << int(K);
        QTest::newRow("cascade observed beats model") << int(Method::ExactFullThenTwoScore) << observed << int(K);
        QTest::newRow("cascade alternative beats model")
            << int(Method::ExactFullThenTwoScore) << alternative << int(C);
        QTest::newRow("cascade falls back to twoScore")
            << int(Method::ExactFullThenTwoScore) << oovCorrect << int(C);
        QTest::newRow("cascade fallback keep") << int(Method::ExactFullThenTwoScore) << oovKeep << int(K);
        QTest::newRow("confirmed observed") << int(Method::TwoScoreConfirmedByExactFull) << observed << int(K);
        QTest::newRow("confirmed both") << int(Method::TwoScoreConfirmedByExactFull) << altTwoCorrect << int(C);
        QTest::newRow("confirmed needs model")
            << int(Method::TwoScoreConfirmedByExactFull) << alternative << int(A);
        QTest::newRow("confirmed needs lexicon")
            << int(Method::TwoScoreConfirmedByExactFull) << oovCorrect << int(A);
        QTest::newRow("confirmed model keep is abstain")
            << int(Method::TwoScoreConfirmedByExactFull) << oovKeep << int(A);
    }

    void methodDecisions()
    {
        QFETCH(int, method);
        QFETCH(CaseFeatures, f);
        QFETCH(int, expected);
        QCOMPARE(int(decideMethod(Method(method), f)), expected);
    }

    void outcomesNeverRecoverAmbiguous()
    {
        QCOMPARE(classifyOutcome(C, true, false, true), Outcome::Recovered);
        QCOMPARE(classifyOutcome(C, true, false, false), Outcome::WrongConversion);
        QCOMPARE(classifyOutcome(C, false, false, true), Outcome::FalseChange);
        QCOMPARE(classifyOutcome(C, true, true, true), Outcome::AmbiguousChange);
        QCOMPARE(classifyOutcome(C, false, true, true), Outcome::AmbiguousChange);
        QCOMPARE(classifyOutcome(K, true, true, true), Outcome::Keep);
        QCOMPARE(classifyOutcome(A, false, false, true), Outcome::Abstain);
        QVERIFY(!isUnsafe(Outcome::Recovered));
        QVERIFY(isUnsafe(Outcome::AmbiguousChange));

        SliceCounts slice;
        slice.add(true, true, Outcome::AmbiguousChange);
        slice.add(true, false, Outcome::Recovered);
        slice.add(false, false, Outcome::FalseChange);
        slice.add(false, true, Outcome::Keep);
        QCOMPARE(slice.wrong.recovered, qint64(1));
        QCOMPARE(slice.ambiguousWrong.changes(), qint64(1));
        QCOMPARE(slice.ambiguousWrong.recovered, qint64(0));
        QCOMPARE(slice.unsafe(), qint64(2));
    }

    void naturalCasesFromArtificialTokens()
    {
        DetectorFixture fx;
        makeFixture(fx);
        QVERIFY(fx.context.isValid());
        const std::array<std::vector<QString>, 2> tokens{{
            {QStringLiteral("House"), QStringLiteral("house"), QStringLiteral("don't"), QStringLiteral("hello"),
             QStringLiteral("World"), QStringLiteral("x1")},
            {u8("\xD0\x9C\xD0\xB8\xD1\x80"), u8("\xD0\xB4\xD0\xBE\xD0\xBC")},
        }};
        const NaturalCases natural = buildNaturalCases(tokens, fx.context);
        const NaturalTokenStats &en = natural.stats[0];
        const NaturalTokenStats &ru = natural.stats[1];
        QCOMPARE(en.tokens, qint64(6));
        QCOMPARE(en.wrongCopies, qint64(4));
        QCOMPARE(en.nativeLetterOnly, qint64(4));
        QCOMPARE(en.supportedNotLetterOnly, qint64(1));
        QCOMPARE(en.unsupportedCharacter, qint64(1));
        QCOMPARE(en.wrongCanonicalMismatches, qint64(0));
        // "House" и "house" — один тип по каноническим клавишам.
        QCOMPARE(en.validTypes, qint64(5));
        QCOMPARE(en.wrongTypes, qint64(3));
        QCOMPARE(en.ambiguous, qint64(1));
        QCOMPARE(en.hadUppercase, qint64(2));
        QCOMPARE(ru.wrongCopies, qint64(2));
        QCOMPARE(natural.cases.size(), std::size_t(6 + 4 + 2 + 2));
        qint64 representatives = 0;
        for (const EvalCase &c : natural.cases)
            representatives += c.typeRepresentative ? 1 : 0;
        QCOMPARE(representatives, en.validTypes + en.wrongTypes + ru.validTypes + ru.wrongTypes);

        const EvalCase &houseValid = natural.cases[0];
        const EvalCase &houseWrong = natural.cases[1];
        QVERIFY(!houseValid.wrong);
        QCOMPARE(houseValid.layout, SourceLayout::Us);
        QVERIFY(houseWrong.wrong);
        QCOMPARE(houseWrong.layout, SourceLayout::Ru);
        QCOMPARE(houseWrong.length, 5);
        // Регистр сохраняется в wrong-копии и восстанавливается преобразованием.
        QCOMPARE(houseWrong.text, convertLayout(QStringLiteral("House"), LayoutDirection::UsToRu));
        QVERIFY(houseWrong.text.at(0).isUpper());
        QVERIFY(houseWrong.conversionMatches);
        QVERIFY(houseValid.features.observedFull);
        QVERIFY(houseValid.features.observedTrain);
        QVERIFY(houseWrong.features.alternativeFull);
        QCOMPARE(decideMethod(Method::ExactFull, houseValid.features), K);
        QCOMPARE(decideMethod(Method::ExactFull, houseWrong.features), C);
        QCOMPARE(classifyOutcome(C, houseWrong.wrong, houseWrong.ambiguous, houseWrong.conversionMatches),
                 Outcome::Recovered);
        QVERIFY(natural.cases[2].typeRepresentative == false);

        for (const EvalCase &c : natural.cases) {
            if (c.text == QStringLiteral("hello") || (c.wrong && c.length == 5 && c.features.observedFull
                                                       && c.features.alternativeFull)) {
                QVERIFY(c.ambiguous);
            }
            if (c.text == QStringLiteral("World")) {
                QVERIFY(c.features.observedFull);
                QVERIFY(!c.features.observedTrain);
            }
            // Живой путь совпадает с вычисленным решением для каждого метода.
            for (const Method m : kMethods)
                QCOMPARE(decideLive(m, c.text, c.layout, fx.context), decideMethod(m, c.features));
        }
        // Неоднозначная wrong-копия никогда не считается восстановленной.
        for (const EvalCase &c : natural.cases) {
            if (c.ambiguous && c.wrong) {
                const Outcome o = classifyOutcome(decideMethod(Method::ExactFull, c.features), c.wrong,
                                                  c.ambiguous, c.conversionMatches);
                QVERIFY(o != Outcome::Recovered);
            }
        }
        QCOMPARE(fullMembership(houseValid.features), Membership::ObservedOnly);
        QCOMPARE(fullMembership(houseWrong.features), Membership::AlternativeOnly);
        QCOMPARE(trainMembership(houseWrong.features), Membership::AlternativeOnly);
    }

    void expectedPrecisionFormula()
    {
        const std::optional<double> p = expectedPrecision(0.01, 0.9, 0.0, 0.001);
        QVERIFY(p.has_value());
        QVERIFY(qAbs(*p - 0.009 / (0.009 + 0.99 * 0.001)) < 1e-12);
        QCOMPARE(*expectedPrecision(0.05, 0.5, 0.0, 0.0), 1.0);
        const std::optional<double> withUnsafe = expectedPrecision(0.5, 0.5, 0.5, 0.0);
        QVERIFY(withUnsafe.has_value());
        QCOMPARE(*withUnsafe, 0.5);
        QVERIFY(!expectedPrecision(0.01, 0.0, 0.0, 0.0).has_value());
        QVERIFY(!expectedPrecision(-0.1, 0.5, 0.0, 0.1).has_value());
        QVERIFY(!expectedPrecision(0.01, std::numeric_limits<double>::quiet_NaN(), 0.0, 0.1).has_value());

        SliceCounts overall;
        overall.add(true, false, Outcome::Recovered);
        overall.add(true, false, Outcome::Abstain);
        overall.add(false, false, Outcome::Keep);
        overall.add(false, true, Outcome::AmbiguousChange);
        const QJsonObject json = expectedPrecisionJson(overall);
        QVERIFY(json.value(QStringLiteral("available")).toBool());
        QCOMPARE(json.value(QStringLiteral("validChangeRateF")).toDouble(), 0.5);
        QCOMPARE(json.value(QStringLiteral("recoveredRateR")).toDouble(), 0.5);
    }

    void pairedDeltaCountsBothDirections()
    {
        const std::vector<Outcome> method{Outcome::Recovered, Outcome::Abstain, Outcome::FalseChange,
                                          Outcome::Keep, Outcome::Recovered, Outcome::AmbiguousChange};
        const std::vector<Outcome> baseline{Outcome::Abstain, Outcome::Recovered, Outcome::Keep,
                                            Outcome::FalseChange, Outcome::Recovered, Outcome::AmbiguousChange};
        const PairedDelta d = pairedDelta(method, baseline, {0, 1, 2, 3, 4, 5, 99});
        QCOMPARE(d.cases, qint64(6));
        QCOMPARE(d.addedRecoveries, qint64(1));
        QCOMPARE(d.lostRecoveries, qint64(1));
        QCOMPARE(d.bothRecovered, qint64(1));
        QCOMPARE(d.addedUnsafe, qint64(1));
        QCOMPARE(d.preventedUnsafe, qint64(1));
        QCOMPARE(d.bothUnsafe, qint64(1));
        const PairedDelta subset = pairedDelta(method, baseline, {0});
        QCOMPARE(subset.addedRecoveries, qint64(1));
        QCOMPARE(subset.addedUnsafe, qint64(0));
    }

    // ---- Испытание ----

    void noiseIsDeterministicAndExcludesLexicon()
    {
        const QByteArray seed("fixture-noise");
        QSet<KeySequence> excludedKeys;
        qint64 probeCounter = 0;
        const NoiseResult probe = generateNoiseKeys(seed, 10, 4, 16, probeCounter, 1000, {});
        QVERIFY(probe.complete);
        excludedKeys.insert(probe.keys[0]);
        excludedKeys.insert(probe.keys[3]);
        auto excluded = [&excludedKeys](const KeySequence &k) { return excludedKeys.contains(k); };

        qint64 counterA = 0;
        const NoiseResult a = generateNoiseKeys(seed, 64, 4, 16, counterA, 10000, excluded);
        qint64 counterB = 0;
        const NoiseResult b = generateNoiseKeys(seed, 64, 4, 16, counterB, 10000, excluded);
        QVERIFY(a.complete);
        QVERIFY(a.keys == b.keys);
        QCOMPARE(counterA, a.attempts);
        QCOMPARE(a.rejectedExcluded, qint64(2));
        const QSet<KeySequence> unique(a.keys.begin(), a.keys.end());
        QCOMPARE(unique.size(), qsizetype(64));
        for (const KeySequence &k : a.keys) {
            QVERIFY(k.size() >= 4 && k.size() <= 16);
            QVERIFY(isValidCanonicalKey(k));
            QVERIFY(!excludedKeys.contains(k));
            for (const SourceLayout layout : {SourceLayout::Us, SourceLayout::Ru})
                QCOMPARE(canon(keysToLayoutText(k, layout), layout), k);
        }
        // Первый ключ получен из SHA-256(seed || 0 || "1"): "0" исключён.
        QByteArray input = seed;
        input.append('\0');
        input.append('1');
        const QByteArray digest = QCryptographicHash::hash(input, QCryptographicHash::Sha256);
        const int length = 4 + int(static_cast<unsigned char>(digest.at(0)) % 13U);
        QCOMPARE(a.keys.front().size(), qsizetype(length));
        QCOMPARE(int(static_cast<unsigned char>(a.keys.front().at(0))),
                 int(static_cast<unsigned char>(digest.at(1)) % 33U));

        // Продолжение счётчика даёт другой поток.
        const NoiseResult next = generateNoiseKeys(seed, 8, 4, 16, counterA, 10000, excluded);
        QVERIFY(next.complete);
        QVERIFY(next.keys.front() != a.keys.front());

        // Граница попыток.
        qint64 counterC = 0;
        const NoiseResult bounded =
            generateNoiseKeys(seed, 5, 4, 16, counterC, 50, [](const KeySequence &) { return true; });
        QVERIFY(!bounded.complete);
        QCOMPARE(bounded.attempts, qint64(50));
        QCOMPARE(bounded.rejectedExcluded, qint64(50));
        qint64 counterD = 0;
        QVERIFY(!generateNoiseKeys(seed, 1, 4, 32, counterD, 10, {}).complete);
        QCOMPARE(counterD, qint64(0));
    }

    void challengeFixturesAreArtificialAndLabelled()
    {
        const std::vector<ChallengeFixture> fixtures = challengeFixtures();
        QSet<QString> categories;
        for (const ChallengeFixture &f : fixtures) {
            QVERIFY(!f.text.isEmpty());
            categories.insert(f.category);
            if (f.category == QStringLiteral("url") || f.category == QStringLiteral("email"))
                QVERIFY(f.text.contains(QStringLiteral("example.invalid")));
            if (f.category == QStringLiteral("mixedScript"))
                QCOMPARE(canonicalizeToken(f.text, f.layout).status, CanonicalStatus::MixedScript);
        }
        for (const char *name : {"url", "email", "command", "path", "code", "hash", "number", "mixedScript",
                                 "name", "acronym"})
            QVERIFY2(categories.contains(QString::fromLatin1(name)), name);

        DetectorFixture fx;
        makeFixture(fx);
        // Неоднозначный ключ: в обоих полных словарях; exactFull оставляет его.
        const ChallengeCase ambiguousUs = makeChallengeCase(QStringLiteral("ambiguous"), SourceLayout::Us,
                                                            u"hello", fx.context);
        QVERIFY(ambiguousUs.ambiguous);
        QCOMPARE(decideMethod(Method::ExactFull, ambiguousUs.features), K);
        const ChallengeCase ambiguousRu = makeChallengeCase(
            QStringLiteral("ambiguous"), SourceLayout::Ru,
            keysToLayoutText(canon(QStringLiteral("hello"), SourceLayout::Us), SourceLayout::Ru), fx.context);
        QVERIFY(ambiguousRu.ambiguous);
        QCOMPARE(classifyOutcome(C, false, ambiguousRu.ambiguous, false), Outcome::AmbiguousChange);
    }

    // ---- Файловые границы ----

    void pathContainment()
    {
        QVERIFY(isStrictlyInside(QStringLiteral("/a/b"), QStringLiteral("/a")));
        QVERIFY(isStrictlyInside(QStringLiteral("/a/b/c"), QStringLiteral("/a/")));
        QVERIFY(!isStrictlyInside(QStringLiteral("/a"), QStringLiteral("/a")));
        QVERIFY(!isStrictlyInside(QStringLiteral("/ab"), QStringLiteral("/a")));
        QVERIFY(!isStrictlyInside(QString(), QStringLiteral("/a")));
    }

    void artifactDirectoryStaysInsideBuild()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        const QString root = QFileInfo(tmp.path()).canonicalFilePath();
        QDir dir(root);
        QVERIFY(dir.mkpath(QStringLiteral("build/probe")));
        QVERIFY(dir.mkpath(QStringLiteral("outside")));
        QString error;

        const QString inside = resolveArtifactDir(root, root + QStringLiteral("/build/probe"), error);
        QCOMPARE(inside, root + QStringLiteral("/build/probe"));
        QVERIFY(resolveArtifactDir(root, root + QStringLiteral("/build"), error).isEmpty());
        QVERIFY(resolveArtifactDir(root, root + QStringLiteral("/outside"), error).isEmpty());
        QVERIFY(resolveArtifactDir(root, root + QStringLiteral("/build/missing"), error).isEmpty());
        QVERIFY(resolveArtifactDir(root, root + QStringLiteral("/build/probe/../../outside"), error).isEmpty());
        QVERIFY(QFile::link(root + QStringLiteral("/outside"), root + QStringLiteral("/build/escape")));
        QVERIFY(resolveArtifactDir(root, root + QStringLiteral("/build/escape"), error).isEmpty());

        QVERIFY(writeArtifactFile(inside, QStringLiteral("model.bin"), QByteArray("abc"), error));
        QFile written(inside + QStringLiteral("/model.bin"));
        QVERIFY(written.open(QIODevice::ReadOnly));
        QCOMPARE(written.readAll(), QByteArray("abc"));
        written.close();
        QVERIFY(writeArtifactFile(inside, QStringLiteral("model.bin"), QByteArray("xyz"), error));
        QVERIFY(!writeArtifactFile(inside, QStringLiteral("../escape.bin"), QByteArray("x"), error));
        QVERIFY(!writeArtifactFile(inside, QStringLiteral("a/b"), QByteArray("x"), error));
        QVERIFY(QFile::link(root + QStringLiteral("/outside/target.bin"), inside + QStringLiteral("/link.bin")));
        QVERIFY(!writeArtifactFile(inside, QStringLiteral("link.bin"), QByteArray("x"), error));
        QVERIFY(!QFileInfo::exists(root + QStringLiteral("/outside/target.bin")));
        QVERIFY(!writeArtifactFile(root + QStringLiteral("/build/escape"), QStringLiteral("x.bin"),
                                   QByteArray("x"), error));

        // build/ как symlink наружу отвергается.
        QTemporaryDir other;
        QVERIFY(other.isValid());
        const QString otherRoot = QFileInfo(other.path()).canonicalFilePath();
        QVERIFY(QDir(otherRoot).mkpath(QStringLiteral("real/sub")));
        QVERIFY(QFile::link(otherRoot + QStringLiteral("/real"), otherRoot + QStringLiteral("/build")));
        QVERIFY(resolveArtifactDir(otherRoot, otherRoot + QStringLiteral("/build/sub"), error).isEmpty());
    }

    void boundedReadEnforcesLimit()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        const QString path = tmp.path() + QStringLiteral("/data.bin");
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write(QByteArray(100, 'x')), qint64(100));
        file.close();
        QByteArray out;
        QString error;
        QVERIFY(readBoundedFile(path, 100, out, error));
        QCOMPARE(out.size(), qsizetype(100));
        QVERIFY(!readBoundedFile(path, 99, out, error));
        QVERIFY(out.isEmpty());
        QVERIFY(!readBoundedFile(tmp.path(), 1000, out, error));
        QVERIFY(!readBoundedFile(tmp.path() + QStringLiteral("/missing"), 1000, out, error));
    }
};

QTEST_GUILESS_MAIN(BenchmarkTests)

#include "benchmark_tests.moc"
