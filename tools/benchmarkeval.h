#pragma once

// Методы, исходы и агрегаты оценки SYS-233 (tools/detector_benchmark).
//
// Только для offline-эксперимента и тестов. Функции не выполняют I/O, не
// логируют и не выводят текст: в JSON попадают только счётчики. Пороги
// приходят из замороженного протокола; подбора на данных оценки нет.

#include "frontcodedlexicon.h"
#include "layoutconverter.h"
#include "ngramdetector.h"

#include <QByteArray>
#include <QJsonObject>
#include <QString>
#include <QStringView>

#include <array>
#include <functional>
#include <optional>
#include <vector>

namespace rasklad::benchmark {

enum class Method {
    AlwaysKeep,
    ExactTrain,
    ExactFull,
    SingleD,
    TwoScore,
    ExactFullThenTwoScore,
    TwoScoreConfirmedByExactFull,
};
inline constexpr int kMethodCount = 7;
inline constexpr std::array<Method, kMethodCount> kMethods{{
    Method::AlwaysKeep,
    Method::ExactTrain,
    Method::ExactFull,
    Method::SingleD,
    Method::TwoScore,
    Method::ExactFullThenTwoScore,
    Method::TwoScoreConfirmedByExactFull,
}};

[[nodiscard]] const char *methodName(Method method);

// Признаки одного случая: всё, от чего зависят решения методов.
struct CaseFeatures {
    // Канонизация успешна (CanonicalStatus::Ok).
    bool supported = false;
    int keyCount = 0;
    bool observedTrain = false;
    bool alternativeTrain = false;
    bool observedFull = false;
    bool alternativeFull = false;
    DecisionAction singleD = DecisionAction::Abstain;
    DecisionAction twoScore = DecisionAction::Abstain;
};

// supported && keyCount >= 4. Иначе все методы, кроме alwaysKeep, — Abstain.
[[nodiscard]] bool isDecisionEligible(const CaseFeatures &features);

// alwaysKeep: Keep.
// exactTrain/exactFull: observed hit -> Keep; else alternative hit -> Correct; else Abstain.
// singleD/twoScore: решения ядра с замороженными порогами.
// exactFullThenTwoScore: observed full -> Keep; else alternative full -> Correct; else twoScore.
// twoScoreConfirmedByExactFull: observed full -> Keep; else Correct, только если
//   twoScore == Correct и alternative full; иначе Abstain.
[[nodiscard]] DecisionAction decideMethod(Method method, const CaseFeatures &features);

struct FeatureContext {
    const NgramModel *model = nullptr;
    // Индекс — DetectorLanguage.
    std::array<const FrontCodedLexicon *, 2> train{{nullptr, nullptr}};
    std::array<const FrontCodedLexicon *, 2> full{{nullptr, nullptr}};
    SingleDThresholds singleD;
    TwoScoreThresholds twoScore;

    [[nodiscard]] bool isValid() const;
};

[[nodiscard]] CaseFeatures computeFeatures(const CanonicalToken &canonical, SourceLayout layout,
                                           const FeatureContext &context);

// Тот же результат, что decideMethod(computeFeatures(...)), но вычисляет
// только нужное методу (путь для измерения задержки).
[[nodiscard]] DecisionAction decideLive(Method method, QStringView text, SourceLayout layout,
                                        const FeatureContext &context);

// Членство ключа относительно исходной раскладки случая.
enum class Membership { Unsupported, ObservedOnly, AlternativeOnly, Both, Neither };
inline constexpr int kMembershipCount = 5;
[[nodiscard]] const char *membershipName(Membership membership);
[[nodiscard]] Membership fullMembership(const CaseFeatures &features);
[[nodiscard]] Membership trainMembership(const CaseFeatures &features);

enum class Outcome : quint8 {
    Keep,
    Abstain,
    // Correct на однозначной wrong-копии, преобразование дало исходный токен.
    Recovered,
    // Correct на однозначном valid (намерение Keep).
    FalseChange,
    // Correct на wrong-копии, но преобразование дало другой текст.
    WrongConversion,
    // Correct на любом неоднозначном случае: небезопасно, не восстановление.
    AmbiguousChange,
};

[[nodiscard]] Outcome classifyOutcome(DecisionAction action, bool wrongIntent, bool ambiguous,
                                      bool conversionMatches);
[[nodiscard]] bool isUnsafe(Outcome outcome);

struct RoleCounts {
    qint64 count = 0;
    qint64 keep = 0;
    qint64 abstain = 0;
    qint64 recovered = 0;
    qint64 falseChange = 0;
    qint64 wrongConversion = 0;
    qint64 ambiguousChange = 0;

    void add(Outcome outcome);
    [[nodiscard]] qint64 changes() const;
    [[nodiscard]] qint64 unsafe() const;
};

struct SliceCounts {
    RoleCounts valid;
    RoleCounts wrong;
    RoleCounts ambiguousValid;
    RoleCounts ambiguousWrong;

    void add(bool wrongIntent, bool ambiguous, Outcome outcome);
    [[nodiscard]] qint64 unsafe() const;
};

[[nodiscard]] QJsonObject sliceJson(const SliceCounts &slice);

// Аналитическая точность при доле wrong-layout prevalence в потоке:
//   P = p*r / (p*(r + u) + (1 - p)*f),
// r — доля восстановленных среди всех wrong (включая неоднозначные),
// u — доля небезопасных изменений среди всех wrong,
// f — доля изменений среди всех valid (включая неоднозначные).
// nullopt, если знаменатель 0 или аргументы вне [0, 1].
[[nodiscard]] std::optional<double> expectedPrecision(double prevalence, double recoveredRate,
                                                      double unsafeWrongRate, double validChangeRate);

[[nodiscard]] QJsonObject expectedPrecisionJson(const SliceCounts &overall);

struct PairedDelta {
    qint64 cases = 0;
    qint64 addedRecoveries = 0;
    qint64 lostRecoveries = 0;
    qint64 addedUnsafe = 0;
    qint64 preventedUnsafe = 0;
    qint64 bothRecovered = 0;
    qint64 bothUnsafe = 0;
};

[[nodiscard]] PairedDelta pairedDelta(const std::vector<Outcome> &method,
                                      const std::vector<Outcome> &baseline,
                                      const std::vector<std::size_t> &indices);
[[nodiscard]] QJsonObject pairedJson(const PairedDelta &delta);

[[nodiscard]] SourceLayout homeLayout(DetectorLanguage language);
[[nodiscard]] SourceLayout otherLayout(SourceLayout layout);
[[nodiscard]] LayoutDirection correctionDirection(SourceLayout typedLayout);
[[nodiscard]] const char *languageName(DetectorLanguage language);
[[nodiscard]] const char *layoutName(SourceLayout layout);
[[nodiscard]] const char *canonicalStatusName(CanonicalStatus status);

// ---- Естественные случаи ----

struct EvalCase {
    // Язык намерения: язык исходного токена.
    DetectorLanguage language = DetectorLanguage::En;
    SourceLayout layout = SourceLayout::Us;
    bool wrong = false;
    bool ambiguous = false;
    bool nativeLetterOnly = false;
    bool conversionMatches = false;
    CanonicalStatus status = CanonicalStatus::Empty;
    // Канонические клавиши, если supported, иначе длина UTF-16.
    int length = 0;
    quint32 typeId = 0;
    bool typeRepresentative = false;
    CaseFeatures features;
    // Хранится только для измерения задержки; не выводится.
    QString text;
};

struct NaturalTokenStats {
    qint64 tokens = 0;
    qint64 supported = 0;
    qint64 eligible = 0;
    qint64 nativeLetterOnly = 0;
    // Канонизация успешна, но токен содержит не-буквы родного языка
    // (например, us-пунктуацию на буквенных клавишах ru).
    qint64 supportedNotLetterOnly = 0;
    qint64 mixedScript = 0;
    qint64 unsupportedCharacter = 0;
    qint64 tooLong = 0;
    qint64 hadUppercase = 0;
    qint64 ambiguous = 0;
    qint64 wrongCopies = 0;
    // Wrong-копия не канонизировалась в те же клавиши (нарушение целостности).
    qint64 wrongCanonicalMismatches = 0;
    qint64 validTypes = 0;
    qint64 wrongTypes = 0;
};

struct NaturalCases {
    std::vector<EvalCase> cases;
    std::array<NaturalTokenStats, 2> stats;
};

// Каждый токен — valid-случай (намерение Keep) в родной раскладке языка.
// Для токенов isNativeLetterOnly дополнительно создаётся wrong-копия:
// convertLayout в другую раскладку с сохранением регистра. Неоднозначность —
// ключ есть в обоих полных словарях. Тип — (язык, роль, канонические
// клавиши) для supported, иначе (язык, роль, точный текст); представитель
// типа — первое вхождение.
[[nodiscard]] NaturalCases buildNaturalCases(const std::array<std::vector<QString>, 2> &tokens,
                                             const FeatureContext &context);

[[nodiscard]] QJsonObject naturalStatsJson(const NaturalTokenStats &stats);

// ---- Испытание, не зависящее от корпуса ----

struct ChallengeFixture {
    QString category;
    SourceLayout layout = SourceLayout::Us;
    QString text;
};

// Фиксированные искусственные токены с намерением Keep: URL и email на
// example.invalid, команды, пути, код, хеши, числа, смешанная письменность,
// имена, аббревиатуры.
[[nodiscard]] std::vector<ChallengeFixture> challengeFixtures();

struct ChallengeCase {
    QString category;
    SourceLayout layout = SourceLayout::Us;
    bool ambiguous = false;
    CanonicalStatus status = CanonicalStatus::Empty;
    int length = 0;
    CaseFeatures features;
};

[[nodiscard]] ChallengeCase makeChallengeCase(const QString &category, SourceLayout layout,
                                              QStringView text, const FeatureContext &context);

struct NoiseResult {
    std::vector<KeySequence> keys;
    qint64 attempts = 0;
    qint64 rejectedExcluded = 0;
    qint64 rejectedDuplicate = 0;
    bool complete = false;
};

// Попытка i: d = SHA-256(seed || 0x00 || ASCII-десятичный counter), затем
// counter += 1. Длина = minLength + d[0] mod (maxLength - minLength + 1),
// клавиша j = d[1 + j] mod 33 (небольшое смещение по модулю допускается и
// документировано). Ключи с excluded(keys) и повторы отбрасываются.
// Не больше maxAttempts попыток; complete = набрано count ключей.
// Параметры вне 1 <= minLength <= maxLength <= 31 — пустой результат.
[[nodiscard]] NoiseResult generateNoiseKeys(const QByteArray &seed, int count, int minLength,
                                            int maxLength, qint64 &counter, qint64 maxAttempts,
                                            const std::function<bool(const KeySequence &)> &excluded);

// Текст ключей в раскладке: us — базовый уровень, ru — строчные буквы ru.
[[nodiscard]] QString keysToLayoutText(const KeySequence &keys, SourceLayout layout);

[[nodiscard]] double percentileNearestRank(const std::vector<double> &sorted, double p);

} // namespace rasklad::benchmark
