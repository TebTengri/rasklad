#include "detectordataset.h"

#include "layoutconverter.h"

#include <QCryptographicHash>
#include <QSet>
#include <QStringDecoder>
#include <QStringView>

#include <algorithm>
#include <array>

namespace rasklad::experiment {
namespace {

bool isLanguageLetter(char16_t c, DetectorLanguage language)
{
    if (language == DetectorLanguage::En)
        return c >= u'a' && c <= u'z';
    return (c >= u'а' && c <= u'я') || c == u'ё';
}

bool isLetterOnly(QStringView word, DetectorLanguage language)
{
    if (word.isEmpty())
        return false;
    return std::all_of(word.begin(), word.end(),
                       [language](QChar ch) { return isLanguageLetter(ch.unicode(), language); });
}

// Поле морфологического описания Hunspell вида "po:noun".
bool looksLikeMorphField(QStringView field)
{
    return field.size() >= 3 && field.at(0).unicode() >= u'a' && field.at(0).unicode() <= u'z'
        && field.at(1).unicode() >= u'a' && field.at(1).unicode() <= u'z'
        && field.at(2) == u':';
}

bool isAsciiDigits(QStringView text)
{
    return !text.isEmpty()
        && std::all_of(text.begin(), text.end(),
                       [](QChar ch) { return ch.unicode() >= u'0' && ch.unicode() <= u'9'; });
}

enum class EntryResult { Word, NotLetters, Multiword };

// Извлекает слово записи без флагов и морфологических полей.
EntryResult extractWord(QStringView line, QString &word)
{
    qsizetype cut = line.size();
    for (qsizetype i = 0; i < line.size(); ++i) {
        if (line.at(i) == u'/' || line.at(i) == u'\t') {
            cut = i;
            break;
        }
    }
    QStringView head = line.left(cut).trimmed();
    const qsizetype space = head.indexOf(u' ');
    if (space >= 0) {
        if (!looksLikeMorphField(head.mid(space + 1).trimmed()))
            return EntryResult::Multiword;
        head = head.left(space);
    }
    word = head.toString();
    return EntryResult::Word;
}

} // namespace

QString dicErrorText(DicError error)
{
    switch (error) {
    case DicError::None:
        return QStringLiteral("ok");
    case DicError::TooLarge:
        return QStringLiteral("file exceeds size limit");
    case DicError::InvalidUtf8:
        return QStringLiteral("file is not valid UTF-8");
    case DicError::MissingHeader:
        return QStringLiteral("missing Hunspell count header");
    case DicError::BadHeader:
        return QStringLiteral("first line is not a Hunspell count header");
    case DicError::CountMismatch:
        return QStringLiteral("record count does not match Hunspell count header");
    case DicError::InvalidLanguage:
        return QStringLiteral("invalid language");
    case DicError::TooManyRecords:
        return QStringLiteral("file exceeds record count limit");
    }
    return QStringLiteral("unknown error");
}

DicParseResult parseHunspellDic(const QByteArray &data, DetectorLanguage language)
{
    DicParseResult result;
    DicFilterReport &report = result.report;
    report.rawBytes = data.size();
    if (!isValidLanguage(language)) {
        result.error = DicError::InvalidLanguage;
        return result;
    }
    if (data.size() > kMaxDicBytes) {
        result.error = DicError::TooLarge;
        return result;
    }
    // До декодирования и split(): байт 0x0A в UTF-8 встречается только как
    // '\n'. Строки записей — все строки после заголовка, кроме пустого
    // хвоста после финального '\n'.
    const qint64 newlines = data.count('\n');
    const qint64 entryLines = data.endsWith('\n') ? newlines - 1 : newlines;
    if (entryLines > kMaxDicRecords) {
        result.error = DicError::TooManyRecords;
        return result;
    }

    QByteArray bytes = data;
    if (bytes.startsWith("\xEF\xBB\xBF")) {
        bytes.remove(0, 3);
        report.bomStripped = true;
    }

    // Stateless: незавершённая последовательность в конце — тоже ошибка.
    // ConvertInitialBom: повторный BOM не скрывается и будет отброшен фильтром.
    QStringDecoder decoder(QStringConverter::Utf8,
                           QStringConverter::Flag::Stateless
                               | QStringConverter::Flag::ConvertInitialBom);
    const QString text = decoder.decode(bytes);
    if (decoder.hasError()) {
        result.error = DicError::InvalidUtf8;
        return result;
    }

    const QList<QStringView> lines = QStringView(text).split(u'\n');
    if (lines.isEmpty() || lines.first().trimmed().isEmpty()) {
        result.error = DicError::MissingHeader;
        return result;
    }
    const QStringView header = lines.first().trimmed();
    if (!isAsciiDigits(header) || header.size() > 12) {
        result.error = DicError::BadHeader;
        return result;
    }
    report.headerCount = header.toLongLong();

    QSet<QString> seen;
    for (qsizetype i = 1; i < lines.size(); ++i) {
        QStringView line = lines.at(i);
        // Пустой хвост после финального '\n' не считается строкой записи.
        if (i == lines.size() - 1 && line.isEmpty())
            break;
        ++report.entryLines;
        if (line.endsWith(u'\r')) {
            line.chop(1);
            ++report.crlfLines;
        }
        if (line.trimmed().isEmpty()) {
            ++report.emptyLines;
            continue;
        }

        QString word;
        if (extractWord(line, word) == EntryResult::Multiword) {
            ++report.rejectedMultiword;
            continue;
        }
        word = word.toLower();
        if (!isLetterOnly(word, language)) {
            ++report.rejectedNotLetters;
            continue;
        }
        if (word.size() > kMaxWordChars) {
            ++report.rejectedLength;
            continue;
        }
        if (seen.contains(word)) {
            ++report.duplicates;
            continue;
        }
        seen.insert(word);
        result.words.append(word);
    }

    if (report.entryLines != report.headerCount) {
        result.error = DicError::CountMismatch;
        result.words.clear();
        return result;
    }

    result.words.sort();
    report.accepted = result.words.size();
    return result;
}

KeySequence splitGroup(const KeySequence &keys)
{
    return keys.left(kGroupPrefixKeys);
}

int splitBucket(const KeySequence &keys)
{
    QByteArray input(kSplitSeed);
    input.append('\0');
    input.append(splitGroup(keys));
    const QByteArray digest = QCryptographicHash::hash(input, QCryptographicHash::Sha256);
    // SHA-256 всегда даёт 32 байта; at() проверяет границы.
    quint64 value = 0;
    for (qsizetype i = 0; i < 8; ++i)
        value = (value << 8) | quint64(static_cast<unsigned char>(digest.at(i)));
    return int(value % 100U);
}

SplitPart splitForKeys(const KeySequence &keys)
{
    const int bucket = splitBucket(keys);
    if (bucket < kTrainBuckets)
        return SplitPart::Train;
    if (bucket < kTrainBuckets + kCalibrationBuckets)
        return SplitPart::Calibration;
    return SplitPart::Test;
}

const char *splitName(SplitPart part)
{
    switch (part) {
    case SplitPart::Train:
        return "train";
    case SplitPart::Calibration:
        return "calibration";
    case SplitPart::Test:
        return "test";
    }
    return "unknown";
}

LeakReport countSplitLeaks(const std::vector<SplitEntry> &en, const std::vector<SplitEntry> &ru)
{
    constexpr std::size_t kParts = 3;
    const std::array<const std::vector<SplitEntry> *, 2> byLanguage{{&en, &ru}};
    std::array<std::array<QSet<KeySequence>, kParts>, 2> keys;
    std::array<std::array<QSet<QString>, kParts>, 2> words;
    std::array<std::array<QSet<QString>, kParts>, 2> mapped;
    for (std::size_t li = 0; li < 2; ++li) {
        const LayoutDirection direction = li == 0 ? LayoutDirection::UsToRu : LayoutDirection::RuToUs;
        for (const SplitEntry &e : *byLanguage[li]) {
            const std::size_t pi = std::size_t(e.part);
            if (pi >= kParts)
                continue;
            keys[li][pi].insert(e.keys);
            words[li][pi].insert(e.word);
            mapped[li][pi].insert(convertLayout(e.word, direction));
        }
    }

    LeakReport report;
    for (std::size_t a = 0; a < kParts; ++a) {
        for (std::size_t b = 0; b < kParts; ++b) {
            if (a == b)
                continue;
            for (std::size_t la = 0; la < 2; ++la) {
                if (a < b) {
                    for (std::size_t lb = 0; lb < 2; ++lb)
                        report.canonicalKeyLeaks += QSet<KeySequence>(keys[la][a]).intersect(keys[lb][b]).size();
                    report.wordLeaks += QSet<QString>(words[la][a]).intersect(words[la][b]).size();
                }
                report.wrongLayoutMappingLeaks +=
                    QSet<QString>(mapped[la][a]).intersect(words[1 - la][b]).size();
            }
        }
    }
    return report;
}

} // namespace rasklad::experiment
