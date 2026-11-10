#pragma once

// Подготовка данных offline-эксперимента SYS-232: разбор Hunspell .dic и
// детерминированное разделение train/calibration/test.
//
// Только для tools/detector_experiment и тестов. Не входит в ядро и не
// должна попадать в runtime приложения. Функции работают с переданными
// байтами в памяти: не открывают файлы, не пишут на диск, не логируют.

#include "ngramdetector.h"

#include <QByteArray>
#include <QString>
#include <QStringList>

#include <vector>

namespace rasklad::experiment {

// Верхняя граница размера входного файла .dic.
inline constexpr qint64 kMaxDicBytes = 64LL * 1024 * 1024;
// Слова длиннее отбрасываются (совпадает с пределом канонизации).
inline constexpr int kMaxWordChars = kMaxTokenKeys;

// Версии фиксируются в JSON-отчёте. При изменении алгоритма — менять.
inline constexpr char kSplitSeed[] = "rasklad-SYS-232-split-v1";
inline constexpr char kNormalizationVersion[] =
    "v1: strict-utf8, strip-bom, strip-cr, cut-at-slash-or-tab, lowercase, letter-only, 1..32";
// Доли по 100 корзинам: [0,70) train, [70,85) calibration, [85,100) test.
inline constexpr int kTrainBuckets = 70;
inline constexpr int kCalibrationBuckets = 15;
// Группа разделения — первые 3 канонические клавиши (proxy семейства слов).
inline constexpr int kGroupPrefixKeys = 3;

enum class DicError {
    None,
    TooLarge,
    InvalidUtf8,
    MissingHeader,
    BadHeader,
    // Число записей после заголовка не равно числу в заголовке.
    CountMismatch,
    // Значение DetectorLanguage вне перечисления.
    InvalidLanguage,
};

// Сообщение без содержимого файла.
[[nodiscard]] QString dicErrorText(DicError error);

struct DicFilterReport {
    qint64 rawBytes = 0;
    bool bomStripped = false;
    qint64 headerCount = -1;
    // Строки после заголовка (включая пустые).
    qint64 entryLines = 0;
    qint64 emptyLines = 0;
    qint64 crlfLines = 0;
    qint64 rejectedNotLetters = 0;
    qint64 rejectedLength = 0;
    qint64 rejectedMultiword = 0;
    qint64 duplicates = 0;
    qint64 accepted = 0;
};

struct DicParseResult {
    DicError error = DicError::None;
    DicFilterReport report;
    // Уникальные слова в нижнем регистре, отсортированы.
    QStringList words;
};

// Разбор Hunspell .dic: первая строка — число записей; в каждой записи
// берётся слово до '/' (флаги аффиксов) или табуляции. Аффиксы не
// разворачиваются, .aff не используется. Строки в CRLF принимаются, UTF-8 BOM
// снимается, иная кодировка или некорректный UTF-8 — отказ всего файла.
// Остаются только слова из букв языка: en — a..z, ru — 33 буквы ru(default).
//
// Fail closed: число записей (все строки после заголовка, включая пустые;
// пустой хвост после финального '\n' записью не считается) должно точно
// совпадать с числом в заголовке, иначе CountMismatch и пустой список слов.
[[nodiscard]] DicParseResult parseHunspellDic(const QByteArray &data, DetectorLanguage language);

enum class SplitPart { Train = 0, Calibration = 1, Test = 2 };

// Группа: первые min(3, n) клавиш. Один ключ (а значит и его EN/RU
// варианты, и wrong-layout копия) всегда попадает в одну часть.
[[nodiscard]] KeySequence splitGroup(const KeySequence &keys);

// SHA-256(seed || 0x00 || group): первые 8 байт big-endian mod 100.
[[nodiscard]] int splitBucket(const KeySequence &keys);
[[nodiscard]] SplitPart splitForKeys(const KeySequence &keys);

[[nodiscard]] const char *splitName(SplitPart part);

// Слово, уже отнесённое к части разделения.
struct SplitEntry {
    QString word;
    KeySequence keys;
    SplitPart part = SplitPart::Train;
};

struct LeakReport {
    // Канонические ключи (оба языка вместе) в разных частях.
    qint64 canonicalKeyLeaks = 0;
    // Одно слово языка в разных частях.
    qint64 wordLeaks = 0;
    // Wrong-layout копия слова одного языка совпадает со словом другого
    // языка из другой части.
    qint64 wrongLayoutMappingLeaks = 0;

    [[nodiscard]] bool leakFree() const
    {
        return canonicalKeyLeaks == 0 && wordLeaks == 0 && wrongLayoutMappingLeaks == 0;
    }
};

// Считает пересечения между частями. Пары частей для ключей и слов
// учитываются один раз (a < b), для отображений — в обе стороны.
[[nodiscard]] LeakReport countSplitLeaks(const std::vector<SplitEntry> &en,
                                         const std::vector<SplitEntry> &ru);

} // namespace rasklad::experiment
