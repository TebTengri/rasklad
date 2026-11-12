// Модульные тесты чистого преобразования раскладки.
//
// Ожидаемые значения записаны вручную как ряды клавиатуры ANSI
// (слева направо) для XKB us(basic) и ru(default). Таблица из
// src/layoutconverter.cpp здесь не используется как oracle.
// Все примеры текста вымышленные.

#include "layoutconverter.h"

#include <QList>
#include <QSet>
#include <QString>
#include <QTest>

using rasklad::LayoutDirection;
using rasklad::convertLayout;

namespace {

struct KeyRow {
    const char *name;
    QString us;
    QString ru;
};

// Независимый oracle: ряды клавиатуры на двух уровнях.
QList<KeyRow> expectedRows()
{
    return {
        {"digits-base", QStringLiteral("`1234567890-="), QStringLiteral("ё1234567890-=")},
        {"digits-shift", QStringLiteral("~!@#$%^&*()_+"), QStringLiteral("Ё!\"№;%:?*()_+")},
        {"top-base", QStringLiteral("qwertyuiop[]\\"), QStringLiteral("йцукенгшщзхъ\\")},
        {"top-shift", QStringLiteral("QWERTYUIOP{}|"), QStringLiteral("ЙЦУКЕНГШЩЗХЪ/")},
        {"home-base", QStringLiteral("asdfghjkl;'"), QStringLiteral("фывапролджэ")},
        {"home-shift", QStringLiteral("ASDFGHJKL:\""), QStringLiteral("ФЫВАПРОЛДЖЭ")},
        {"bottom-base", QStringLiteral("zxcvbnm,./"), QStringLiteral("ячсмитьбю.")},
        {"bottom-shift", QStringLiteral("ZXCVBNM<>?"), QStringLiteral("ЯЧСМИТЬБЮ,")},
    };
}

QString withLoneSurrogate(char16_t surrogate)
{
    QString text = QStringLiteral("a");
    text.append(QChar(surrogate));
    text.append(QChar(u'b'));
    return text;
}

QString expectedWithLoneSurrogate(char16_t surrogate)
{
    QString text = QStringLiteral("ф");
    text.append(QChar(surrogate));
    text.append(QChar(u'и'));
    return text;
}

} // namespace

class LayoutConverterTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void oracleCoversAllPrintableAscii();
    void oracleRuSideIsUnique();
    void allUtf16CodeUnitsFollowIndependentOracle();

    void exampleWord();

    void wholeRows_data();
    void wholeRows();

    void singleKeys_data();
    void singleKeys();

    void letterCase_data();
    void letterCase();

    void yo_data();
    void yo();

    void sharedSymbolsStayInPlace();
    void punctuationFollowsPhysicalKeys();
    void directionIsExplicit();

    void unmappedUnicodeUnchanged_data();
    void unmappedUnicodeUnchanged();

    void asciiOutsideRuLayoutUnchanged();

    void combiningMarks_data();
    void combiningMarks();

    void loneSurrogatesUnchanged();

    void mixedStrings_data();
    void mixedStrings();

    void emptyAndWhitespace_data();
    void emptyAndWhitespace();

    void roundTrip();
    void unknownDirectionUnchanged();
};

// The independent keyboard rows define mapped characters. Exhaustively check
// every other UTF-16 unit too: compact lookup must preserve unknown values,
// controls and lone surrogates rather than accidentally indexing a gap.
void LayoutConverterTest::allUtf16CodeUnitsFollowIndependentOracle()
{
    const auto rows = expectedRows();
    for (const auto direction : {LayoutDirection::UsToRu, LayoutDirection::RuToUs}) {
        for (uint value = 0; value <= 0xffff; ++value) {
            const QChar input{ushort(value)};
            QChar expected = input;
            for (const KeyRow &row : rows) {
                const QString &from = direction == LayoutDirection::UsToRu ? row.us : row.ru;
                const QString &to = direction == LayoutDirection::UsToRu ? row.ru : row.us;
                const qsizetype index = from.indexOf(input);
                if (index >= 0) {
                    expected = to.at(index);
                    break;
                }
            }
            QCOMPARE(convertLayout(QString(1, input), direction), QString(1, expected));
            const QString marked = QString(1, input) + QChar(0x0301);
            QCOMPARE(convertLayout(marked, direction), marked);
        }
    }
}

void LayoutConverterTest::oracleCoversAllPrintableAscii()
{
    // US ANSI на уровнях base/Shift выдаёт ровно печатный ASCII без пробела.
    QString all;
    for (const KeyRow &row : expectedRows()) {
        QCOMPARE(row.us.size(), row.ru.size());
        all += row.us;
    }
    QCOMPARE(all.size(), qsizetype(94));
    QSet<char16_t> seen;
    for (const QChar c : all)
        seen.insert(c.unicode());
    QCOMPARE(seen.size(), qsizetype(94));
    for (char16_t c = 0x21; c <= 0x7E; ++c)
        QVERIFY2(seen.contains(c), "printable ASCII symbol missing from oracle");
}

void LayoutConverterTest::oracleRuSideIsUnique()
{
    QSet<char16_t> seen;
    qsizetype total = 0;
    for (const KeyRow &row : expectedRows()) {
        for (const QChar c : row.ru) {
            seen.insert(c.unicode());
            ++total;
        }
    }
    QCOMPARE(total, qsizetype(94));
    QCOMPARE(seen.size(), total);
}

void LayoutConverterTest::exampleWord()
{
    QCOMPARE(convertLayout(QStringLiteral("ghbdtn"), LayoutDirection::UsToRu),
             QStringLiteral("привет"));
    QCOMPARE(convertLayout(QStringLiteral("привет"), LayoutDirection::RuToUs),
             QStringLiteral("ghbdtn"));
}

void LayoutConverterTest::wholeRows_data()
{
    QTest::addColumn<QString>("us");
    QTest::addColumn<QString>("ru");
    for (const KeyRow &row : expectedRows())
        QTest::newRow(row.name) << row.us << row.ru;
}

void LayoutConverterTest::wholeRows()
{
    QFETCH(QString, us);
    QFETCH(QString, ru);
    QCOMPARE(convertLayout(us, LayoutDirection::UsToRu), ru);
    QCOMPARE(convertLayout(ru, LayoutDirection::RuToUs), us);
}

void LayoutConverterTest::singleKeys_data()
{
    QTest::addColumn<QString>("us");
    QTest::addColumn<QString>("ru");
    for (const KeyRow &row : expectedRows()) {
        for (qsizetype i = 0; i < row.us.size(); ++i) {
            QTest::addRow("%s-%lld", row.name, static_cast<long long>(i))
                << QString(row.us.at(i)) << QString(row.ru.at(i));
        }
    }
}

void LayoutConverterTest::singleKeys()
{
    QFETCH(QString, us);
    QFETCH(QString, ru);
    QCOMPARE(convertLayout(us, LayoutDirection::UsToRu), ru);
    QCOMPARE(convertLayout(ru, LayoutDirection::RuToUs), us);
}

void LayoutConverterTest::letterCase_data()
{
    QTest::addColumn<QString>("us");
    QTest::addColumn<QString>("ru");
    QTest::newRow("lower") << QStringLiteral("ghbdtn") << QStringLiteral("привет");
    QTest::newRow("capitalized") << QStringLiteral("Ghbdtn") << QStringLiteral("Привет");
    QTest::newRow("upper") << QStringLiteral("GHBDTN") << QStringLiteral("ПРИВЕТ");
    QTest::newRow("alternating") << QStringLiteral("gHbDtN") << QStringLiteral("пРиВеТ");
}

void LayoutConverterTest::letterCase()
{
    QFETCH(QString, us);
    QFETCH(QString, ru);
    QCOMPARE(convertLayout(us, LayoutDirection::UsToRu), ru);
    QCOMPARE(convertLayout(ru, LayoutDirection::RuToUs), us);
}

void LayoutConverterTest::yo_data()
{
    QTest::addColumn<QString>("us");
    QTest::addColumn<QString>("ru");
    QTest::newRow("lower-key") << QStringLiteral("`") << QStringLiteral("ё");
    QTest::newRow("upper-key") << QStringLiteral("~") << QStringLiteral("Ё");
    QTest::newRow("word-lower") << QStringLiteral("`krf") << QStringLiteral("ёлка");
    QTest::newRow("word-capitalized") << QStringLiteral("~krf") << QStringLiteral("Ёлка");
    QTest::newRow("word-upper") << QStringLiteral("~KRF") << QStringLiteral("ЁЛКА");
    QTest::newRow("inside-word") << QStringLiteral("ts`") <<QStringLiteral("еыё");
}

void LayoutConverterTest::yo()
{
    QFETCH(QString, us);
    QFETCH(QString, ru);
    QCOMPARE(convertLayout(us, LayoutDirection::UsToRu), ru);
    QCOMPARE(convertLayout(ru, LayoutDirection::RuToUs), us);
}

void LayoutConverterTest::sharedSymbolsStayInPlace()
{
    // Эти символы стоят на одной клавише и уровне в обеих раскладках.
    const QString shared = QStringLiteral("1234567890!%*()-_=+\\");
    QCOMPARE(convertLayout(shared, LayoutDirection::UsToRu), shared);
    QCOMPARE(convertLayout(shared, LayoutDirection::RuToUs), shared);
}

void LayoutConverterTest::punctuationFollowsPhysicalKeys()
{
    // Это перенос по клавишам, а не транслитерация и не пунктуационная
    // нормализация: запятая и точка переносятся по своим клавишам.
    QCOMPARE(convertLayout(QStringLiteral("ghbdtn, vbh."), LayoutDirection::UsToRu),
             QStringLiteral("приветб мирю"));
    QCOMPARE(convertLayout(QStringLiteral("привет, мир."), LayoutDirection::RuToUs),
             QStringLiteral("ghbdtn? vbh/"));
    QCOMPARE(convertLayout(QStringLiteral("2@3#4$6^7&"), LayoutDirection::UsToRu),
             QStringLiteral("2\"3№4;6:7?"));
    QCOMPARE(convertLayout(QStringLiteral("2\"3№4;6:7?"), LayoutDirection::RuToUs),
             QStringLiteral("2@3#4$6^7&"));
}

void LayoutConverterTest::directionIsExplicit()
{
    // Нет автоопределения: текст не из исходной раскладки не меняется.
    QCOMPARE(convertLayout(QStringLiteral("ghbdtn"), LayoutDirection::RuToUs),
             QStringLiteral("ghbdtn"));
    QCOMPARE(convertLayout(QStringLiteral("привет"), LayoutDirection::UsToRu),
             QStringLiteral("привет"));
}

void LayoutConverterTest::unmappedUnicodeUnchanged_data()
{
    QTest::addColumn<QString>("text");
    QTest::addColumn<int>("direction");

    const int usToRu = static_cast<int>(LayoutDirection::UsToRu);
    const int ruToUs = static_cast<int>(LayoutDirection::RuToUs);

    QTest::newRow("cyrillic-us-to-ru") << QStringLiteral("ПриВЕТ ёЁ") << usToRu;
    QTest::newRow("numero-us-to-ru") << QStringLiteral("№") << usToRu;
    QTest::newRow("other-cyrillic-ru-to-us") << QStringLiteral("іїєґўІЇЄҐЎ") << ruToUs;
    QTest::newRow("latin1-ru-to-us") << QStringLiteral("éüßÆ") << ruToUs;
    QTest::newRow("latin1-us-to-ru") << QStringLiteral("éüßÆ") << usToRu;
    QTest::newRow("greek-us-to-ru") << QStringLiteral("αβγΩ") << usToRu;
    QTest::newRow("cjk-ru-to-us") << QStringLiteral("漢字かな") << ruToUs;
    QTest::newRow("fullwidth-us-to-ru") << QStringLiteral("ＡＢＣ；，．") << usToRu;
    QTest::newRow("typographic-ru-to-us") << QStringLiteral("«»—…“”") << ruToUs;
    QTest::newRow("typographic-us-to-ru") << QStringLiteral("«»—…“”") << usToRu;
    QTest::newRow("emoji-us-to-ru") << QStringLiteral("🙂👍🏽🇰🇿") << usToRu;
    QTest::newRow("emoji-ru-to-us") << QStringLiteral("🙂👍🏽🇰🇿") << ruToUs;
    QTest::newRow("emoji-zwj-us-to-ru") << QStringLiteral("👩‍💻") << usToRu;
    QTest::newRow("non-bmp-letter-ru-to-us") << QStringLiteral("\U0001D400\U00010330")
                                             << ruToUs;
    const QString controls = QString(QChar(char16_t(0x01))) + QChar(char16_t(0x7F))
                             + QStringLiteral("‍﻿");
    QTest::newRow("controls-us-to-ru") << controls << usToRu;
    QTest::newRow("controls-ru-to-us") << controls << ruToUs;
}

void LayoutConverterTest::unmappedUnicodeUnchanged()
{
    QFETCH(QString, text);
    QFETCH(int, direction);
    QCOMPARE(convertLayout(text, static_cast<LayoutDirection>(direction)), text);
}

void LayoutConverterTest::asciiOutsideRuLayoutUnchanged()
{
    // Эти ASCII-символы отсутствуют в ru(default) на уровнях base/Shift.
    const QString text =
        QStringLiteral("abcxyzABCXYZ@#$^&[]{}'<>|`~");
    QCOMPARE(convertLayout(text, LayoutDirection::RuToUs), text);
}

void LayoutConverterTest::combiningMarks_data()
{
    QTest::addColumn<QString>("text");
    QTest::addColumn<int>("direction");
    QTest::addColumn<QString>("expected");

    const int usToRu = static_cast<int>(LayoutDirection::UsToRu);
    const int ruToUs = static_cast<int>(LayoutDirection::RuToUs);

    // Символ раскладки перед combining mark образует кластер, которого нет
    // на клавиатуре: кластер остаётся как есть.
    QTest::newRow("latin-acute") << QStringLiteral("é") << usToRu
                                 << QStringLiteral("é");
    QTest::newRow("latin-two-marks") << QStringLiteral("é̂") << usToRu
                                     << QStringLiteral("é̂");
    QTest::newRow("cyrillic-decomposed-yo") << QStringLiteral("ё") << ruToUs
                                            << QStringLiteral("ё");
    QTest::newRow("keycap-hash") << QStringLiteral("#️⃣") << usToRu
                                 << QStringLiteral("#️⃣");
    QTest::newRow("keycap-hash-ru-to-us") << QStringLiteral("#️⃣") << ruToUs
                                          << QStringLiteral("#️⃣");
    QTest::newRow("non-bmp-mark") << QStringLiteral("a\U0001D167") << usToRu
                                  << QStringLiteral("a\U0001D167");
    QTest::newRow("standalone-mark") << QStringLiteral("́") << usToRu
                                     << QStringLiteral("́");
    QTest::newRow("mark-before-key") << QStringLiteral("́e") << usToRu
                                     << QStringLiteral("́у");
    // Соседние символы без марок преобразуются.
    QTest::newRow("only-marked-key-kept") << QStringLiteral("ab́c") << usToRu
                                          << QStringLiteral("фb́с");
    QTest::newRow("only-marked-key-kept-ru") << QStringLiteral("фи́с") << ruToUs
                                             << QStringLiteral("aи́c");
}

void LayoutConverterTest::combiningMarks()
{
    QFETCH(QString, text);
    QFETCH(int, direction);
    QFETCH(QString, expected);
    QCOMPARE(convertLayout(text, static_cast<LayoutDirection>(direction)), expected);
}

void LayoutConverterTest::loneSurrogatesUnchanged()
{
    QCOMPARE(convertLayout(withLoneSurrogate(0xD800), LayoutDirection::UsToRu),
             expectedWithLoneSurrogate(0xD800));
    QCOMPARE(convertLayout(withLoneSurrogate(0xDC00), LayoutDirection::UsToRu),
             expectedWithLoneSurrogate(0xDC00));
}

void LayoutConverterTest::mixedStrings_data()
{
    QTest::addColumn<QString>("text");
    QTest::addColumn<int>("direction");
    QTest::addColumn<QString>("expected");

    const int usToRu = static_cast<int>(LayoutDirection::UsToRu);
    const int ruToUs = static_cast<int>(LayoutDirection::RuToUs);

    QTest::newRow("latin-and-cyrillic") << QStringLiteral("ghbdtn мир 🙂") << usToRu
                                        << QStringLiteral("привет мир 🙂");
    QTest::newRow("cyrillic-and-latin") << QStringLiteral("руддщ world 🙂") << ruToUs
                                        << QStringLiteral("hello world 🙂");
    QTest::newRow("multiline") << QStringLiteral("ghbdtn\n\tvbh\r\n") << usToRu
                               << QStringLiteral("привет\n\tмир\r\n");
    QTest::newRow("emoji-between") << QStringLiteral("rjn🐈rjn") << usToRu
                                   << QStringLiteral("кот🐈кот");
    QTest::newRow("nbsp-between") << QStringLiteral("rjn vbh") << usToRu
                                  << QStringLiteral("кот мир");
}

void LayoutConverterTest::mixedStrings()
{
    QFETCH(QString, text);
    QFETCH(int, direction);
    QFETCH(QString, expected);
    QCOMPARE(convertLayout(text, static_cast<LayoutDirection>(direction)), expected);
}

void LayoutConverterTest::emptyAndWhitespace_data()
{
    QTest::addColumn<QString>("text");
    QTest::newRow("null") << QString();
    QTest::newRow("empty") << QStringLiteral("");
    QTest::newRow("spaces") << QStringLiteral("   ");
    QTest::newRow("tabs-newlines") << QStringLiteral(" \t\n\r\v\f");
    QTest::newRow("unicode-spaces") << QStringLiteral("  　 ");
}

void LayoutConverterTest::emptyAndWhitespace()
{
    QFETCH(QString, text);
    const QString toRu = convertLayout(text, LayoutDirection::UsToRu);
    const QString toUs = convertLayout(text, LayoutDirection::RuToUs);
    QCOMPARE(toRu, text);
    QCOMPARE(toUs, text);
    QCOMPARE(toRu.isEmpty(), text.isEmpty());
}

void LayoutConverterTest::roundTrip()
{
    for (const KeyRow &row : expectedRows()) {
        const QString ru = convertLayout(row.us, LayoutDirection::UsToRu);
        QCOMPARE(convertLayout(ru, LayoutDirection::RuToUs), row.us);
        const QString us = convertLayout(row.ru, LayoutDirection::RuToUs);
        QCOMPARE(convertLayout(us, LayoutDirection::UsToRu), row.ru);
        // Повторный вызов даёт тот же результат.
        QCOMPARE(convertLayout(row.us, LayoutDirection::UsToRu), ru);
    }
}

void LayoutConverterTest::unknownDirectionUnchanged()
{
    const QString text = QStringLiteral("ghbdtn привет");
    QCOMPARE(convertLayout(text, static_cast<LayoutDirection>(42)), text);
}

QTEST_GUILESS_MAIN(LayoutConverterTest)

#include "rasklad_tests.moc"
