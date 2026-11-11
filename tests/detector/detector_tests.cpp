// Модульные тесты экспериментального n-граммного scorer'а (SYS-232) и
// helper'ов offline-эксперимента: канонизация, обучение, сериализация,
// решения, разбор .dic, разделение, манифест источников.
//
// Все fixtures искусственные и крошечные; внешние словари, сеть и файлы
// не используются.

#include "detectordataset.h"
#include "detectormanifest.h"
#include "layoutconverter.h"
#include "ngramdetector.h"

#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QTest>

#include <array>
#include <cmath>
#include <initializer_list>
#include <limits>
#include <optional>
#include <vector>

using namespace rasklad;
using namespace rasklad::experiment;

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

TokenScore makeScore(int keyCount, qint32 en, qint32 ru)
{
    TokenScore s;
    s.ok = true;
    s.keyCount = keyCount;
    s.steps = keyCount + 1;
    s.raw = {{en, ru}};
    return s;
}

QByteArray utf8(const QString &text)
{
    return text.toUtf8();
}

constexpr SourceLayout kInvalidLayout = static_cast<SourceLayout>(7);
constexpr DetectorLanguage kInvalidLanguage = static_cast<DetectorLanguage>(2);

// ---- Fixture манифеста ----

QByteArray fixtureEnBytes()
{
    return QByteArray("2\nab\ncd\n");
}

QByteArray fixtureRuBytes()
{
    return utf8(QStringLiteral("1\nёж\n"));
}

QJsonObject sourceObject(const char *language, const QByteArray &bytes)
{
    QJsonObject o;
    o.insert(QStringLiteral("language"), QString::fromLatin1(language));
    o.insert(QStringLiteral("sha256"), sha256Hex(bytes));
    o.insert(QStringLiteral("bytes"), qint64(bytes.size()));
    o.insert(QStringLiteral("notice"), QStringLiteral("data/notices/fixture.README.txt"));
    o.insert(QStringLiteral("notice_sha256"), QString(64, QLatin1Char('0')));
    return o;
}

QJsonObject manifestObject()
{
    QJsonObject root;
    root.insert(QStringLiteral("schema_version"), 1);
    root.insert(QStringLiteral("commit"), QString(40, QLatin1Char('a')));
    root.insert(QStringLiteral("scope"), QStringLiteral("local-experiment-only"));
    root.insert(QStringLiteral("status"), QStringLiteral("approved-local-experiment"));
    root.insert(QStringLiteral("release_approved"), false);
    QJsonArray sources;
    sources.append(sourceObject("ru", fixtureRuBytes()));
    sources.append(sourceObject("en", fixtureEnBytes()));
    root.insert(QStringLiteral("training_sources"), sources);
    return root;
}

QByteArray encode(const QJsonObject &object)
{
    return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

QJsonObject withRoot(const char *key, const QJsonValue &value)
{
    QJsonObject root = manifestObject();
    root.insert(QString::fromLatin1(key), value);
    return root;
}

QJsonObject withoutRoot(const char *key)
{
    QJsonObject root = manifestObject();
    root.remove(QString::fromLatin1(key));
    return root;
}

QJsonObject withSource(qsizetype index, const char *key, const QJsonValue &value)
{
    QJsonObject root = manifestObject();
    QJsonArray sources = root.value(QStringLiteral("training_sources")).toArray();
    QJsonObject source = sources.at(index).toObject();
    source.insert(QString::fromLatin1(key), value);
    sources.replace(index, source);
    root.insert(QStringLiteral("training_sources"), sources);
    return root;
}

} // namespace

class DetectorTests : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    // ---- Канонизация ----
    void canonicalPunctuationWord();
    void canonicalUppercaseAndShiftPunctuation();
    void canonicalRussianLetterKeys();
    void canonicalRejects_data();
    void canonicalRejects();
    void canonicalLimits();
    void canonicalInvalidLayout();
    void keysToUsBaseRejectsInvalid();

    // ---- Обучение и оценка ----
    void trainSkipsInvalidKeys();
    void handComputedScore();
    void scoreRejectsInvalidInput();
    void invalidLanguageDoesNotIndex();

    // ---- Сериализация ----
    void serializationRoundTripAndHeader();
    void serializationDeterministic();
    void deserializeRejectsTruncationAndSize();
    void deserializeRejectsHeaderFields_data();
    void deserializeRejectsHeaderFields();
    void deserializeRejectsInvalidWeights();

    // ---- Решения ----
    void singleDBoundaries();
    void twoScoreBoundaries();
    void disabledAndInvalidThresholds();
    void shortTokensAbstain();
    void forgedScoresRejected_data();
    void forgedScoresRejected();
    void rawLowerBoundIsAccepted();
    void invalidSourceLayoutDecision();

    // ---- Разбор .dic ----
    void dicBasicAndTrailingNewline();
    void dicBomAndCrlf();
    void dicFiltering();
    void dicRussian();
    void dicCountMismatch_data();
    void dicCountMismatch();
    void dicInvalidUtf8_data();
    void dicInvalidUtf8();
    void dicBadHeaders_data();
    void dicBadHeaders();
    void dicTooLarge();
    void dicTooManyRecords();
    void dicInvalidLanguage();

    // ---- Разделение ----
    void splitGroupAndBucketFormula();
    void splitDistribution();
    void splitKeepsLanguageVariantsAndCopiesTogether();
    void leakFreeForHashedSplit();
    void leaksDetected();

    // ---- Манифест ----
    void manifestValid();
    void manifestRejects_data();
    void manifestRejects();
    void manifestNoticePaths_data();
    void manifestNoticePaths();
    void manifestResolveNotice();
    void manifestBytesMatch();
};

// ---- Канонизация ----

void DetectorTests::canonicalPunctuationWord()
{
    // "k.,jdm" — «любовь», набранная в us: пунктуация на буквенных клавишах
    // не делит токен.
    const CanonicalToken us = canonicalizeToken(QStringLiteral("k.,jdm"), SourceLayout::Us);
    QCOMPARE(int(us.status), int(CanonicalStatus::Ok));
    QCOMPARE(us.keys.size(), qsizetype(6));
    QVERIFY(!us.hadUppercase);

    const CanonicalToken ru = canonicalizeToken(QStringLiteral("любовь"), SourceLayout::Ru);
    QCOMPARE(int(ru.status), int(CanonicalStatus::Ok));
    QCOMPARE(ru.keys, us.keys);
    QCOMPARE(keysToUsBase(us.keys), QStringLiteral("k.,jdm"));
    QCOMPARE(us.keys, keys({20, 32, 31, 19, 15, 30}));
}

void DetectorTests::canonicalUppercaseAndShiftPunctuation()
{
    const KeySequence base = canon(QStringLiteral("k.,jdm"), SourceLayout::Us);

    const CanonicalToken shifted = canonicalizeToken(QStringLiteral("K><JDM"), SourceLayout::Us);
    QCOMPARE(int(shifted.status), int(CanonicalStatus::Ok));
    QCOMPARE(shifted.keys, base);
    QVERIFY(shifted.hadUppercase);

    const CanonicalToken mixedCase = canonicalizeToken(QStringLiteral("k.<jDm"), SourceLayout::Us);
    QCOMPARE(mixedCase.keys, base);
    QVERIFY(mixedCase.hadUppercase);

    const CanonicalToken ruUpper = canonicalizeToken(QStringLiteral("ЛЮБОВЬ"), SourceLayout::Ru);
    QCOMPARE(ruUpper.keys, base);
    QVERIFY(ruUpper.hadUppercase);

    // Shift-уровень us-пунктуации сводится к той же клавише.
    QCOMPARE(canon(QStringLiteral("{}:\"<>~"), SourceLayout::Us),
             canon(QStringLiteral("[];',.`"), SourceLayout::Us));
}

void DetectorTests::canonicalRussianLetterKeys()
{
    // Буквы ru на клавишах us-пунктуации, включая ё.
    QCOMPARE(canon(QStringLiteral("хъжэбюё"), SourceLayout::Ru),
             canon(QStringLiteral("[];',.`"), SourceLayout::Us));
    QCOMPARE(canon(QStringLiteral("ё"), SourceLayout::Ru), keys({0}));
    const CanonicalToken upper = canonicalizeToken(QStringLiteral("Ёж"), SourceLayout::Ru);
    QCOMPARE(int(upper.status), int(CanonicalStatus::Ok));
    QVERIFY(upper.hadUppercase);
    QCOMPARE(upper.keys, canon(QStringLiteral("`;"), SourceLayout::Us));
}

void DetectorTests::canonicalRejects_data()
{
    QTest::addColumn<QString>("token");
    QTest::addColumn<int>("layout");
    QTest::addColumn<int>("status");

    const int us = int(SourceLayout::Us);
    const int ru = int(SourceLayout::Ru);
    const int mixed = int(CanonicalStatus::MixedScript);
    const int unsupported = int(CanonicalStatus::UnsupportedCharacter);

    QTest::newRow("us-cyrillic") << QStringLiteral("loveд") << us << mixed;
    QTest::newRow("us-ukrainian-i") << QStringLiteral("і") << us << mixed;
    QTest::newRow("ru-latin") << QStringLiteral("любоvь") << ru << mixed;
    QTest::newRow("us-digit") << QStringLiteral("abc1") << us << unsupported;
    QTest::newRow("us-hyphen") << QStringLiteral("a-b") << us << unsupported;
    QTest::newRow("us-typographic-apostrophe") << QStringLiteral("don’t") << us << unsupported;
    QTest::newRow("us-slash") << QStringLiteral("a/b") << us << unsupported;
    QTest::newRow("us-backslash") << QStringLiteral("a\\b") << us << unsupported;
    QTest::newRow("us-space") << QStringLiteral("a b") << us << unsupported;
    QTest::newRow("us-combining") << QStringLiteral("é") << us << unsupported;
    QTest::newRow("us-non-bmp") << QStringLiteral("a\U0001F600") << us << unsupported;
    QTest::newRow("ru-digit") << QStringLiteral("ёж1") << ru << unsupported;
    QTest::newRow("ru-hyphen") << QStringLiteral("ё-ж") << ru << unsupported;
    QTest::newRow("ru-combining") << QStringLiteral("ё") << ru << unsupported;
    QTest::newRow("ru-ukrainian-i") << QStringLiteral("і") << ru << unsupported;
    QTest::newRow("ru-ascii-punctuation") << QStringLiteral("ж;") << ru << unsupported;
}

void DetectorTests::canonicalRejects()
{
    QFETCH(QString, token);
    QFETCH(int, layout);
    QFETCH(int, status);
    const CanonicalToken result = canonicalizeToken(token, static_cast<SourceLayout>(layout));
    QCOMPARE(int(result.status), status);
    QVERIFY(result.keys.isEmpty());
}

void DetectorTests::canonicalLimits()
{
    QCOMPARE(int(canonicalizeToken(QString(), SourceLayout::Us).status), int(CanonicalStatus::Empty));
    QCOMPARE(int(canonicalizeToken(QString(), SourceLayout::Ru).status), int(CanonicalStatus::Empty));

    const CanonicalToken one = canonicalizeToken(QStringLiteral("q"), SourceLayout::Us);
    QCOMPARE(int(one.status), int(CanonicalStatus::Ok));
    QCOMPARE(one.keys, keys({1}));

    const CanonicalToken max = canonicalizeToken(QString(kMaxTokenKeys, QLatin1Char('a')), SourceLayout::Us);
    QCOMPARE(int(max.status), int(CanonicalStatus::Ok));
    QCOMPARE(max.keys.size(), qsizetype(kMaxTokenKeys));

    const CanonicalToken over =
        canonicalizeToken(QString(kMaxTokenKeys + 1, QLatin1Char('a')), SourceLayout::Us);
    QCOMPARE(int(over.status), int(CanonicalStatus::TooLong));
    QVERIFY(over.keys.isEmpty());
}

void DetectorTests::canonicalInvalidLayout()
{
    QVERIFY(!isValidSourceLayout(kInvalidLayout));
    const CanonicalToken result = canonicalizeToken(QStringLiteral("hello"), kInvalidLayout);
    QCOMPARE(int(result.status), int(CanonicalStatus::InvalidLayout));
    QVERIFY(result.keys.isEmpty());
}

void DetectorTests::keysToUsBaseRejectsInvalid()
{
    QVERIFY(keysToUsBase(KeySequence()).isEmpty());
    QVERIFY(keysToUsBase(keys({33})).isEmpty());
    QVERIFY(keysToUsBase(KeySequence(kMaxTokenKeys + 1, char(1))).isEmpty());
    QCOMPARE(keysToUsBase(keys({0, 12, 31, 32})), QStringLiteral("`],."));
}

// ---- Обучение и оценка ----

void DetectorTests::trainSkipsInvalidKeys()
{
    const std::vector<KeySequence> clean{keys({1})};
    const std::vector<KeySequence> dirty{KeySequence(), keys({33}), KeySequence(kMaxTokenKeys + 1, char(1)),
                                         keys({1}), keys({1, 200})};
    qsizetype skipped = -1;
    const NgramModel model = NgramModel::train(dirty, {}, &skipped);
    QCOMPARE(skipped, qsizetype(4));
    QVERIFY(model.isValid());

    qsizetype cleanSkipped = -1;
    const NgramModel reference = NgramModel::train(clean, {}, &cleanSkipped);
    QCOMPARE(cleanSkipped, qsizetype(0));
    // Пропущенные последовательности не влияют на таблицы.
    QCOMPARE(model.serialize(), reference.serialize());
}

void DetectorTests::handComputedScore()
{
    // EN обучен на одной последовательности [q] (индекс 1), RU — пусто.
    // EN: unigram N(q)=1, N(EOS)=1 -> P = 1.5/19.5 -> round(8 ln) = -21;
    // bigram и trigram: единственный наблюдаемый символ контекста ->
    // 1.5/18.5 -> -20. Шаг: -21-20-20 = -61, два шага (q, EOS) -> -122.
    // RU: везде 0.5/17.5 = 1/35 -> -28, шаг -84, два шага -> -168.
    const NgramModel model = NgramModel::train({keys({1})}, {});
    QVERIFY(model.isValid());
    const std::vector<qint8> &en = model.table(DetectorLanguage::En);
    const int a = NgramModel::kAlphabet;
    const int bos = NgramModel::kBos;
    const int eos = NgramModel::kEos;
    QCOMPARE(int(en[1]), -21);
    QCOMPARE(int(en[eos]), -21);
    QCOMPARE(int(en[2]), -29); // unseen: 0.5/19.5
    QCOMPARE(int(en[NgramModel::kUnigramSize + bos * a + 1]), -20);
    QCOMPARE(int(en[NgramModel::kUnigramSize + 1 * a + eos]), -20);
    QCOMPARE(int(en[NgramModel::kUnigramSize + NgramModel::kBigramSize + (bos * a + bos) * a + 1]), -20);
    QCOMPARE(int(en[NgramModel::kUnigramSize + NgramModel::kBigramSize + (bos * a + 1) * a + eos]), -20);
    QCOMPARE(int(model.table(DetectorLanguage::Ru)[1]), -28);

    const TokenScore score = model.score(keys({1}));
    QVERIFY(score.ok);
    QVERIFY(isWellFormedScore(score));
    QCOMPARE(score.keyCount, 1);
    QCOMPARE(score.steps, 2);
    QCOMPARE(score.raw[0], -122);
    QCOMPARE(score.raw[1], -168);
    QCOMPARE(score.perStep(DetectorLanguage::En), -7.625);
    QCOMPARE(score.perStep(DetectorLanguage::Ru), -10.5);
}

void DetectorTests::scoreRejectsInvalidInput()
{
    const NgramModel empty;
    QVERIFY(!empty.isValid());
    QVERIFY(!empty.score(keys({1, 2, 3, 4})).ok);
    QVERIFY(empty.serialize().isEmpty());

    const NgramModel model = NgramModel::train({keys({1, 2, 3, 4})}, {keys({5, 6, 7, 8})});
    QVERIFY(!model.score(KeySequence()).ok);
    QVERIFY(!model.score(keys({33})).ok);
    QVERIFY(!model.score(KeySequence(kMaxTokenKeys + 1, char(1))).ok);
    const TokenScore bad = model.score(KeySequence());
    QVERIFY(std::isnan(bad.perStep(DetectorLanguage::En)));
}

void DetectorTests::invalidLanguageDoesNotIndex()
{
    QVERIFY(!isValidLanguage(kInvalidLanguage));
    QVERIFY(!isValidLanguage(static_cast<DetectorLanguage>(-1)));
    const TokenScore score = makeScore(4, -200, -100);
    QVERIFY(std::isnan(score.perStep(kInvalidLanguage)));
    QVERIFY(std::isnan(score.perStep(static_cast<DetectorLanguage>(-1))));

    const NgramModel model = NgramModel::train({keys({1})}, {keys({2})});
    QVERIFY(model.table(kInvalidLanguage).empty());
    QCOMPARE(model.table(DetectorLanguage::En).size(), std::size_t(NgramModel::kTableSize));
}

// ---- Сериализация ----

void DetectorTests::serializationRoundTripAndHeader()
{
    const NgramModel model = NgramModel::train({keys({1, 2, 3}), keys({4, 5})}, {keys({6, 7, 8, 9})});
    const QByteArray bytes = model.serialize();
    QCOMPARE(bytes.size(), qsizetype(NgramModel::kSerializedBytes));
    QCOMPARE(bytes.left(4), QByteArray("RSKN"));
    // Little-endian: version 1, scale 8, alphabet 35, tableSize 44135 = 0xAC67.
    QCOMPARE(bytes.mid(4, 4), QByteArray("\x01\x00\x00\x00", 4));
    QCOMPARE(bytes.mid(8, 4), QByteArray("\x08\x00\x00\x00", 4));
    QCOMPARE(bytes.mid(12, 4), QByteArray("\x23\x00\x00\x00", 4));
    QCOMPARE(bytes.mid(16, 4), QByteArray("\x67\xAC\x00\x00", 4));

    const std::optional<NgramModel> restored = NgramModel::deserialize(bytes);
    QVERIFY(restored.has_value());
    QVERIFY(restored->table(DetectorLanguage::En) == model.table(DetectorLanguage::En));
    QVERIFY(restored->table(DetectorLanguage::Ru) == model.table(DetectorLanguage::Ru));
    QCOMPARE(restored->serialize(), bytes);
    const TokenScore a = model.score(keys({1, 2, 3, 4}));
    const TokenScore b = restored->score(keys({1, 2, 3, 4}));
    QCOMPARE(a.raw[0], b.raw[0]);
    QCOMPARE(a.raw[1], b.raw[1]);
}

void DetectorTests::serializationDeterministic()
{
    const std::vector<KeySequence> en{keys({1, 2, 3}), keys({4, 5}), keys({1, 2})};
    const std::vector<KeySequence> enReordered{keys({1, 2}), keys({4, 5}), keys({1, 2, 3})};
    const std::vector<KeySequence> ru{keys({6, 7, 8, 9})};
    const QByteArray first = NgramModel::train(en, ru).serialize();
    const QByteArray second = NgramModel::train(en, ru).serialize();
    const QByteArray reordered = NgramModel::train(enReordered, ru).serialize();
    QCOMPARE(first, second);
    QCOMPARE(first, reordered);
    QVERIFY(first != NgramModel::train(ru, en).serialize());
}

void DetectorTests::deserializeRejectsTruncationAndSize()
{
    const QByteArray bytes = NgramModel::train({keys({1, 2})}, {keys({3, 4})}).serialize();
    QVERIFY(!NgramModel::deserialize(QByteArray()).has_value());
    QVERIFY(!NgramModel::deserialize(bytes.left(NgramModel::kHeaderBytes)).has_value());
    QVERIFY(!NgramModel::deserialize(bytes.chopped(1)).has_value());
    QVERIFY(!NgramModel::deserialize(bytes + QByteArray(1, '\0')).has_value());

    QByteArray badMagic = bytes;
    badMagic[3] = 'X';
    QVERIFY(!NgramModel::deserialize(badMagic).has_value());
}

void DetectorTests::deserializeRejectsHeaderFields_data()
{
    QTest::addColumn<int>("offset");
    QTest::addColumn<int>("value");
    QTest::newRow("version") << 4 << 2;
    QTest::newRow("version-high-byte") << 7 << 1;
    QTest::newRow("scale") << 8 << 9;
    QTest::newRow("alphabet") << 12 << 36;
    QTest::newRow("table-size") << 16 << 0x68;
}

void DetectorTests::deserializeRejectsHeaderFields()
{
    QFETCH(int, offset);
    QFETCH(int, value);
    QByteArray bytes = NgramModel::train({keys({1, 2})}, {keys({3, 4})}).serialize();
    bytes[offset] = static_cast<char>(value);
    QVERIFY(!NgramModel::deserialize(bytes).has_value());
}

void DetectorTests::deserializeRejectsInvalidWeights()
{
    const QByteArray bytes = NgramModel::train({keys({1, 2})}, {keys({3, 4})}).serialize();
    const qsizetype enFirst = NgramModel::kHeaderBytes;
    const qsizetype ruLast = NgramModel::kSerializedBytes - 1;

    QByteArray positive = bytes;
    positive[enFirst] = static_cast<char>(1);
    QVERIFY(!NgramModel::deserialize(positive).has_value());

    QByteArray belowMin = bytes;
    belowMin[ruLast] = static_cast<char>(-128);
    QVERIFY(!NgramModel::deserialize(belowMin).has_value());

    QByteArray atMin = bytes;
    atMin[ruLast] = static_cast<char>(NgramModel::kMinQuantized);
    QVERIFY(NgramModel::deserialize(atMin).has_value());

    QByteArray zero = bytes;
    zero[enFirst] = '\0';
    QVERIFY(NgramModel::deserialize(zero).has_value());
}

// ---- Решения ----
// makeScore(4, -200, -100): steps 5, S_en = -200/40 = -5, S_ru = -100/40 = -2.5.

void DetectorTests::singleDBoundaries()
{
    const TokenScore s = makeScore(4, -200, -100);

    // us: observed en -5, alternative ru -2.5, D = 2.5.
    DetectorDecision d = decideSingleD(s, SourceLayout::Us, {true, 2.5});
    QCOMPARE(int(d.action), int(DecisionAction::Correct));
    QCOMPARE(int(d.reason), int(DecisionReason::AlternativePreferred));

    d = decideSingleD(s, SourceLayout::Us, {true, 2.75});
    QCOMPARE(int(d.action), int(DecisionAction::Abstain));
    QCOMPARE(int(d.reason), int(DecisionReason::Uncertain));

    // ru: observed ru -2.5, alternative en -5, D = -2.5.
    d = decideSingleD(s, SourceLayout::Ru, {true, 2.5});
    QCOMPARE(int(d.action), int(DecisionAction::Keep));
    QCOMPARE(int(d.reason), int(DecisionReason::ObservedPreferred));

    // D = 0 при gap 0: ни Correct, ни Keep.
    d = decideSingleD(makeScore(4, -100, -100), SourceLayout::Us, {true, 0.0});
    QCOMPARE(int(d.action), int(DecisionAction::Abstain));
    QCOMPARE(int(d.reason), int(DecisionReason::Uncertain));
}

void DetectorTests::twoScoreBoundaries()
{
    const TokenScore s = makeScore(4, -200, -100);

    // Все границы включительны.
    DetectorDecision d = decideTwoScore(s, SourceLayout::Us, {true, -2.5, -5.0, 2.5});
    QCOMPARE(int(d.action), int(DecisionAction::Correct));
    QCOMPARE(int(d.reason), int(DecisionReason::AlternativePreferred));

    d = decideTwoScore(s, SourceLayout::Us, {true, -2.4, -5.0, 2.5});
    QCOMPARE(int(d.action), int(DecisionAction::Abstain));
    QCOMPARE(int(d.reason), int(DecisionReason::Uncertain));

    d = decideTwoScore(s, SourceLayout::Us, {true, -2.5, -5.1, 2.5});
    QCOMPARE(int(d.action), int(DecisionAction::Abstain));

    d = decideTwoScore(s, SourceLayout::Us, {true, -2.5, -5.0, 2.6});
    QCOMPARE(int(d.action), int(DecisionAction::Abstain));

    // ru: observed -2.5 > maxObserved -5 и >= alternative -5 -> Keep.
    d = decideTwoScore(s, SourceLayout::Ru, {true, -2.5, -5.0, 2.5});
    QCOMPARE(int(d.action), int(DecisionAction::Keep));
    QCOMPARE(int(d.reason), int(DecisionReason::ObservedPreferred));

    // observed == maxObserved: Keep не выполняется.
    d = decideTwoScore(s, SourceLayout::Ru, {true, -2.5, -2.5, 2.5});
    QCOMPARE(int(d.action), int(DecisionAction::Abstain));
}

void DetectorTests::disabledAndInvalidThresholds()
{
    const TokenScore s = makeScore(4, -200, -100);
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double inf = std::numeric_limits<double>::infinity();

    const std::vector<SingleDThresholds> single{
        {false, 2.5}, SingleDThresholds{}, {true, nan}, {true, inf}, {true, -0.5}};
    for (const SingleDThresholds &t : single) {
        const DetectorDecision d = decideSingleD(s, SourceLayout::Us, t);
        QCOMPARE(int(d.action), int(DecisionAction::Abstain));
        QCOMPARE(int(d.reason), int(DecisionReason::Disabled));
    }

    const std::vector<TwoScoreThresholds> two{{false, -2.5, -5.0, 2.5}, TwoScoreThresholds{},
                                              {true, nan, -5.0, 2.5},   {true, -2.5, nan, 2.5},
                                              {true, -2.5, -5.0, nan},  {true, -inf, -5.0, 2.5},
                                              {true, -2.5, inf, 2.5},   {true, -2.5, -5.0, -1.0}};
    for (const TwoScoreThresholds &t : two) {
        const DetectorDecision d = decideTwoScore(s, SourceLayout::Us, t);
        QCOMPARE(int(d.action), int(DecisionAction::Abstain));
        QCOMPARE(int(d.reason), int(DecisionReason::Disabled));
    }
}

void DetectorTests::shortTokensAbstain()
{
    for (int n = kMinTokenKeys; n < kMinDecisionKeys; ++n) {
        const TokenScore s = makeScore(n, -10 * (n + 1), -1);
        QVERIFY(isWellFormedScore(s));
        DetectorDecision d = decideSingleD(s, SourceLayout::Us, {true, 0.0});
        QCOMPARE(int(d.action), int(DecisionAction::Abstain));
        QCOMPARE(int(d.reason), int(DecisionReason::TooShort));
        d = decideTwoScore(s, SourceLayout::Us, {true, -100.0, 0.0, 0.0});
        QCOMPARE(int(d.reason), int(DecisionReason::TooShort));
    }
    // Порог kMinDecisionKeys уже допускает решение.
    const DetectorDecision d =
        decideSingleD(makeScore(kMinDecisionKeys, -200, -100), SourceLayout::Us, {true, 0.0});
    QCOMPARE(int(d.action), int(DecisionAction::Correct));
}

void DetectorTests::forgedScoresRejected_data()
{
    QTest::addColumn<bool>("ok");
    QTest::addColumn<int>("keyCount");
    QTest::addColumn<int>("steps");
    QTest::addColumn<int>("rawEn");
    QTest::addColumn<int>("rawRu");

    const int floor5 = 5 * NgramModel::kMinStepRaw;
    QTest::newRow("not-ok") << false << 4 << 5 << -200 << -100;
    QTest::newRow("zero-keys") << true << 0 << 1 << -10 << -10;
    QTest::newRow("negative-keys") << true << -1 << 0 << 0 << 0;
    QTest::newRow("too-many-keys") << true << 33 << 34 << -200 << -100;
    QTest::newRow("steps-equal-keys") << true << 4 << 4 << -200 << -100;
    QTest::newRow("steps-too-large") << true << 4 << 6 << -200 << -100;
    QTest::newRow("steps-zero") << true << 4 << 0 << -200 << -100;
    QTest::newRow("positive-en") << true << 4 << 5 << 1 << -100;
    QTest::newRow("positive-ru") << true << 4 << 5 << -200 << 40;
    QTest::newRow("below-floor-en") << true << 4 << 5 << floor5 - 1 << -100;
    QTest::newRow("below-floor-ru") << true << 4 << 5 << -200 << floor5 - 1;
    QTest::newRow("int-min") << true << 4 << 5 << std::numeric_limits<int>::min() << -100;
}

void DetectorTests::forgedScoresRejected()
{
    QFETCH(bool, ok);
    QFETCH(int, keyCount);
    QFETCH(int, steps);
    QFETCH(int, rawEn);
    QFETCH(int, rawRu);
    TokenScore s;
    s.ok = ok;
    s.keyCount = keyCount;
    s.steps = steps;
    s.raw = {{rawEn, rawRu}};
    QVERIFY(!isWellFormedScore(s));
    for (const SourceLayout layout : {SourceLayout::Us, SourceLayout::Ru}) {
        DetectorDecision d = decideSingleD(s, layout, {true, 0.0});
        QCOMPARE(int(d.action), int(DecisionAction::Abstain));
        QCOMPARE(int(d.reason), int(DecisionReason::Unsupported));
        d = decideTwoScore(s, layout, {true, -100.0, 0.0, 0.0});
        QCOMPARE(int(d.action), int(DecisionAction::Abstain));
        QCOMPARE(int(d.reason), int(DecisionReason::Unsupported));
    }
}

void DetectorTests::rawLowerBoundIsAccepted()
{
    const qint32 floor5 = 5 * NgramModel::kMinStepRaw;
    QCOMPARE(int(NgramModel::kMinStepRaw), -381);
    const TokenScore s = makeScore(4, floor5, 0);
    QVERIFY(isWellFormedScore(s));
    const DetectorDecision d = decideSingleD(s, SourceLayout::Us, {true, 0.0});
    QCOMPARE(int(d.action), int(DecisionAction::Correct));

    // Реальный score всегда хорошо сформирован.
    const NgramModel model = NgramModel::train({keys({1, 2, 3, 4})}, {});
    QVERIFY(isWellFormedScore(model.score(KeySequence(kMaxTokenKeys, char(32)))));
}

void DetectorTests::invalidSourceLayoutDecision()
{
    const TokenScore s = makeScore(4, -200, -100);
    DetectorDecision d = decideSingleD(s, kInvalidLayout, {true, 0.0});
    QCOMPARE(int(d.action), int(DecisionAction::Abstain));
    QCOMPARE(int(d.reason), int(DecisionReason::Unsupported));
    d = decideTwoScore(s, kInvalidLayout, {true, -100.0, 0.0, 0.0});
    QCOMPARE(int(d.action), int(DecisionAction::Abstain));
    QCOMPARE(int(d.reason), int(DecisionReason::Unsupported));
}

// ---- Разбор .dic ----

void DetectorTests::dicBasicAndTrailingNewline()
{
    const DicParseResult withNewline =
        parseHunspellDic(QByteArray("3\nworld/AB\nhello\ncat\n"), DetectorLanguage::En);
    QCOMPARE(int(withNewline.error), int(DicError::None));
    QCOMPARE(withNewline.words,
             QStringList({QStringLiteral("cat"), QStringLiteral("hello"), QStringLiteral("world")}));
    QCOMPARE(withNewline.report.headerCount, qint64(3));
    QCOMPARE(withNewline.report.entryLines, qint64(3));
    QCOMPARE(withNewline.report.accepted, qint64(3));
    QVERIFY(!withNewline.report.bomStripped);

    // Без финального перевода строки — то же число записей.
    const DicParseResult withoutNewline =
        parseHunspellDic(QByteArray("3\nworld/AB\nhello\ncat"), DetectorLanguage::En);
    QCOMPARE(int(withoutNewline.error), int(DicError::None));
    QCOMPARE(withoutNewline.words, withNewline.words);

    const DicParseResult empty = parseHunspellDic(QByteArray("0\n"), DetectorLanguage::En);
    QCOMPARE(int(empty.error), int(DicError::None));
    QVERIFY(empty.words.isEmpty());
}

void DetectorTests::dicBomAndCrlf()
{
    const DicParseResult bom =
        parseHunspellDic(QByteArray("\xEF\xBB\xBF" "1\nab\n"), DetectorLanguage::En);
    QCOMPARE(int(bom.error), int(DicError::None));
    QVERIFY(bom.report.bomStripped);
    QCOMPARE(bom.words, QStringList({QStringLiteral("ab")}));

    // Снимается только один BOM; второй делает заголовок недопустимым.
    const DicParseResult doubleBom = parseHunspellDic(
        QByteArray("\xEF\xBB\xBF\xEF\xBB\xBF" "1\nab\n"), DetectorLanguage::En);
    QCOMPARE(int(doubleBom.error), int(DicError::BadHeader));

    const DicParseResult crlf =
        parseHunspellDic(QByteArray("2\r\nab/X\r\ncd\r\n"), DetectorLanguage::En);
    QCOMPARE(int(crlf.error), int(DicError::None));
    QCOMPARE(crlf.report.crlfLines, qint64(2));
    QCOMPARE(crlf.report.entryLines, qint64(2));
    QCOMPARE(crlf.words, QStringList({QStringLiteral("ab"), QStringLiteral("cd")}));
}

void DetectorTests::dicFiltering()
{
    QByteArray input("8\nhello/AB\nHELLO\nDon't\nna\xC3\xAF" "ve\nice cream\nword po:noun\n");
    input.append(QByteArray(kMaxWordChars + 1, 'a'));
    input.append("\n\n");
    const DicParseResult r = parseHunspellDic(input, DetectorLanguage::En);
    QCOMPARE(int(r.error), int(DicError::None));
    QCOMPARE(r.report.entryLines, qint64(8));
    QCOMPARE(r.report.emptyLines, qint64(1));
    QCOMPARE(r.report.duplicates, qint64(1));
    QCOMPARE(r.report.rejectedNotLetters, qint64(2));
    QCOMPARE(r.report.rejectedMultiword, qint64(1));
    QCOMPARE(r.report.rejectedLength, qint64(1));
    QCOMPARE(r.report.accepted, qint64(2));
    QCOMPARE(r.words, QStringList({QStringLiteral("hello"), QStringLiteral("word")}));

    // Ровно 32 буквы ещё принимаются.
    QByteArray limit("1\n");
    limit.append(QByteArray(kMaxWordChars, 'b'));
    const DicParseResult atLimit = parseHunspellDic(limit, DetectorLanguage::En);
    QCOMPARE(int(atLimit.error), int(DicError::None));
    QCOMPARE(atLimit.words.size(), qsizetype(1));
}

void DetectorTests::dicRussian()
{
    const DicParseResult r = parseHunspellDic(
        utf8(QStringLiteral("5\nёж/X\nПривет\nhello\nпривет\nкое-что\n")), DetectorLanguage::Ru);
    QCOMPARE(int(r.error), int(DicError::None));
    QCOMPARE(r.words.size(), qsizetype(2));
    QVERIFY(r.words.contains(QStringLiteral("ёж")));
    QVERIFY(r.words.contains(QStringLiteral("привет")));
    QCOMPARE(r.report.rejectedNotLetters, qint64(2));
    QCOMPARE(r.report.duplicates, qint64(1));

    // Латиница в ru-словаре и кириллица в en-словаре отбрасываются.
    const DicParseResult en = parseHunspellDic(utf8(QStringLiteral("2\nдом\ncat\n")), DetectorLanguage::En);
    QCOMPARE(en.words, QStringList({QStringLiteral("cat")}));
}

void DetectorTests::dicCountMismatch_data()
{
    QTest::addColumn<QByteArray>("input");
    QTest::newRow("fewer-records") << QByteArray("3\na\nb\n");
    QTest::newRow("more-records") << QByteArray("1\na\nb");
    QTest::newRow("extra-blank-line") << QByteArray("2\na\nb\n\n");
    QTest::newRow("header-only-nonzero") << QByteArray("1\n");
    QTest::newRow("zero-with-record") << QByteArray("0\na\n");
}

void DetectorTests::dicCountMismatch()
{
    QFETCH(QByteArray, input);
    const DicParseResult r = parseHunspellDic(input, DetectorLanguage::En);
    QCOMPARE(int(r.error), int(DicError::CountMismatch));
    QVERIFY(r.words.isEmpty());
}

void DetectorTests::dicInvalidUtf8_data()
{
    QTest::addColumn<QByteArray>("input");
    QTest::newRow("invalid-byte") << QByteArray("1\n\xFF\n");
    QTest::newRow("overlong") << QByteArray("1\n\xC0\xAF\n");
    QTest::newRow("truncated-at-end") << QByteArray("1\n\xD0");
    QTest::newRow("lone-continuation") << QByteArray("1\na\x80" "b\n");
    QTest::newRow("cp1251") << QByteArray("1\n\xEF\xF0\xE8\n");
}

void DetectorTests::dicInvalidUtf8()
{
    QFETCH(QByteArray, input);
    const DicParseResult r = parseHunspellDic(input, DetectorLanguage::Ru);
    QCOMPARE(int(r.error), int(DicError::InvalidUtf8));
    QVERIFY(r.words.isEmpty());
}

void DetectorTests::dicBadHeaders_data()
{
    QTest::addColumn<QByteArray>("input");
    QTest::addColumn<int>("error");
    QTest::newRow("empty") << QByteArray() << int(DicError::MissingHeader);
    QTest::newRow("blank-first-line") << QByteArray("\nab\n") << int(DicError::MissingHeader);
    QTest::newRow("word-header") << QByteArray("abc\nx\n") << int(DicError::BadHeader);
    QTest::newRow("negative") << QByteArray("-1\n") << int(DicError::BadHeader);
    QTest::newRow("two-numbers") << QByteArray("1 2\na\n") << int(DicError::BadHeader);
    QTest::newRow("too-long") << QByteArray("1234567890123\n") << int(DicError::BadHeader);
}

void DetectorTests::dicBadHeaders()
{
    QFETCH(QByteArray, input);
    QFETCH(int, error);
    const DicParseResult r = parseHunspellDic(input, DetectorLanguage::En);
    QCOMPARE(int(r.error), error);
    QVERIFY(r.words.isEmpty());
}

void DetectorTests::dicTooLarge()
{
    const DicParseResult r = parseHunspellDic(QByteArray(kMaxDicBytes + 1, 'a'), DetectorLanguage::En);
    QCOMPARE(int(r.error), int(DicError::TooLarge));
}

void DetectorTests::dicTooManyRecords()
{
    const qsizetype limit = qsizetype(kMaxDicRecords);
    // Ровно kMaxDicRecords строк записей с согласованным заголовком
    // принимаются: с финальным '\n' и без него (хвост — запись).
    QByteArray atLimit = QByteArray::number(kMaxDicRecords) + '\n';
    atLimit.append(QByteArray(limit, '\n'));
    const DicParseResult accepted = parseHunspellDic(atLimit, DetectorLanguage::En);
    QCOMPARE(int(accepted.error), int(DicError::None));
    QCOMPARE(accepted.report.entryLines, kMaxDicRecords);
    QVERIFY(accepted.words.isEmpty());

    QByteArray atLimitNoFinal = QByteArray::number(kMaxDicRecords) + '\n';
    atLimitNoFinal.append(QByteArray(limit - 1, '\n'));
    atLimitNoFinal.append('a');
    const DicParseResult acceptedNoFinal = parseHunspellDic(atLimitNoFinal, DetectorLanguage::En);
    QCOMPARE(int(acceptedNoFinal.error), int(DicError::None));
    QCOMPARE(acceptedNoFinal.words, QStringList({QStringLiteral("a")}));

    // На одну строку больше — отказ, хотя заголовок согласован и без
    // предела разбор был бы успешным.
    QByteArray over = QByteArray::number(kMaxDicRecords + 1) + '\n';
    over.append(QByteArray(limit + 1, '\n'));
    const DicParseResult flood = parseHunspellDic(over, DetectorLanguage::En);
    QCOMPARE(int(flood.error), int(DicError::TooManyRecords));
    QVERIFY(flood.words.isEmpty());
    QCOMPARE(flood.report.entryLines, qint64(0));

    QByteArray overNoFinal = QByteArray::number(kMaxDicRecords + 1) + '\n';
    overNoFinal.append(QByteArray(limit, '\n'));
    overNoFinal.append('a');
    QCOMPARE(int(parseHunspellDic(overNoFinal, DetectorLanguage::En).error), int(DicError::TooManyRecords));

    // Подсчёт идёт до декодирования: некорректный UTF-8 не меняет отказ.
    QByteArray invalid("1\n");
    invalid.append(QByteArray(limit + 1, '\n'));
    invalid.append('\xFF');
    QCOMPARE(int(parseHunspellDic(invalid, DetectorLanguage::En).error), int(DicError::TooManyRecords));
    QVERIFY(!dicErrorText(DicError::TooManyRecords).isEmpty());
}

void DetectorTests::dicInvalidLanguage()
{
    const DicParseResult r = parseHunspellDic(QByteArray("1\nab\n"), kInvalidLanguage);
    QCOMPARE(int(r.error), int(DicError::InvalidLanguage));
    QVERIFY(r.words.isEmpty());
}

// ---- Разделение ----

void DetectorTests::splitGroupAndBucketFormula()
{
    QCOMPARE(splitGroup(keys({1, 2, 3, 4, 5})), keys({1, 2, 3}));
    QCOMPARE(splitGroup(keys({1, 2})), keys({1, 2}));

    // Независимая запись формулы: SHA-256(seed || 0x00 || group), 8 байт BE mod 100.
    for (const KeySequence &k : {keys({1}), keys({20, 32, 31, 19}), keys({0, 0, 0}), keys({32, 31, 30, 1})}) {
        QByteArray input(kSplitSeed);
        input.append('\0');
        input.append(k.left(3));
        const QByteArray digest = QCryptographicHash::hash(input, QCryptographicHash::Sha256);
        quint64 value = 0;
        for (int i = 0; i < 8; ++i)
            value = (value << 8) | quint64(static_cast<unsigned char>(digest.at(i)));
        QCOMPARE(splitBucket(k), int(value % 100U));
    }

    // Один префикс — одна часть.
    QCOMPARE(int(splitForKeys(keys({5, 6, 7}))), int(splitForKeys(keys({5, 6, 7, 1, 2, 3}))));
    QCOMPARE(splitBucket(keys({5, 6, 7, 8})), splitBucket(keys({5, 6, 7, 30})));
}

void DetectorTests::splitDistribution()
{
    std::array<int, 3> counts{{0, 0, 0}};
    int total = 0;
    for (int a = 0; a < NgramModel::kLetterKeys; ++a) {
        for (int b = 0; b < NgramModel::kLetterKeys; ++b) {
            for (int c = 0; c < NgramModel::kLetterKeys; ++c) {
                const int bucket = splitBucket(keys({a, b, c}));
                QVERIFY(bucket >= 0 && bucket < 100);
                ++counts[std::size_t(splitForKeys(keys({a, b, c})))];
                ++total;
            }
        }
    }
    const double train = double(counts[0]) / total;
    const double calibration = double(counts[1]) / total;
    const double test = double(counts[2]) / total;
    QVERIFY(train > 0.65 && train < 0.75);
    QVERIFY(calibration > 0.12 && calibration < 0.18);
    QVERIFY(test > 0.12 && test < 0.18);
}

void DetectorTests::splitKeepsLanguageVariantsAndCopiesTogether()
{
    // EN-слово, его копия в ru и RU-слово на тех же клавишах — один ключ,
    // а значит одна часть.
    const QStringList enWords{QStringLiteral("hello"), QStringLiteral("world"), QStringLiteral("cat")};
    for (const QString &word : enWords) {
        const KeySequence home = canon(word, SourceLayout::Us);
        const KeySequence copy = canon(convertLayout(word, LayoutDirection::UsToRu), SourceLayout::Ru);
        QVERIFY(!home.isEmpty());
        QCOMPARE(copy, home);
        QCOMPARE(int(splitForKeys(copy)), int(splitForKeys(home)));
    }
    const QStringList ruWords{QStringLiteral("привет"), QStringLiteral("любовь"), QStringLiteral("ёж")};
    for (const QString &word : ruWords) {
        const KeySequence home = canon(word, SourceLayout::Ru);
        const KeySequence copy = canon(convertLayout(word, LayoutDirection::RuToUs), SourceLayout::Us);
        QVERIFY(!home.isEmpty());
        QCOMPARE(copy, home);
        QCOMPARE(int(splitForKeys(copy)), int(splitForKeys(home)));
    }
    QCOMPARE(canon(QStringLiteral("ghbdtn"), SourceLayout::Us), canon(QStringLiteral("привет"), SourceLayout::Ru));
}

void DetectorTests::leakFreeForHashedSplit()
{
    std::vector<SplitEntry> en;
    std::vector<SplitEntry> ru;
    // Включая общий ключ: EN "ghbdtn" и RU "привет".
    for (const QString &w : {QStringLiteral("hello"), QStringLiteral("help"), QStringLiteral("world"),
                             QStringLiteral("ghbdtn"), QStringLiteral("cat")}) {
        const KeySequence k = canon(w, SourceLayout::Us);
        en.push_back({w, k, splitForKeys(k)});
    }
    for (const QString &w : {QStringLiteral("привет"), QStringLiteral("любовь"), QStringLiteral("ёж"),
                             QStringLiteral("руддщ")}) {
        const KeySequence k = canon(w, SourceLayout::Ru);
        ru.push_back({w, k, splitForKeys(k)});
    }
    const LeakReport report = countSplitLeaks(en, ru);
    QVERIFY(report.leakFree());
}

void DetectorTests::leaksDetected()
{
    const KeySequence shared = canon(QStringLiteral("ghbdtn"), SourceLayout::Us);

    // Общий EN/RU ключ в разных частях: ключ и обе wrong-layout копии.
    const LeakReport crossLanguage =
        countSplitLeaks({{QStringLiteral("ghbdtn"), shared, SplitPart::Train}},
                        {{QStringLiteral("привет"), shared, SplitPart::Test}});
    QCOMPARE(crossLanguage.canonicalKeyLeaks, qint64(1));
    QCOMPARE(crossLanguage.wordLeaks, qint64(0));
    QCOMPARE(crossLanguage.wrongLayoutMappingLeaks, qint64(2));
    QVERIFY(!crossLanguage.leakFree());

    // Одно слово в двух частях.
    const KeySequence word = canon(QStringLiteral("word"), SourceLayout::Us);
    const LeakReport sameWord = countSplitLeaks(
        {{QStringLiteral("word"), word, SplitPart::Train}, {QStringLiteral("word"), word, SplitPart::Calibration}},
        {});
    QCOMPARE(sameWord.canonicalKeyLeaks, qint64(1));
    QCOMPARE(sameWord.wordLeaks, qint64(1));
    QCOMPARE(sameWord.wrongLayoutMappingLeaks, qint64(0));

    // Одна часть — утечки нет.
    const LeakReport samePart =
        countSplitLeaks({{QStringLiteral("ghbdtn"), shared, SplitPart::Test}},
                        {{QStringLiteral("привет"), shared, SplitPart::Test}});
    QVERIFY(samePart.leakFree());
}

// ---- Манифест ----

void DetectorTests::manifestValid()
{
    const QByteArray bytes = encode(manifestObject());
    const ManifestParseResult r = parseExperimentManifest(bytes);
    QCOMPARE(int(r.error), int(ManifestError::None));
    QCOMPARE(r.manifest.manifestSha256, sha256Hex(bytes));
    QCOMPARE(r.manifest.scope, QStringLiteral("local-experiment-only"));
    QCOMPARE(r.manifest.status, QStringLiteral("approved-local-experiment"));
    const ManifestSource &en = r.manifest.sources[std::size_t(DetectorLanguage::En)];
    const ManifestSource &ru = r.manifest.sources[std::size_t(DetectorLanguage::Ru)];
    QCOMPARE(en.bytes, qint64(fixtureEnBytes().size()));
    QCOMPARE(ru.sha256, sha256Hex(fixtureRuBytes()));
    QVERIFY(bytesMatch(fixtureEnBytes(), en.bytes, en.sha256));
    QVERIFY(bytesMatch(fixtureRuBytes(), ru.bytes, ru.sha256));
    // Источники не перепутаны.
    QVERIFY(!bytesMatch(fixtureRuBytes(), en.bytes, en.sha256));
}

void DetectorTests::manifestRejects_data()
{
    QTest::addColumn<QByteArray>("manifest");
    QTest::addColumn<int>("error");

    const auto row = [](const char *name, const QJsonObject &object, ManifestError error) {
        QTest::newRow(name) << encode(object) << int(error);
    };

    QTest::newRow("too-large") << QByteArray(kMaxManifestBytes + 1, ' ') << int(ManifestError::TooLarge);
    QTest::newRow("not-json") << QByteArray("{") << int(ManifestError::InvalidJson);
    QTest::newRow("array-root") << QByteArray("[]") << int(ManifestError::InvalidJson);
    QTest::newRow("empty") << QByteArray() << int(ManifestError::InvalidJson);

    row("schema-2", withRoot("schema_version", 2), ManifestError::SchemaVersion);
    row("schema-string", withRoot("schema_version", QStringLiteral("1")), ManifestError::SchemaVersion);
    row("schema-fraction", withRoot("schema_version", 1.5), ManifestError::SchemaVersion);
    row("schema-missing", withoutRoot("schema_version"), ManifestError::SchemaVersion);
    row("status-blocked", withRoot("status", QStringLiteral("blocked")), ManifestError::StatusNotApproved);
    row("status-missing", withoutRoot("status"), ManifestError::StatusNotApproved);
    row("scope-release", withRoot("scope", QStringLiteral("release")), ManifestError::ScopeMismatch);
    row("scope-missing", withoutRoot("scope"), ManifestError::ScopeMismatch);
    row("release-true", withRoot("release_approved", true), ManifestError::ReleaseApproved);
    row("release-string", withRoot("release_approved", QStringLiteral("false")),
        ManifestError::ReleaseApproved);
    row("release-missing", withoutRoot("release_approved"), ManifestError::ReleaseApproved);
    row("commit-short", withRoot("commit", QStringLiteral("abc")), ManifestError::BadCommit);
    row("commit-upper", withRoot("commit", QString(40, QLatin1Char('A'))), ManifestError::BadCommit);
    row("commit-missing", withoutRoot("commit"), ManifestError::BadCommit);

    row("sources-missing", withoutRoot("training_sources"), ManifestError::BadSources);
    row("sources-object", withRoot("training_sources", QJsonObject()), ManifestError::BadSources);
    {
        QJsonArray one;
        one.append(sourceObject("en", fixtureEnBytes()));
        row("sources-one", withRoot("training_sources", one), ManifestError::BadSources);
        QJsonArray three = one;
        three.append(sourceObject("ru", fixtureRuBytes()));
        three.append(sourceObject("ru", fixtureRuBytes()));
        row("sources-three", withRoot("training_sources", three), ManifestError::BadSources);
        QJsonArray duplicate = one;
        duplicate.append(sourceObject("en", fixtureEnBytes()));
        row("sources-duplicate-en", withRoot("training_sources", duplicate), ManifestError::BadSources);
        QJsonArray other = one;
        other.append(sourceObject("de", fixtureRuBytes()));
        row("sources-unknown-language", withRoot("training_sources", other), ManifestError::BadSources);
        QJsonArray notObject = one;
        notObject.append(QStringLiteral("ru"));
        row("sources-not-object", withRoot("training_sources", notObject), ManifestError::BadSources);
    }

    row("sha-upper", withSource(0, "sha256", QString(64, QLatin1Char('A'))), ManifestError::BadSourceField);
    row("sha-short", withSource(1, "sha256", QStringLiteral("abc")), ManifestError::BadSourceField);
    row("notice-sha-bad", withSource(0, "notice_sha256", 5), ManifestError::BadSourceField);
    row("bytes-zero", withSource(0, "bytes", 0), ManifestError::BadSourceField);
    row("bytes-fraction", withSource(0, "bytes", 1.5), ManifestError::BadSourceField);
    row("bytes-too-large", withSource(1, "bytes", double(kMaxDicBytes + 1)), ManifestError::BadSourceField);
    row("bytes-string", withSource(1, "bytes", QStringLiteral("5")), ManifestError::BadSourceField);
    row("notice-missing", withSource(0, "notice", QJsonValue()), ManifestError::BadSourceField);
    row("notice-parent", withSource(0, "notice", QStringLiteral("../secret.txt")), ManifestError::BadNoticePath);
    row("notice-absolute", withSource(1, "notice", QStringLiteral("/etc/passwd")), ManifestError::BadNoticePath);
    row("notice-escape", withSource(1, "notice", QStringLiteral("data/notices/../../x.txt")),
        ManifestError::BadNoticePath);
}

void DetectorTests::manifestRejects()
{
    QFETCH(QByteArray, manifest);
    QFETCH(int, error);
    QCOMPARE(int(parseExperimentManifest(manifest).error), error);
}

void DetectorTests::manifestNoticePaths_data()
{
    QTest::addColumn<QString>("path");
    QTest::addColumn<bool>("safe");
    QTest::newRow("ok") << QStringLiteral("data/notices/en_US.README.txt") << true;
    QTest::newRow("nested") << QStringLiteral("data/notices/a/b-c_d.txt") << true;
    QTest::newRow("empty") << QString() << false;
    QTest::newRow("prefix-only") << QStringLiteral("data/notices/") << false;
    QTest::newRow("other-dir") << QStringLiteral("data/sources.json") << false;
    QTest::newRow("absolute") << QStringLiteral("/data/notices/x.txt") << false;
    QTest::newRow("dotdot") << QStringLiteral("data/notices/../../etc") << false;
    QTest::newRow("dot") << QStringLiteral("data/notices/./x.txt") << false;
    QTest::newRow("double-slash") << QStringLiteral("data/notices//x.txt") << false;
    QTest::newRow("backslash") << QStringLiteral("data/notices/a\\b.txt") << false;
    QTest::newRow("space") << QStringLiteral("data/notices/a b.txt") << false;
    QTest::newRow("non-ascii") << QStringLiteral("data/notices/файл.txt") << false;
    QTest::newRow("too-long") << QStringLiteral("data/notices/") + QString(kMaxNoticePathChars, QLatin1Char('a'))
                              << false;
}

void DetectorTests::manifestNoticePaths()
{
    QFETCH(QString, path);
    QFETCH(bool, safe);
    QCOMPARE(isSafeNoticePath(path), safe);
}

void DetectorTests::manifestResolveNotice()
{
    const QString manifest = QStringLiteral("/fixture/repo/data/sources.json");
    QCOMPARE(manifestRoot(manifest), QStringLiteral("/fixture/repo"));
    QCOMPARE(resolveNoticePath(manifest, QStringLiteral("data/notices/en_US.README.txt")),
             QStringLiteral("/fixture/repo/data/notices/en_US.README.txt"));
    QVERIFY(resolveNoticePath(manifest, QStringLiteral("../outside.txt")).isEmpty());
    QVERIFY(resolveNoticePath(manifest, QStringLiteral("data/notices/../../../x")).isEmpty());
    QVERIFY(resolveNoticePath(QString(), QStringLiteral("data/notices/x.txt")).isEmpty());

    // Относительный путь манифеста даёт абсолютный результат.
    const QString relative = resolveNoticePath(QStringLiteral("data/sources.json"),
                                               QStringLiteral("data/notices/x.txt"));
    QVERIFY(relative.startsWith(QLatin1Char('/')));
    QVERIFY(relative.endsWith(QStringLiteral("/data/notices/x.txt")));
}

void DetectorTests::manifestBytesMatch()
{
    const QByteArray data = fixtureEnBytes();
    const QString sha = sha256Hex(data);
    QVERIFY(bytesMatch(data, data.size(), sha));
    QVERIFY(!bytesMatch(data, data.size() + 1, sha));
    QByteArray changed = data;
    changed[0] = '3';
    QVERIFY(!bytesMatch(changed, changed.size(), sha));
    QVERIFY(!bytesMatch(data, data.size(), sha.toUpper()));
    QVERIFY(!bytesMatch(data, data.size(), QString()));
}

QTEST_APPLESS_MAIN(DetectorTests)

#include "detector_tests.moc"
