#pragma once

// Компактный точный словарь канонических ключей для offline-эксперимента
// SYS-233 (tools/detector_benchmark и тесты). Не входит в ядро и runtime.
//
// Представление: отсортированные уникальные ключи (байты 0..32, длина
// 1..32), разбитые на блоки по kBlockSize. Каждая запись — prefixLen (u8),
// suffixLen (u8) и suffixLen байт суффикса; первая запись блока хранится
// целиком (prefixLen = 0). Байт 0 — допустимая клавиша, поэтому нигде нет
// предположений о NUL-терминированных строках.
//
// Поиск: двоичный поиск по декодированным первым ключам блоков, затем
// последовательное декодирование не более kBlockSize записей одного блока
// и точное сравнение. Вероятностного членства нет: ложноположительных и
// ложноотрицательных ответов не бывает для корректно загруженных данных.
//
// Это простая учебная структура для измерения, а не оптимизированный
// промышленный FST/DAFSA.
//
// Сериализация v1 (little-endian):
//   "RSKF" | u32 version | u32 count | u32 blockSize | u32 blockCount |
//   u32 payloadBytes | u32 offsets[blockCount] | payload[payloadBytes].
// Размер файла равен сумме частей точно; хвостовые байты — отказ.
//
// Модуль чистый: без I/O, сети и логирования.

#include "ngramdetector.h"

#include <QByteArray>

#include <array>
#include <optional>
#include <vector>

namespace rasklad::benchmark {

class FrontCodedLexicon
{
public:
    static constexpr quint32 kFormatVersion = 1;
    static constexpr int kBlockSize = 16;
    static constexpr int kHeaderBytes = 24;
    static constexpr quint32 kMaxKeys = 1024U * 1024U;
    static constexpr qint64 kMaxSerializedBytes = 64LL * 1024 * 1024;

    enum class BuildError { None, InvalidKey, TooManyKeys };

    FrontCodedLexicon() = default;

    // Ключи сортируются, повторы удаляются. Любой ключ вне длины 1..32 или
    // с байтом > 32 — отказ всего построения.
    [[nodiscard]] static std::optional<FrontCodedLexicon> build(std::vector<KeySequence> keys,
                                                                BuildError *error = nullptr);

    // Полная проверка: магия, версия, размер блока, число блоков, границы и
    // строгий рост смещений, длины записей, prefixLen <= длины предыдущего
    // ключа и равен их общему префиксу, байты клавиш, строгая сортировка,
    // точное заполнение каждого блока и отсутствие хвостовых данных.
    [[nodiscard]] static std::optional<FrontCodedLexicon> deserialize(const QByteArray &data);

    [[nodiscard]] QByteArray serialize() const;

    // Точное членство. Недопустимый ключ (пустой, длиннее 32, байт > 32) — false.
    [[nodiscard]] bool contains(const KeySequence &key) const;

    // Все ключи по порядку (для проверок; не используется в поиске).
    [[nodiscard]] std::vector<KeySequence> decodeAll() const;

    [[nodiscard]] qint64 keyCount() const { return qint64(m_count); }
    [[nodiscard]] qint64 blockCount() const { return qint64(m_offsets.size()); }
    [[nodiscard]] qint64 payloadBytes() const { return qint64(m_payload.size()); }
    [[nodiscard]] qint64 serializedBytes() const;
    // Байты полезной нагрузки контейнеров в памяти: payload + смещения +
    // каталог первых ключей. Без накладных расходов аллокатора и Qt; это не
    // RSS процесса.
    [[nodiscard]] qint64 residentStructureBytes() const;

private:
    struct FirstKey {
        quint8 length = 0;
        std::array<quint8, kMaxTokenKeys> bytes{};
    };

    [[nodiscard]] int compareWithFirstKey(const FirstKey &first, const KeySequence &key) const;

    quint32 m_count = 0;
    std::vector<quint8> m_payload;
    std::vector<quint32> m_offsets;
    std::vector<FirstKey> m_firstKeys;
};

// Пара EN/RU в одном файле (v1, little-endian):
//   "RSKP" | u32 version | u32 languages (=2) | u32 enBytes | u32 ruBytes |
//   enBytes байт FrontCodedLexicon EN | ruBytes байт FrontCodedLexicon RU.
// Размеры частей строгие, хвостовые байты — отказ. Индекс — DetectorLanguage.
inline constexpr quint32 kLexiconPairVersion = 1;
inline constexpr int kLexiconPairHeaderBytes = 20;

[[nodiscard]] QByteArray serializeLexiconPair(const FrontCodedLexicon &en, const FrontCodedLexicon &ru);
[[nodiscard]] std::optional<std::array<FrontCodedLexicon, 2>> deserializeLexiconPair(const QByteArray &data);

// Допустимый канонический ключ: длина 1..32, каждый байт < 33.
[[nodiscard]] bool isValidCanonicalKey(const KeySequence &key);

} // namespace rasklad::benchmark
