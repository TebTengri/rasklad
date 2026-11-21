#include "layoutconverter.h"

#include <QChar>

#include <array>
#include <cstddef>
#include <initializer_list>

namespace rasklad {
namespace {

// Один элемент — одна пара (физическая клавиша ANSI, уровень).
// Таблица составлена вручную по стандартным раскладкам XKB us(basic) и
// ru(default); код и словари сторонних проектов не использовались.
// Пробел не включён: на обеих раскладках он одинаков.
struct KeySymbol {
    char16_t us;
    char16_t ru;
};

constexpr std::array<KeySymbol, 94> kKeySymbols{{
    // Ряд цифр: TLDE, AE01..AE12. Base, затем Shift.
    {u'`', u'ё'}, {u'~', u'Ё'},
    {u'1', u'1'}, {u'!', u'!'},
    {u'2', u'2'}, {u'@', u'"'},
    {u'3', u'3'}, {u'#', u'№'},
    {u'4', u'4'}, {u'$', u';'},
    {u'5', u'5'}, {u'%', u'%'},
    {u'6', u'6'}, {u'^', u':'},
    {u'7', u'7'}, {u'&', u'?'},
    {u'8', u'8'}, {u'*', u'*'},
    {u'9', u'9'}, {u'(', u'('},
    {u'0', u'0'}, {u')', u')'},
    {u'-', u'-'}, {u'_', u'_'},
    {u'=', u'='}, {u'+', u'+'},

    // Верхний буквенный ряд: AD01..AD12, BKSL.
    {u'q', u'й'}, {u'Q', u'Й'},
    {u'w', u'ц'}, {u'W', u'Ц'},
    {u'e', u'у'}, {u'E', u'У'},
    {u'r', u'к'}, {u'R', u'К'},
    {u't', u'е'}, {u'T', u'Е'},
    {u'y', u'н'}, {u'Y', u'Н'},
    {u'u', u'г'}, {u'U', u'Г'},
    {u'i', u'ш'}, {u'I', u'Ш'},
    {u'o', u'щ'}, {u'O', u'Щ'},
    {u'p', u'з'}, {u'P', u'З'},
    {u'[', u'х'}, {u'{', u'Х'},
    {u']', u'ъ'}, {u'}', u'Ъ'},
    {u'\\', u'\\'}, {u'|', u'/'},

    // Средний ряд: AC01..AC11.
    {u'a', u'ф'}, {u'A', u'Ф'},
    {u's', u'ы'}, {u'S', u'Ы'},
    {u'd', u'в'}, {u'D', u'В'},
    {u'f', u'а'}, {u'F', u'А'},
    {u'g', u'п'}, {u'G', u'П'},
    {u'h', u'р'}, {u'H', u'Р'},
    {u'j', u'о'}, {u'J', u'О'},
    {u'k', u'л'}, {u'K', u'Л'},
    {u'l', u'д'}, {u'L', u'Д'},
    {u';', u'ж'}, {u':', u'Ж'},
    {u'\'', u'э'}, {u'"', u'Э'},

    // Нижний ряд: AB01..AB10.
    {u'z', u'я'}, {u'Z', u'Я'},
    {u'x', u'ч'}, {u'X', u'Ч'},
    {u'c', u'с'}, {u'C', u'С'},
    {u'v', u'м'}, {u'V', u'М'},
    {u'b', u'и'}, {u'B', u'И'},
    {u'n', u'т'}, {u'N', u'Т'},
    {u'm', u'ь'}, {u'M', u'Ь'},
    {u',', u'б'}, {u'<', u'Б'},
    {u'.', u'ю'}, {u'>', u'Ю'},
    {u'/', u'.'}, {u'?', u','},
}};

// Каждая сторона таблицы должна быть инъективной, иначе обратное
// преобразование стало бы неоднозначным.
constexpr bool hasUniqueSide(bool usSide)
{
    for (std::size_t i = 0; i < kKeySymbols.size(); ++i) {
        for (std::size_t j = i + 1; j < kKeySymbols.size(); ++j) {
            const char16_t a = usSide ? kKeySymbols[i].us : kKeySymbols[i].ru;
            const char16_t b = usSide ? kKeySymbols[j].us : kKeySymbols[j].ru;
            if (a == b)
                return false;
        }
    }
    return true;
}

// Суррогаты и пробелы в таблице недопустимы: поиск идёт по кодовым
// единицам UTF-16 и не должен затрагивать половины суррогатных пар.
constexpr bool hasOnlyPlainBmpSymbols()
{
    for (const KeySymbol &key : kKeySymbols) {
        for (const char16_t c : {key.us, key.ru}) {
            if (c <= u' ' || (c >= 0xD800 && c <= 0xDFFF))
                return false;
        }
    }
    return true;
}

static_assert(hasUniqueSide(true), "duplicate symbol on the us side");
static_assert(hasUniqueSide(false), "duplicate symbol on the ru side");
static_assert(hasOnlyPlainBmpSymbols(), "table must contain only printable BMP symbols");

constexpr char16_t kAsciiFirst = 0x21;
constexpr char16_t kAsciiLast = 0x7e;
constexpr char16_t kCyrillicFirst = 0x400;
constexpr char16_t kCyrillicLast = 0x45f;
constexpr char16_t kNumero = 0x2116;

struct DirectIndex {
    std::array<char16_t, kAsciiLast - kAsciiFirst + 1> us{};
    std::array<char16_t, kAsciiLast - kAsciiFirst + 1> ruAscii{};
    std::array<char16_t, kCyrillicLast - kCyrillicFirst + 1> ruCyrillic{};
    char16_t numero = 0;
};

constexpr DirectIndex makeIndex()
{
    DirectIndex out;
    for (const KeySymbol &key : kKeySymbols) {
        if (key.us >= kAsciiFirst && key.us <= kAsciiLast)
            out.us[key.us - kAsciiFirst] = key.ru;
        if (key.ru >= kAsciiFirst && key.ru <= kAsciiLast)
            out.ruAscii[key.ru - kAsciiFirst] = key.us;
        else if (key.ru >= kCyrillicFirst && key.ru <= kCyrillicLast)
            out.ruCyrillic[key.ru - kCyrillicFirst] = key.us;
        else if (key.ru == kNumero)
            out.numero = key.us;
    }
    return out;
}

// Индекс выводится из единственной таблицы. Сам поиск память не выделяет.
// Ноль обозначает отсутствие отображения: все символы таблицы печатные.
constexpr auto kIndex = makeIndex();

constexpr bool lookup(char16_t source, LayoutDirection direction, char16_t &target)
{
    char16_t mapped = 0;
    if (direction == LayoutDirection::UsToRu) {
        if (source >= kAsciiFirst && source <= kAsciiLast)
            mapped = kIndex.us[source - kAsciiFirst];
    } else if (direction == LayoutDirection::RuToUs) {
        if (source >= kAsciiFirst && source <= kAsciiLast)
            mapped = kIndex.ruAscii[source - kAsciiFirst];
        else if (source >= kCyrillicFirst && source <= kCyrillicLast)
            mapped = kIndex.ruCyrillic[source - kCyrillicFirst];
        else if (source == kNumero)
            mapped = kIndex.numero;
    }
    if (mapped == 0)
        return false;
    target = mapped;
    return true;
}

constexpr bool indexCoversTable()
{
    for (const KeySymbol &key : kKeySymbols) {
        char16_t forward = 0;
        char16_t reverse = 0;
        if (!lookup(key.us, LayoutDirection::UsToRu, forward) || forward != key.ru
            || !lookup(key.ru, LayoutDirection::RuToUs, reverse) || reverse != key.us)
            return false;
    }
    return true;
}
static_assert(indexCoversTable(), "lookup must cover every key pair in both directions");

// Начинается ли с позиции pos combining mark, включая марки вне BMP.
bool markStartsAt(const QString &text, qsizetype pos)
{
    if (pos >= text.size())
        return false;

    const QChar first = text.at(pos);
    char32_t codePoint = first.unicode();
    if (first.isHighSurrogate() && pos + 1 < text.size()) {
        const QChar second = text.at(pos + 1);
        if (second.isLowSurrogate())
            codePoint = QChar::surrogateToUcs4(first, second);
    }
    return QChar::isMark(codePoint);
}

} // namespace

QString convertLayout(const QString &text, LayoutDirection direction)
{
    if (direction != LayoutDirection::UsToRu && direction != LayoutDirection::RuToUs)
        return text;

    QString result = text;
    for (qsizetype i = 0; i < text.size(); ++i) {
        const char16_t source = text.at(i).unicode();
        char16_t target = 0;
        if (!lookup(source, direction, target))
            continue;
        if (markStartsAt(text, i + 1))
            continue;
        if (target != source)
            result[i] = QChar(target);
    }
    return result;
}

} // namespace rasklad
