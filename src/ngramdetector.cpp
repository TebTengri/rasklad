#include "ngramdetector.h"

#include "layoutconverter.h"

#include <QChar>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace rasklad {
namespace {

// Порядок канонических клавиш: базовый уровень us для 33 клавиш, на которых
// в ru(default) находятся буквы: TLDE, AD01..AD12, AC01..AC11, AB01..AB09.
// BKSL и AB10 ('/') не входят: в ru на них не буквы.
constexpr char16_t kCanonicalKeyOrder[] = u"`qwertyuiop[]asdfghjkl;'zxcvbnm,.";
static_assert(sizeof(kCanonicalKeyOrder) / sizeof(char16_t) == NgramModel::kLetterKeys + 1,
              "canonical alphabet must contain 33 keys");

// Смещения таблиц порядков внутри одного канала.
constexpr int kUnigramOffset = 0;
constexpr int kBigramOffset = NgramModel::kUnigramSize;
constexpr int kTrigramOffset = NgramModel::kUnigramSize + NgramModel::kBigramSize;

// Худший случай суммы: (32 + 1) шагов * сумма весов * 127 должен
// помещаться в qint32 с большим запасом.
constexpr qint64 kOrderWeightSum = NgramModel::kOrderWeights[0] + NgramModel::kOrderWeights[1]
    + NgramModel::kOrderWeights[2];
static_assert(qint64(kMaxTokenKeys + 1) * kOrderWeightSum * (-NgramModel::kMinQuantized)
                  < qint64(std::numeric_limits<qint32>::max()),
              "accumulator may overflow");
static_assert(NgramModel::kWeightBytes == 88270, "unexpected weight size");

int canonicalIndex(char16_t c)
{
    for (int i = 0; i < NgramModel::kLetterKeys; ++i) {
        if (kCanonicalKeyOrder[i] == c)
            return i;
    }
    return -1;
}

bool isAsciiLetter(char16_t c)
{
    return (c >= u'a' && c <= u'z') || (c >= u'A' && c <= u'Z');
}

// Символ us на одной из 33 буквенных клавиш ru (оба уровня).
bool isUsLetterKeySymbol(char16_t c, bool &shifted)
{
    if (c >= u'a' && c <= u'z') {
        shifted = false;
        return true;
    }
    if (c >= u'A' && c <= u'Z') {
        shifted = true;
        return true;
    }
    constexpr char16_t base[] = u"`[];',.";
    constexpr char16_t shift[] = u"~{}:\"<>";
    for (const char16_t b : base) {
        if (b != 0 && b == c) {
            shifted = false;
            return true;
        }
    }
    for (const char16_t s : shift) {
        if (s != 0 && s == c) {
            shifted = true;
            return true;
        }
    }
    return false;
}

// Одна из 33 букв ru(default) в любом регистре.
bool isRuLetter(char16_t c, bool &upper)
{
    if ((c >= u'а' && c <= u'я') || c == u'ё') {
        upper = false;
        return true;
    }
    if ((c >= u'А' && c <= u'Я') || c == u'Ё') {
        upper = true;
        return true;
    }
    return false;
}

bool isCyrillic(char16_t c)
{
    return c >= 0x0400 && c <= 0x052F;
}

bool hasValidKeys(const KeySequence &keys)
{
    if (keys.size() < kMinTokenKeys || keys.size() > kMaxTokenKeys)
        return false;
    for (const char k : keys) {
        if (static_cast<unsigned char>(k) >= NgramModel::kLetterKeys)
            return false;
    }
    return true;
}

struct Counts {
    std::vector<qint64> uni = std::vector<qint64>(NgramModel::kUnigramSize, 0);
    std::vector<qint64> bi = std::vector<qint64>(NgramModel::kBigramSize, 0);
    std::vector<qint64> tri = std::vector<qint64>(NgramModel::kTrigramSize, 0);
};

// Последовательность BOS BOS k1..kn EOS: предсказываются k1..kn и EOS.
void accumulate(Counts &counts, const KeySequence &keys)
{
    int a = NgramModel::kBos;
    int b = NgramModel::kBos;
    for (qsizetype i = 0; i <= keys.size(); ++i) {
        const int x = i < keys.size() ? static_cast<unsigned char>(keys.at(i)) : NgramModel::kEos;
        ++counts.uni[x];
        ++counts.bi[b * NgramModel::kAlphabet + x];
        ++counts.tri[(a * NgramModel::kAlphabet + b) * NgramModel::kAlphabet + x];
        a = b;
        b = x;
    }
}

qint8 quantize(double logProbability)
{
    const double q = std::round(logProbability * NgramModel::kScale);
    const double clamped = std::clamp(q, double(NgramModel::kMinQuantized), 0.0);
    return static_cast<qint8>(clamped);
}

// Условное распределение для каждого контекста:
//   P(c | ctx) = (n(ctx, c) + alpha) / (n(ctx) + 35 * alpha).
void fillConditional(const std::vector<qint64> &counts, int contexts, qint8 *out)
{
    const int a = NgramModel::kAlphabet;
    for (int ctx = 0; ctx < contexts; ++ctx) {
        qint64 total = 0;
        for (int c = 0; c < a; ++c)
            total += counts[std::size_t(ctx) * a + c];
        const double denominator = double(total) + a * NgramModel::kAlpha;
        for (int c = 0; c < a; ++c) {
            const double numerator = double(counts[std::size_t(ctx) * a + c]) + NgramModel::kAlpha;
            out[ctx * a + c] = quantize(std::log(numerator / denominator));
        }
    }
}

std::vector<qint8> buildTable(const std::vector<KeySequence> &words, qsizetype &skipped)
{
    Counts counts;
    for (const KeySequence &keys : words) {
        if (!hasValidKeys(keys)) {
            ++skipped;
            continue;
        }
        accumulate(counts, keys);
    }
    std::vector<qint8> table(NgramModel::kTableSize, 0);
    fillConditional(counts.uni, 1, table.data() + kUnigramOffset);
    fillConditional(counts.bi, NgramModel::kAlphabet, table.data() + kBigramOffset);
    fillConditional(counts.tri, NgramModel::kAlphabet * NgramModel::kAlphabet,
                    table.data() + kTrigramOffset);
    return table;
}

qint32 scoreTable(const std::vector<qint8> &table, const KeySequence &keys)
{
    const qint8 *t = table.data();
    const auto &w = NgramModel::kOrderWeights;
    qint32 sum = 0;
    int a = NgramModel::kBos;
    int b = NgramModel::kBos;
    for (qsizetype i = 0; i <= keys.size(); ++i) {
        const int x = i < keys.size() ? static_cast<unsigned char>(keys.at(i)) : NgramModel::kEos;
        sum += w[0] * t[kUnigramOffset + x];
        sum += w[1] * t[kBigramOffset + b * NgramModel::kAlphabet + x];
        sum += w[2] * t[kTrigramOffset + (a * NgramModel::kAlphabet + b) * NgramModel::kAlphabet + x];
        a = b;
        b = x;
    }
    return sum;
}

void appendU32(QByteArray &out, quint32 value)
{
    for (int shift = 0; shift < 32; shift += 8)
        out.append(static_cast<char>((value >> shift) & 0xFFU));
}

// Вызывающий код уже проверил фиксированный размер; at() дополнительно
// проверяет границы и не обращается к сырому указателю буфера.
quint32 readU32(const QByteArray &data, qsizetype offset)
{
    quint32 value = 0;
    for (int i = 0; i < 4; ++i)
        value |= quint32(static_cast<unsigned char>(data.at(offset + i))) << (8 * i);
    return value;
}

constexpr char kMagic[4] = {'R', 'S', 'K', 'N'};

bool hasMagic(const QByteArray &data)
{
    for (qsizetype i = 0; i < 4; ++i) {
        if (data.at(i) != kMagic[i])
            return false;
    }
    return true;
}

// Общие условия отказа для обеих политик.
bool rejectEarly(const TokenScore &score, SourceLayout source, bool enabled, bool thresholdsFinite,
                 DetectorDecision &decision)
{
    if (!isWellFormedScore(score) || !isValidSourceLayout(source)) {
        decision = {DecisionAction::Abstain, DecisionReason::Unsupported};
        return true;
    }
    if (score.keyCount < kMinDecisionKeys) {
        decision = {DecisionAction::Abstain, DecisionReason::TooShort};
        return true;
    }
    if (!enabled || !thresholdsFinite) {
        decision = {DecisionAction::Abstain, DecisionReason::Disabled};
        return true;
    }
    return false;
}

} // namespace

bool isValidLanguage(DetectorLanguage language)
{
    return language == DetectorLanguage::En || language == DetectorLanguage::Ru;
}

bool isValidSourceLayout(SourceLayout source)
{
    return source == SourceLayout::Us || source == SourceLayout::Ru;
}

bool isWellFormedScore(const TokenScore &score)
{
    if (!score.ok || score.keyCount < kMinTokenKeys || score.keyCount > kMaxTokenKeys
        || score.steps != score.keyCount + 1)
        return false;
    const qint64 lower = qint64(score.steps) * NgramModel::kMinStepRaw;
    for (const qint32 raw : score.raw) {
        if (raw > 0 || qint64(raw) < lower)
            return false;
    }
    return true;
}

DetectorLanguage observedLanguage(SourceLayout source)
{
    return source == SourceLayout::Us ? DetectorLanguage::En : DetectorLanguage::Ru;
}

DetectorLanguage alternativeLanguage(SourceLayout source)
{
    return source == SourceLayout::Us ? DetectorLanguage::Ru : DetectorLanguage::En;
}

CanonicalToken canonicalizeToken(QStringView token, SourceLayout source)
{
    CanonicalToken result;
    if (!isValidSourceLayout(source)) {
        result.status = CanonicalStatus::InvalidLayout;
        return result;
    }
    if (token.isEmpty()) {
        result.status = CanonicalStatus::Empty;
        return result;
    }
    // Допустимые символы лежат в BMP, поэтому одна кодовая единица — одна клавиша.
    if (token.size() > kMaxTokenKeys) {
        result.status = CanonicalStatus::TooLong;
        return result;
    }

    // Строгая проверка до преобразования: иначе convertLayout оставил бы
    // символ другой письменности как есть, и он прошёл бы как «буква».
    bool upper = false;
    for (const QChar ch : token) {
        const char16_t c = ch.unicode();
        bool shifted = false;
        if (source == SourceLayout::Us) {
            if (isUsLetterKeySymbol(c, shifted)) {
                upper = upper || shifted;
                continue;
            }
            result.status = isCyrillic(c) ? CanonicalStatus::MixedScript
                                          : CanonicalStatus::UnsupportedCharacter;
            return result;
        }
        if (isRuLetter(c, shifted)) {
            upper = upper || shifted;
            continue;
        }
        result.status = isAsciiLetter(c) ? CanonicalStatus::MixedScript
                                         : CanonicalStatus::UnsupportedCharacter;
        return result;
    }

    // us -> ru переводит оба уровня в русские буквы, toLower снимает Shift,
    // ru -> us даёт базовый уровень us, то есть имена физических клавиш.
    QString ruText = source == SourceLayout::Us
        ? convertLayout(token.toString(), LayoutDirection::UsToRu)
        : token.toString();
    ruText = ruText.toLower();
    const QString usBase = convertLayout(ruText, LayoutDirection::RuToUs);

    KeySequence keys;
    keys.reserve(usBase.size());
    for (const QChar ch : usBase) {
        const int index = canonicalIndex(ch.unicode());
        if (index < 0) {
            // Не должно случиться после проверки выше; отказ вместо догадки.
            result.status = CanonicalStatus::UnsupportedCharacter;
            return result;
        }
        keys.append(static_cast<char>(index));
    }

    result.status = CanonicalStatus::Ok;
    result.keys = keys;
    result.hadUppercase = upper;
    return result;
}

QString keysToUsBase(const KeySequence &keys)
{
    if (!hasValidKeys(keys))
        return {};
    QString out;
    out.reserve(keys.size());
    for (const char k : keys)
        out.append(QChar(kCanonicalKeyOrder[static_cast<unsigned char>(k)]));
    return out;
}

double TokenScore::perStep(DetectorLanguage language) const
{
    if (!ok || steps <= 0 || !isValidLanguage(language))
        return std::numeric_limits<double>::quiet_NaN();
    return double(raw[std::size_t(language)]) / (double(NgramModel::kScale) * steps);
}

NgramModel NgramModel::train(const std::vector<KeySequence> &en, const std::vector<KeySequence> &ru,
                             qsizetype *skipped)
{
    qsizetype skippedCount = 0;
    NgramModel model;
    model.m_tables[std::size_t(DetectorLanguage::En)] = buildTable(en, skippedCount);
    model.m_tables[std::size_t(DetectorLanguage::Ru)] = buildTable(ru, skippedCount);
    if (skipped)
        *skipped = skippedCount;
    return model;
}

bool NgramModel::isValid() const
{
    return m_tables[0].size() == std::size_t(kTableSize) && m_tables[1].size() == std::size_t(kTableSize);
}

QByteArray NgramModel::serialize() const
{
    if (!isValid())
        return {};
    QByteArray out;
    out.reserve(kSerializedBytes);
    out.append(kMagic, 4);
    appendU32(out, kFormatVersion);
    appendU32(out, quint32(kScale));
    appendU32(out, quint32(kAlphabet));
    appendU32(out, quint32(kTableSize));
    for (const std::vector<qint8> &table : m_tables)
        out.append(reinterpret_cast<const char *>(table.data()), qsizetype(table.size()));
    return out;
}

std::optional<NgramModel> NgramModel::deserialize(const QByteArray &data)
{
    if (data.size() != kSerializedBytes)
        return std::nullopt;
    if (!hasMagic(data))
        return std::nullopt;
    if (readU32(data, 4) != kFormatVersion || readU32(data, 8) != quint32(kScale)
        || readU32(data, 12) != quint32(kAlphabet) || readU32(data, 16) != quint32(kTableSize))
        return std::nullopt;

    NgramModel model;
    qsizetype offset = kHeaderBytes;
    for (std::vector<qint8> &table : model.m_tables) {
        table.resize(kTableSize);
        for (int i = 0; i < kTableSize; ++i) {
            const qint8 v = static_cast<qint8>(data.at(offset + i));
            if (v < kMinQuantized || v > 0)
                return std::nullopt;
            table[std::size_t(i)] = v;
        }
        offset += kTableSize;
    }
    return model;
}

TokenScore NgramModel::score(const KeySequence &keys) const
{
    TokenScore result;
    if (!isValid() || !hasValidKeys(keys))
        return result;
    result.keyCount = int(keys.size());
    result.steps = result.keyCount + 1;
    for (std::size_t i = 0; i < m_tables.size(); ++i)
        result.raw[i] = scoreTable(m_tables[i], keys);
    result.ok = true;
    return result;
}

const std::vector<qint8> &NgramModel::table(DetectorLanguage language) const
{
    static const std::vector<qint8> kEmpty;
    if (!isValidLanguage(language))
        return kEmpty;
    return m_tables[std::size_t(language)];
}

DetectorDecision decideSingleD(const TokenScore &score, SourceLayout source,
                               const SingleDThresholds &thresholds)
{
    DetectorDecision decision;
    if (rejectEarly(score, source, thresholds.enabled,
                    std::isfinite(thresholds.gap) && thresholds.gap >= 0.0, decision))
        return decision;

    const double observed = score.perStep(observedLanguage(source));
    const double alternative = score.perStep(alternativeLanguage(source));
    const double d = alternative - observed;
    if (d > 0.0 && d >= thresholds.gap)
        return {DecisionAction::Correct, DecisionReason::AlternativePreferred};
    if (d < 0.0 && -d >= thresholds.gap)
        return {DecisionAction::Keep, DecisionReason::ObservedPreferred};
    return {DecisionAction::Abstain, DecisionReason::Uncertain};
}

DetectorDecision decideTwoScore(const TokenScore &score, SourceLayout source,
                                const TwoScoreThresholds &thresholds)
{
    DetectorDecision decision;
    const bool finite = std::isfinite(thresholds.minAlternative)
        && std::isfinite(thresholds.maxObserved) && std::isfinite(thresholds.gap)
        && thresholds.gap >= 0.0;
    if (rejectEarly(score, source, thresholds.enabled, finite, decision))
        return decision;

    const double observed = score.perStep(observedLanguage(source));
    const double alternative = score.perStep(alternativeLanguage(source));
    if (alternative > observed && alternative >= thresholds.minAlternative
        && observed <= thresholds.maxObserved && alternative - observed >= thresholds.gap)
        return {DecisionAction::Correct, DecisionReason::AlternativePreferred};
    if (observed > thresholds.maxObserved && observed >= alternative)
        return {DecisionAction::Keep, DecisionReason::ObservedPreferred};
    return {DecisionAction::Abstain, DecisionReason::Uncertain};
}

} // namespace rasklad
