#pragma once

// Экспериментальный n-граммный scorer ошибочной раскладки (SYS-232).
//
// Модуль чистый: без I/O, сети, логирования, таймеров и глобального
// изменяемого состояния. Он не выполняет замену текста и не подключён к
// вводу. Результаты обучения на словарях — материал offline-эксперимента
// (tools/detector_experiment), а не проверенный production-детектор.
//
// Модель хранит только числовые таблицы, версию формата и шкалу
// квантования. Исходные слова и токены в ней не сохраняются.

#include <QByteArray>
#include <QString>
#include <QStringView>

#include <array>
#include <optional>
#include <vector>

namespace rasklad {

// Каноническая последовательность физических клавиш: каждый байт — индекс
// 0..32 одной из 33 клавиш, на которых в XKB ru(default) находятся буквы
// (порядок см. kCanonicalKeyOrder в .cpp). BOS/EOS сюда не входят.
using KeySequence = QByteArray;

// Канал модели. Значения используются как индексы массивов только после
// проверки isValidLanguage: значение, полученное приведением произвольного
// целого, массивы не индексирует.
enum class DetectorLanguage { En = 0, Ru = 1 };

// Явно известная раскладка, в которой набран токен. Детектор её не угадывает.
enum class SourceLayout { Us, Ru };

[[nodiscard]] bool isValidLanguage(DetectorLanguage language);
[[nodiscard]] bool isValidSourceLayout(SourceLayout source);

// Предусловие: isValidSourceLayout(source). Для недопустимого значения
// возвращается En, но canonicalizeToken и decide* такое значение отвергают
// до вызова этих функций.
[[nodiscard]] DetectorLanguage observedLanguage(SourceLayout source);
[[nodiscard]] DetectorLanguage alternativeLanguage(SourceLayout source);

enum class CanonicalStatus {
    Ok,
    Empty,
    TooLong,
    // Символ другой письменности: кириллица в us-токене или латиница в ru-токене.
    MixedScript,
    // Цифры, дефис, апостроф ’, combining marks, иные символы вне 33 клавиш.
    UnsupportedCharacter,
    // Значение SourceLayout вне перечисления.
    InvalidLayout,
};

struct CanonicalToken {
    CanonicalStatus status = CanonicalStatus::Empty;
    KeySequence keys;
    // Признак сохраняется для вызывающего кода (например, чтобы восстановить
    // регистр), но не используется scorer'ом и не повышает уверенность.
    bool hadUppercase = false;
};

// Длины канонических последовательностей, которые принимает модуль.
inline constexpr int kMinTokenKeys = 1;
inline constexpr int kMaxTokenKeys = 32;
// Токены короче этого порога решения всегда получают Abstain:
// short-table/отдельная модель для длин 1..3 отложены.
inline constexpr int kMinDecisionKeys = 4;

// Переводит токен в канонические клавиши с учётом явной исходной раскладки.
// Регистр нормализуется (Shift-уровень us-пунктуации, например '{' -> '[',
// тоже). Токен не делится по пунктуации: "k.,jdm" в us — допустимый токен.
// Используется существующий rasklad::convertLayout; исходная строка не
// сохраняется в результате.
[[nodiscard]] CanonicalToken canonicalizeToken(QStringView token, SourceLayout source);

// Строка базового уровня us для канонических клавиш (для проверок и
// воспроизведения в эксперименте). Пустая строка для недопустимого входа.
[[nodiscard]] QString keysToUsBase(const KeySequence &keys);

struct TokenScore {
    bool ok = false;
    int keyCount = 0;
    // Число предсказываемых символов: keyCount + 1 (последним идёт EOS).
    int steps = 0;
    // Сумма квантованных взвешенных log-оценок, индекс — DetectorLanguage.
    std::array<qint32, 2> raw{{0, 0}};

    // Нормированная оценка: raw / (kScale * steps). Единица — «натов на шаг»
    // суммы трёх сглаженных log-оценок. Это не вероятность и не
    // калиброванная уверенность. Для !ok, steps <= 0 или недопустимого
    // language возвращает NaN.
    [[nodiscard]] double perStep(DetectorLanguage language) const;
};

// TokenScore — открытая структура, её можно собрать вручную. Решения
// принимаются только для формы, которую может выдать NgramModel::score:
// ok, keyCount в [kMinTokenKeys, kMaxTokenKeys], steps == keyCount + 1,
// каждое raw в [steps * kMinStepRaw, 0].
[[nodiscard]] bool isWellFormedScore(const TokenScore &score);

class NgramModel
{
public:
    // Алфавит: 33 клавиши букв + BOS + EOS.
    static constexpr int kLetterKeys = 33;
    static constexpr int kBos = 33;
    static constexpr int kEos = 34;
    static constexpr int kAlphabet = 35;
    static constexpr int kUnigramSize = kAlphabet;
    static constexpr int kBigramSize = kAlphabet * kAlphabet;
    static constexpr int kTrigramSize = kAlphabet * kAlphabet * kAlphabet;
    static constexpr int kTableSize = kUnigramSize + kBigramSize + kTrigramSize; // 44 135
    static constexpr int kWeightBytes = 2 * kTableSize;                          // 88 270

    // Фиксированная шкала квантования: q = round(ln P * kScale), q в [-127, 0].
    static constexpr int kScale = 8;
    static constexpr int kMinQuantized = -127;
    // Параметр add-alpha сглаживания при обучении (в модели не хранится).
    static constexpr double kAlpha = 0.5;
    // Веса порядков 1, 2, 3 в сумме log-оценок.
    static constexpr std::array<int, 3> kOrderWeights{{1, 1, 1}};
    // Наименьший вклад одного шага в raw: сумма весов * kMinQuantized (-381).
    static constexpr qint32 kMinStepRaw =
        (kOrderWeights[0] + kOrderWeights[1] + kOrderWeights[2]) * kMinQuantized;

    static constexpr quint32 kFormatVersion = 1;
    static constexpr int kHeaderBytes = 20;
    static constexpr int kSerializedBytes = kHeaderBytes + kWeightBytes;

    NgramModel() = default;

    // Обучение по спискам канонических последовательностей. Каждый элемент
    // учитывается один раз; недопустимые (длина вне 1..32, индекс > 32)
    // пропускаются, их число возвращается через skipped.
    [[nodiscard]] static NgramModel train(const std::vector<KeySequence> &en,
                                          const std::vector<KeySequence> &ru,
                                          qsizetype *skipped = nullptr);

    [[nodiscard]] bool isValid() const;

    // Формат v1 (little-endian):
    //   "RSKN" | u32 version | u32 scale | u32 alphabet | u32 tableSize |
    //   int8[tableSize] EN | int8[tableSize] RU.
    // Таблица языка: unigram[c], bigram[a*35+c], trigram[(a*35+b)*35+c].
    [[nodiscard]] QByteArray serialize() const;
    [[nodiscard]] static std::optional<NgramModel> deserialize(const QByteArray &data);

    [[nodiscard]] TokenScore score(const KeySequence &keys) const;

    // Для недопустимого language — пустая таблица.
    [[nodiscard]] const std::vector<qint8> &table(DetectorLanguage language) const;

private:
    std::array<std::vector<qint8>, 2> m_tables;
};

enum class DecisionAction { Keep, Correct, Abstain };

enum class DecisionReason {
    Unsupported,
    TooShort,
    Disabled,
    ObservedPreferred,
    AlternativePreferred,
    Uncertain,
};

struct DetectorDecision {
    DecisionAction action = DecisionAction::Abstain;
    DecisionReason reason = DecisionReason::Unsupported;
};

// Пороги задаются вызывающим кодом (в эксперименте — калибровкой).
// Значений по умолчанию, пригодных для production, нет: disabled.
struct SingleDThresholds {
    bool enabled = false;
    double gap = 0.0;
};

struct TwoScoreThresholds {
    bool enabled = false;
    double minAlternative = 0.0;
    double maxObserved = 0.0;
    double gap = 0.0;
};

// Обе политики возвращают Abstain/Unsupported для !isWellFormedScore(score)
// и недопустимого source, Abstain/TooShort для keyCount < kMinDecisionKeys,
// Abstain/Disabled для выключенных или нечисловых порогов и gap < 0.
//
// Single-D baseline: D = S_alt - S_obs.
//   D >= gap и D > 0  -> Correct;
//   -D >= gap и D < 0 -> Keep;
//   иначе             -> Abstain.
[[nodiscard]] DetectorDecision decideSingleD(const TokenScore &score, SourceLayout source,
                                             const SingleDThresholds &thresholds);

// Две оценки с reject-регионом:
//   S_alt >= minAlternative, S_obs <= maxObserved, S_alt - S_obs >= gap,
//   S_alt > S_obs                      -> Correct;
//   S_obs > maxObserved и S_obs >= S_alt -> Keep (текущий вариант убедителен);
//   иначе                              -> Abstain.
[[nodiscard]] DetectorDecision decideTwoScore(const TokenScore &score, SourceLayout source,
                                              const TwoScoreThresholds &thresholds);

} // namespace rasklad
