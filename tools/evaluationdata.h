#pragma once

// Независимые данные оценки SYS-233: паспорт data/evaluation-002-sources.json,
// строгий разбор Tatoeba TSV (id<TAB>iso<TAB>sentence), детерминированная
// выборка предложений и токенизация по замороженному протоколу.
//
// Только для tools/detector_benchmark и тестов. Функции работают с байтами
// в памяти: не открывают файлы, не пишут на диск, не логируют. Сообщения об
// ошибках не содержат ни текста, ни идентификаторов, ни номеров строк.

#include "ngramdetector.h"

#include <QByteArray>
#include <QString>
#include <QStringView>

#include <array>
#include <vector>

namespace rasklad::benchmark {

inline constexpr qint64 kMaxEvalManifestBytes = 64LL * 1024;
inline constexpr qint64 kMaxEvalSourceBytes = 32LL * 1024 * 1024;
inline constexpr int kMaxRowBytes = 8192;
// Верхняя граница записей TSV (финальный '\n' необязателен и запись не
// добавляет). Проверяется подсчётом '\n' в сырых байтах до декодирования,
// split(), reserve() и QSet ID, чтобы поток переводов строк в пределах
// kMaxEvalSourceBytes не порождал миллионы QStringView. Предел выше
// размера одобренных источников и не меняет их разбор.
inline constexpr qint64 kMaxEvalRecords = 250000;
inline constexpr int kMaxSentenceUtf16 = 4096;
inline constexpr int kMaxIdDigits = 18;

inline constexpr char kEvalRequiredStatus[] = "approved-local-evaluation";
inline constexpr char kEvalRequiredScope[] = "local-evaluation-only";

// Фиксированный набор внешних символов, снимаемых с краёв токена.
// Совпадает с tokenization.outer_trim_characters протокола.
[[nodiscard]] QString outerTrimCharacters();

// ISO 639-3 код языка Tatoeba: eng / rus.
[[nodiscard]] const char *isoLanguageFor(DetectorLanguage language);

// ---- Паспорт источников оценки ----

enum class EvalManifestError {
    None,
    TooLarge,
    InvalidJson,
    SchemaVersion,
    StatusNotApproved,
    ScopeMismatch,
    ReleaseApproved,
    // Нужны ровно два объекта: en и ru, без повторов и лишних элементов.
    BadSources,
    BadSourceField,
    BadIsoLanguage,
    BadNoticePath,
};

[[nodiscard]] QString evalManifestErrorText(EvalManifestError error);

struct EvalSource {
    QString isoLanguage;
    QString sha256;
    qint64 bytes = 0;
    QString notice;
    QString noticeSha256;
};

struct EvalManifest {
    QString manifestSha256;
    QString status;
    QString scope;
    // Индекс — DetectorLanguage.
    std::array<EvalSource, 2> sources;
};

struct EvalManifestResult {
    EvalManifestError error = EvalManifestError::None;
    EvalManifest manifest;
};

// Проверки: размер <= 64 KiB, корень — объект, schema_version == 1,
// status/scope равны обязательным значениям, release_approved — false,
// evaluation_sources — ровно en и ru. У источника: iso_language eng/rus
// для en/ru, sha256 и notice_sha256 — 64 строчных hex, bytes в
// [1, kMaxEvalSourceBytes], notice — безопасный путь под data/notices/.
// Дополнительные описательные поля (url, лицензия) допускаются и не
// используются.
[[nodiscard]] EvalManifestResult parseEvaluationManifest(const QByteArray &bytes);

// ---- Tatoeba TSV ----

enum class TsvError {
    None,
    TooLarge,
    Empty,
    InvalidUtf8,
    EmptyLine,
    RowTooLong,
    ControlCharacter,
    BadColumns,
    BadId,
    DuplicateId,
    LanguageMismatch,
    EmptySentence,
    SentenceTooLong,
    // Записей больше kMaxEvalRecords.
    TooManyRecords,
};

[[nodiscard]] QString tsvErrorText(TsvError error);

struct SentenceRecord {
    qint64 id = 0;
    QString text;
};

struct TsvParseResult {
    TsvError error = TsvError::None;
    std::vector<SentenceRecord> records;
};

// Строгий разбор: весь файл — корректный UTF-8 (BOM не снимается и
// приводит к отказу ID), не больше kMaxEvalSourceBytes и не больше
// kMaxEvalRecords записей (до декодирования). Записи разделены
// '\n'; финальный '\n' необязателен, пустые строки запрещены. В строке
// не больше kMaxRowBytes байт UTF-8 и ровно два '\t'. ID — 1..18 ASCII
// цифр без ведущего нуля, > 0, уникален. Второй столбец равен isoLanguage.
// Предложение непустое, не длиннее kMaxSentenceUtf16 кодовых единиц.
// Управляющие символы (категория Cc, включая '\r', NUL, DEL, C1) кроме
// разделителей записей и столбцов запрещены. Любая ошибка — отказ всего
// файла и пустой список.
[[nodiscard]] TsvParseResult parseTatoebaTsv(const QByteArray &data, QStringView isoLanguage);

// Длина строки в байтах UTF-8 (одиночный суррогат считается как U+FFFD).
[[nodiscard]] qint64 utf8Length(QStringView text);

// ---- Выборка ----

struct SelectionReport {
    qint64 records = 0;
    qint64 uniqueTexts = 0;
    qint64 duplicateTextRecords = 0;
    qint64 selected = 0;
    // SHA-256 выбранных ID в порядке ранга, каждый — ASCII-десятичный и '\n'.
    QString selectedIdsSha256;
};

struct SelectionResult {
    SelectionReport report;
    std::vector<SentenceRecord> selected;
};

// SHA-256(seed || 0x00 || ASCII-десятичный ID).
[[nodiscard]] QByteArray sampleRankDigest(const QByteArray &seed, qint64 id);

// Точные повторы текста (без нормализации) оставляют запись с наименьшим
// числовым ID. Затем ранг sampleRankDigest по возрастанию (беззнаковое
// побайтовое сравнение), равные ранги — по меньшему ID. Берутся первые
// limit записей. Токенизация и фильтрация выполняются после выборки.
[[nodiscard]] SelectionResult selectSentences(std::vector<SentenceRecord> records,
                                              const QByteArray &seed, int limit);

// ---- Токенизация ----

struct TokenizeStats {
    // Фрагменты между пробельными символами Unicode (QChar::isSpace).
    qint64 pieces = 0;
    // Фрагменты, ставшие пустыми после снятия внешних символов.
    qint64 emptyAfterTrim = 0;
    // Токены, у которых был снят хотя бы один внешний символ.
    qint64 trimmed = 0;
};

// Делит по пробельным символам Unicode и снимает с обоих краёв только
// символы outerTrimCharacters(). Внутренняя пунктуация сохраняется,
// регистр не меняется, фильтрации по модели нет. Пустые токены
// отбрасываются и учитываются в stats.
[[nodiscard]] std::vector<QString> tokenizeSentence(QStringView sentence, TokenizeStats *stats = nullptr);

// Только буквы родного языка в любом регистре, длина 1..32:
// en — A-Z/a-z, ru — 33 буквы ru(default) А-Я/а-я/Ё/ё.
[[nodiscard]] bool isNativeLetterOnly(QStringView token, DetectorLanguage language);

} // namespace rasklad::benchmark
