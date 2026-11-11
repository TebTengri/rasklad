#include "frontcodedlexicon.h"

#include <algorithm>
#include <utility>

namespace rasklad::benchmark {
namespace {

constexpr char kLexiconMagic[4] = {'R', 'S', 'K', 'F'};
constexpr char kPairMagic[4] = {'R', 'S', 'K', 'P'};

void appendU32(QByteArray &out, quint32 value)
{
    for (int shift = 0; shift < 32; shift += 8)
        out.append(static_cast<char>((value >> shift) & 0xFFU));
}

// Вызывающий код проверяет длину заранее; at() дополнительно проверяет границы.
quint32 readU32(const QByteArray &data, qsizetype offset)
{
    quint32 value = 0;
    for (int i = 0; i < 4; ++i)
        value |= quint32(static_cast<unsigned char>(data.at(offset + i))) << (8 * i);
    return value;
}

bool hasMagic(const QByteArray &data, const char (&magic)[4])
{
    if (data.size() < 4)
        return false;
    for (qsizetype i = 0; i < 4; ++i) {
        if (data.at(i) != magic[i])
            return false;
    }
    return true;
}

quint8 keyByte(const KeySequence &key, qsizetype i)
{
    return static_cast<quint8>(static_cast<unsigned char>(key.at(i)));
}

// Лексикографическое сравнение по беззнаковым байтам, затем по длине.
int compareBuffer(const quint8 *a, int aLength, const KeySequence &b)
{
    const int bLength = int(b.size());
    const int common = std::min(aLength, bLength);
    for (int i = 0; i < common; ++i) {
        const quint8 bb = keyByte(b, i);
        if (a[i] != bb)
            return a[i] < bb ? -1 : 1;
    }
    if (aLength == bLength)
        return 0;
    return aLength < bLength ? -1 : 1;
}

int compareBuffers(const quint8 *a, int aLength, const quint8 *b, int bLength)
{
    const int common = std::min(aLength, bLength);
    for (int i = 0; i < common; ++i) {
        if (a[i] != b[i])
            return a[i] < b[i] ? -1 : 1;
    }
    if (aLength == bLength)
        return 0;
    return aLength < bLength ? -1 : 1;
}

int commonPrefix(const quint8 *a, int aLength, const quint8 *b, int bLength)
{
    const int common = std::min(aLength, bLength);
    int i = 0;
    while (i < common && a[i] == b[i])
        ++i;
    return i;
}

} // namespace

bool isValidCanonicalKey(const KeySequence &key)
{
    if (key.size() < kMinTokenKeys || key.size() > kMaxTokenKeys)
        return false;
    for (qsizetype i = 0; i < key.size(); ++i) {
        if (keyByte(key, i) >= NgramModel::kLetterKeys)
            return false;
    }
    return true;
}

std::optional<FrontCodedLexicon> FrontCodedLexicon::build(std::vector<KeySequence> keys,
                                                          BuildError *error)
{
    auto setError = [error](BuildError value) {
        if (error)
            *error = value;
    };
    setError(BuildError::None);
    for (const KeySequence &key : keys) {
        if (!isValidCanonicalKey(key)) {
            setError(BuildError::InvalidKey);
            return std::nullopt;
        }
    }
    // QByteArray сравнивает байты как беззнаковые; ключи < 33, знак не влияет.
    std::sort(keys.begin(), keys.end());
    keys.erase(std::unique(keys.begin(), keys.end()), keys.end());
    if (keys.size() > std::size_t(kMaxKeys)) {
        setError(BuildError::TooManyKeys);
        return std::nullopt;
    }

    FrontCodedLexicon lexicon;
    lexicon.m_count = quint32(keys.size());
    std::array<quint8, kMaxTokenKeys> previous{};
    int previousLength = 0;
    for (std::size_t i = 0; i < keys.size(); ++i) {
        const KeySequence &key = keys[i];
        const int length = int(key.size());
        std::array<quint8, kMaxTokenKeys> current{};
        for (int j = 0; j < length; ++j)
            current[std::size_t(j)] = keyByte(key, j);

        int prefix = 0;
        if (i % std::size_t(kBlockSize) == 0) {
            lexicon.m_offsets.push_back(quint32(lexicon.m_payload.size()));
            FirstKey first;
            first.length = quint8(length);
            first.bytes = current;
            lexicon.m_firstKeys.push_back(first);
        } else {
            prefix = commonPrefix(previous.data(), previousLength, current.data(), length);
        }
        lexicon.m_payload.push_back(quint8(prefix));
        lexicon.m_payload.push_back(quint8(length - prefix));
        for (int j = prefix; j < length; ++j)
            lexicon.m_payload.push_back(current[std::size_t(j)]);
        previous = current;
        previousLength = length;
    }
    if (lexicon.serializedBytes() > kMaxSerializedBytes) {
        setError(BuildError::TooManyKeys);
        return std::nullopt;
    }
    lexicon.m_payload.shrink_to_fit();
    lexicon.m_offsets.shrink_to_fit();
    lexicon.m_firstKeys.shrink_to_fit();
    return lexicon;
}

qint64 FrontCodedLexicon::serializedBytes() const
{
    return qint64(kHeaderBytes) + 4 * qint64(m_offsets.size()) + qint64(m_payload.size());
}

qint64 FrontCodedLexicon::residentStructureBytes() const
{
    return qint64(m_payload.size()) + qint64(m_offsets.size() * sizeof(quint32))
        + qint64(m_firstKeys.size() * sizeof(FirstKey));
}

QByteArray FrontCodedLexicon::serialize() const
{
    QByteArray out;
    out.reserve(qsizetype(serializedBytes()));
    out.append(kLexiconMagic, 4);
    appendU32(out, kFormatVersion);
    appendU32(out, m_count);
    appendU32(out, quint32(kBlockSize));
    appendU32(out, quint32(m_offsets.size()));
    appendU32(out, quint32(m_payload.size()));
    for (const quint32 offset : m_offsets)
        appendU32(out, offset);
    for (const quint8 byte : m_payload)
        out.append(static_cast<char>(byte));
    return out;
}

std::optional<FrontCodedLexicon> FrontCodedLexicon::deserialize(const QByteArray &data)
{
    if (data.size() < kHeaderBytes || data.size() > kMaxSerializedBytes)
        return std::nullopt;
    if (!hasMagic(data, kLexiconMagic))
        return std::nullopt;
    if (readU32(data, 4) != kFormatVersion || readU32(data, 12) != quint32(kBlockSize))
        return std::nullopt;
    const quint32 count = readU32(data, 8);
    const quint32 blockCount = readU32(data, 16);
    const quint32 payloadBytes = readU32(data, 20);
    if (count > kMaxKeys)
        return std::nullopt;
    const quint64 expectedBlocks = (quint64(count) + kBlockSize - 1) / kBlockSize;
    if (quint64(blockCount) != expectedBlocks)
        return std::nullopt;
    const qint64 expectedSize = qint64(kHeaderBytes) + 4 * qint64(blockCount) + qint64(payloadBytes);
    if (expectedSize != qint64(data.size()))
        return std::nullopt;
    // Каждая запись занимает минимум 3 байта (два заголовка и суффикс >= 1).
    if (quint64(payloadBytes) < 3 * quint64(count))
        return std::nullopt;
    // Пустой словарь не содержит ни блоков, ни байтов нагрузки.
    if (count == 0 && payloadBytes != 0)
        return std::nullopt;

    FrontCodedLexicon lexicon;
    lexicon.m_count = count;
    lexicon.m_offsets.reserve(blockCount);
    const qsizetype offsetsStart = kHeaderBytes;
    for (quint32 b = 0; b < blockCount; ++b) {
        const quint32 offset = readU32(data, offsetsStart + 4 * qsizetype(b));
        if (b == 0 && offset != 0)
            return std::nullopt;
        if (b > 0 && offset <= lexicon.m_offsets.back())
            return std::nullopt;
        if (offset >= payloadBytes)
            return std::nullopt;
        lexicon.m_offsets.push_back(offset);
    }
    const qsizetype payloadStart = offsetsStart + 4 * qsizetype(blockCount);
    lexicon.m_payload.resize(payloadBytes);
    for (quint32 i = 0; i < payloadBytes; ++i)
        lexicon.m_payload[i] = static_cast<quint8>(static_cast<unsigned char>(data.at(payloadStart + qsizetype(i))));

    lexicon.m_firstKeys.reserve(blockCount);
    std::array<quint8, kMaxTokenKeys> previous{};
    int previousLength = 0;
    bool havePrevious = false;
    for (quint32 b = 0; b < blockCount; ++b) {
        const quint32 start = lexicon.m_offsets[b];
        const quint32 end = b + 1 < blockCount ? lexicon.m_offsets[b + 1] : payloadBytes;
        const quint32 records = std::min<quint32>(quint32(kBlockSize), count - b * quint32(kBlockSize));
        quint32 pos = start;
        for (quint32 r = 0; r < records; ++r) {
            if (end - pos < 2)
                return std::nullopt;
            const int prefix = lexicon.m_payload[pos];
            const int suffix = lexicon.m_payload[pos + 1];
            pos += 2;
            if (r == 0 && prefix != 0)
                return std::nullopt;
            if (prefix > previousLength)
                return std::nullopt;
            if (suffix < 1)
                return std::nullopt;
            const int length = prefix + suffix;
            if (length < kMinTokenKeys || length > kMaxTokenKeys)
                return std::nullopt;
            if (quint32(suffix) > end - pos)
                return std::nullopt;
            std::array<quint8, kMaxTokenKeys> current = previous;
            for (int j = 0; j < suffix; ++j) {
                const quint8 byte = lexicon.m_payload[pos + quint32(j)];
                if (byte >= NgramModel::kLetterKeys)
                    return std::nullopt;
                current[std::size_t(prefix + j)] = byte;
            }
            pos += quint32(suffix);
            if (havePrevious) {
                if (compareBuffers(previous.data(), previousLength, current.data(), length) >= 0)
                    return std::nullopt;
                // Каноническая форма: prefixLen внутри блока равен общему префиксу.
                if (r > 0 && commonPrefix(previous.data(), previousLength, current.data(), length) != prefix)
                    return std::nullopt;
            }
            if (r == 0) {
                FirstKey first;
                first.length = quint8(length);
                first.bytes = current;
                lexicon.m_firstKeys.push_back(first);
            }
            previous = current;
            previousLength = length;
            havePrevious = true;
        }
        if (pos != end)
            return std::nullopt;
    }
    return lexicon;
}

int FrontCodedLexicon::compareWithFirstKey(const FirstKey &first, const KeySequence &key) const
{
    return compareBuffer(first.bytes.data(), first.length, key);
}

bool FrontCodedLexicon::contains(const KeySequence &key) const
{
    if (m_count == 0 || !isValidCanonicalKey(key))
        return false;
    // Последний блок, первый ключ которого <= key.
    std::size_t lo = 0;
    std::size_t hi = m_firstKeys.size();
    while (lo < hi) {
        const std::size_t mid = lo + (hi - lo) / 2;
        if (compareWithFirstKey(m_firstKeys[mid], key) <= 0)
            lo = mid + 1;
        else
            hi = mid;
    }
    if (lo == 0)
        return false;
    const std::size_t block = lo - 1;
    const quint32 records =
        std::min<quint32>(quint32(kBlockSize), m_count - quint32(block) * quint32(kBlockSize));
    // Данные проверены при построении/загрузке, индексы не выходят за границы.
    std::size_t pos = m_offsets[block];
    std::array<quint8, kMaxTokenKeys> buffer{};
    for (quint32 r = 0; r < records; ++r) {
        const int prefix = m_payload[pos];
        const int suffix = m_payload[pos + 1];
        pos += 2;
        for (int j = 0; j < suffix; ++j)
            buffer[std::size_t(prefix + j)] = m_payload[pos + std::size_t(j)];
        pos += std::size_t(suffix);
        const int cmp = compareBuffer(buffer.data(), prefix + suffix, key);
        if (cmp == 0)
            return true;
        if (cmp > 0)
            return false;
    }
    return false;
}

std::vector<KeySequence> FrontCodedLexicon::decodeAll() const
{
    std::vector<KeySequence> out;
    out.reserve(m_count);
    std::array<quint8, kMaxTokenKeys> buffer{};
    std::size_t pos = 0;
    for (quint32 i = 0; i < m_count; ++i) {
        const int prefix = m_payload[pos];
        const int suffix = m_payload[pos + 1];
        pos += 2;
        for (int j = 0; j < suffix; ++j)
            buffer[std::size_t(prefix + j)] = m_payload[pos + std::size_t(j)];
        pos += std::size_t(suffix);
        KeySequence key;
        key.reserve(prefix + suffix);
        for (int j = 0; j < prefix + suffix; ++j)
            key.append(static_cast<char>(buffer[std::size_t(j)]));
        out.push_back(key);
    }
    return out;
}

QByteArray serializeLexiconPair(const FrontCodedLexicon &en, const FrontCodedLexicon &ru)
{
    const QByteArray enBytes = en.serialize();
    const QByteArray ruBytes = ru.serialize();
    QByteArray out;
    out.reserve(kLexiconPairHeaderBytes + enBytes.size() + ruBytes.size());
    out.append(kPairMagic, 4);
    appendU32(out, kLexiconPairVersion);
    appendU32(out, 2);
    appendU32(out, quint32(enBytes.size()));
    appendU32(out, quint32(ruBytes.size()));
    out.append(enBytes);
    out.append(ruBytes);
    return out;
}

std::optional<std::array<FrontCodedLexicon, 2>> deserializeLexiconPair(const QByteArray &data)
{
    if (data.size() < kLexiconPairHeaderBytes
        || qint64(data.size()) > kLexiconPairHeaderBytes + 2 * FrontCodedLexicon::kMaxSerializedBytes)
        return std::nullopt;
    if (!hasMagic(data, kPairMagic))
        return std::nullopt;
    if (readU32(data, 4) != kLexiconPairVersion || readU32(data, 8) != 2)
        return std::nullopt;
    const qint64 enBytes = readU32(data, 12);
    const qint64 ruBytes = readU32(data, 16);
    if (enBytes > FrontCodedLexicon::kMaxSerializedBytes || ruBytes > FrontCodedLexicon::kMaxSerializedBytes)
        return std::nullopt;
    if (qint64(kLexiconPairHeaderBytes) + enBytes + ruBytes != qint64(data.size()))
        return std::nullopt;
    std::optional<FrontCodedLexicon> en =
        FrontCodedLexicon::deserialize(data.mid(kLexiconPairHeaderBytes, qsizetype(enBytes)));
    if (!en)
        return std::nullopt;
    std::optional<FrontCodedLexicon> ru =
        FrontCodedLexicon::deserialize(data.mid(kLexiconPairHeaderBytes + qsizetype(enBytes), qsizetype(ruBytes)));
    if (!ru)
        return std::nullopt;
    return std::array<FrontCodedLexicon, 2>{{std::move(*en), std::move(*ru)}};
}

} // namespace rasklad::benchmark
