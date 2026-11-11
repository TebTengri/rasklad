// Тесты политик и парсеров опыта 005 (SYS-237). Только синтетические
// fixtures в памяти и вымышленные строки; сгенерированные признаки, labels,
// корпус, словари, модель и Hunspell не читаются и не вызываются. Из
// репозитория читается лишь замороженный протокол 005.

#include "contextpolicy.h"
#include "detectormanifest.h"
#include "ngramdetector.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>

#include <array>
#include <functional>
#include <initializer_list>
#include <vector>

using namespace rasklad;
using namespace rasklad::context;

namespace {

QByteArray readRepo(const char *relative)
{
    QFile file(QStringLiteral(RASKLAD_SOURCE_ROOT) + QLatin1Char('/') + QString::fromLatin1(relative));
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return file.readAll();
}

QString hex(char c)
{
    return QString(64, QLatin1Char(c));
}

// ---- Вымышленные анкоры и цели ----

QString en3()
{
    return QStringLiteral("please");
}
QString en4()
{
    return QStringLiteral("write");
}
QString ru3()
{
    return QStringLiteral("прошу");
}
QString ru4()
{
    return QStringLiteral("записать");
}
// us-набор русского слова (6 клавиш) и ru-набор английского (5 клавиш).
QString usLong()
{
    return QStringLiteral("ghbdtn");
}
QString ruLong()
{
    return QStringLiteral("руддщ");
}
// Две клавиши: допустимо для context-методов, вне старого guard >= 4.
QString usShort()
{
    return QStringLiteral("jy");
}

// ---- TSV ----

QStringList goodFields()
{
    return {QStringLiteral("us"), usLong(), QString(), QString(), en3(), en4(), QStringLiteral("prose"),
            QStringLiteral("0"), QStringLiteral("1"), QStringLiteral("0"), QStringLiteral("C"),
            QStringLiteral("C")};
}

QStringList with(QStringList fields, int index, const QString &value)
{
    fields[index] = value;
    return fields;
}

QString join(const QStringList &fields)
{
    return fields.join(QLatin1Char('\t'));
}

QByteArray tsv(const QStringList &lines)
{
    QByteArray out;
    for (const QString &line : lines) {
        out.append(line.toUtf8());
        out.append('\n');
    }
    return out;
}

FeatureError lineError(const QStringList &fields)
{
    return parseFeatures(tsv({join(fields)}), 1).error;
}

FeatureRow makeRow(SourceLayout layout, const QString &target, const QString &left3, const QString &left4,
                   FieldKind field, bool observed, bool alternative, bool ambiguous)
{
    FeatureRow row;
    row.layout = layout;
    row.target = target;
    row.left = {{QString(), QString(), left3, left4}};
    row.field = field;
    row.fullObserved = observed;
    row.fullAlternative = alternative;
    row.sharedAmbiguous = ambiguous;
    row.tokenFullAction = expectedTokenFullAction(target, layout, observed, alternative);
    row.tokenModelAction = PolicyAction::Abstain;
    return row;
}

QByteArray codes(const ContextRow &row)
{
    QByteArray out;
    for (const PolicyAction action : row.actions)
        out.append(policyActionCode(action));
    return out;
}

QByteArray codes(const FeatureRow &row)
{
    return codes(evaluateRow(row));
}

// ---- Паспорт ----

QJsonObject goodManifest()
{
    QJsonObject features;
    features.insert(QStringLiteral("path"), QStringLiteral("build/experiment-005-data/features.tsv"));
    features.insert(QStringLiteral("sha256"), hex('a'));
    features.insert(QStringLiteral("bytes"), 100);
    features.insert(QStringLiteral("rows"), 2);
    QJsonObject labels;
    labels.insert(QStringLiteral("path"), QStringLiteral("build/experiment-005-data/labels.tsv"));
    labels.insert(QStringLiteral("sha256"), hex('c'));
    QJsonObject notice;
    notice.insert(QStringLiteral("notice"), QStringLiteral("data/notices/Fixture-LICENSE.txt"));
    notice.insert(QStringLiteral("notice_sha256"), hex('b'));
    QJsonObject o;
    o.insert(QStringLiteral("schema_version"), 1);
    o.insert(QStringLiteral("status"), QStringLiteral("approved-local-synthetic-evaluation"));
    o.insert(QStringLiteral("scope"), QStringLiteral("local-evaluation-only"));
    o.insert(QStringLiteral("release_approved"), false);
    o.insert(QStringLiteral("protocol_sha256"), QString::fromLatin1(kProtocolSha256));
    o.insert(QStringLiteral("description"), QStringLiteral("extra descriptive field"));
    o.insert(QStringLiteral("labels"), labels);
    o.insert(QStringLiteral("features"), features);
    o.insert(QStringLiteral("notices"), QJsonArray{notice});
    return o;
}

ManifestError manifestError(const QJsonObject &o)
{
    return parseManifest(QJsonDocument(o).toJson()).error;
}

QJsonObject withFeatures(const char *field, const QJsonValue &value)
{
    QJsonObject o = goodManifest();
    QJsonObject features = o.value(QStringLiteral("features")).toObject();
    if (value.isUndefined())
        features.remove(QString::fromLatin1(field));
    else
        features.insert(QString::fromLatin1(field), value);
    o.insert(QStringLiteral("features"), features);
    return o;
}

QJsonObject noticeEntry(const QString &path, const QString &sha)
{
    QJsonObject n;
    n.insert(QStringLiteral("notice"), path);
    n.insert(QStringLiteral("notice_sha256"), sha);
    return n;
}

QJsonObject withNotices(const QJsonArray &notices)
{
    QJsonObject o = goodManifest();
    o.insert(QStringLiteral("notices"), notices);
    return o;
}

QStringList cli(std::initializer_list<const char *> items)
{
    QStringList out{QStringLiteral("context_decisions")};
    for (const char *item : items)
        out.append(QString::fromUtf8(item));
    return out;
}

QByteArray mutatedProtocol(const std::function<void(QJsonObject &)> &mutate)
{
    QJsonObject o = QJsonDocument::fromJson(readRepo(kProtocolPath)).object();
    mutate(o);
    return QJsonDocument(o).toJson();
}

} // namespace

class ContextTests : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    // ---- Протокол ----

    void frozenProtocolExactHash()
    {
        const QByteArray raw = readRepo(kProtocolPath);
        QVERIFY(!raw.isEmpty());
        QVERIFY(qint64(raw.size()) <= kMaxProtocolBytes);
        QCOMPARE(experiment::sha256Hex(raw), QString::fromLatin1(kProtocolSha256));
        QCOMPARE(validateProtocol005(raw), ProtocolError::None);
        QCOMPARE(validateProtocolFields(raw), ProtocolError::None);

        // Любой изменённый байт, даже пробельный, — отказ по хешу.
        QByteArray changed = raw;
        changed[changed.size() - 2] = changed.at(changed.size() - 2) == ' ' ? '\t' : ' ';
        QCOMPARE(validateProtocol005(changed), ProtocolError::HashMismatch);
        QCOMPARE(validateProtocol005(raw + '\n'), ProtocolError::HashMismatch);
        QCOMPARE(validateProtocol005(raw.left(raw.size() - 1)), ProtocolError::HashMismatch);
        QCOMPARE(validateProtocol005(QByteArray()), ProtocolError::HashMismatch);
        QCOMPARE(validateProtocol005(QByteArray(kMaxProtocolBytes + 1, ' ')), ProtocolError::TooLarge);
        QCOMPARE(validateProtocol005(raw + QByteArray(kMaxProtocolBytes, ' ')), ProtocolError::TooLarge);

        // Семантически эквивалентная переразметка JSON — тоже другой файл.
        const QByteArray reformatted = QJsonDocument::fromJson(raw).toJson(QJsonDocument::Compact);
        QCOMPARE(validateProtocolFields(reformatted), ProtocolError::None);
        QCOMPARE(validateProtocol005(reformatted), ProtocolError::HashMismatch);
    }

    void protocolFieldsMatchCode()
    {
        const QJsonObject protocol = QJsonDocument::fromJson(readRepo(kProtocolPath)).object();
        const QJsonArray policies = protocol.value(QStringLiteral("policies")).toArray();
        QCOMPARE(policies.size(), qsizetype(kContextMethodCount));
        for (int i = 0; i < kContextMethodCount; ++i)
            QCOMPARE(policies.at(i).toString(),
                     QString::fromLatin1(contextMethodName(kContextMethods[std::size_t(i)])));

        // Хеш проверяется первым: изменённая политика не доходит до полей.
        const QByteArray swapped = mutatedProtocol([](QJsonObject &o) {
            QJsonArray p = o.value(QStringLiteral("policies")).toArray();
            const QJsonValue first = p.at(3);
            p[3] = p.at(4);
            p[4] = first;
            o.insert(QStringLiteral("policies"), p);
        });
        QCOMPARE(validateProtocol005(swapped), ProtocolError::HashMismatch);
        QCOMPARE(validateProtocolFields(swapped), ProtocolError::FieldMismatch);

        QCOMPARE(validateProtocolFields(mutatedProtocol([](QJsonObject &o) {
                     QJsonArray p = o.value(QStringLiteral("policies")).toArray();
                     p.append(QStringLiteral("extraPolicy"));
                     o.insert(QStringLiteral("policies"), p);
                 })),
                 ProtocolError::FieldMismatch);
        QCOMPARE(validateProtocolFields(mutatedProtocol([](QJsonObject &o) {
                     QJsonObject limits = o.value(QStringLiteral("limits")).toObject();
                     limits.insert(QStringLiteral("field_utf16"), 31);
                     o.insert(QStringLiteral("limits"), limits);
                 })),
                 ProtocolError::FieldMismatch);
        QCOMPARE(validateProtocolFields(mutatedProtocol([](QJsonObject &o) {
                     QJsonObject limits = o.value(QStringLiteral("limits")).toObject();
                     limits.insert(QStringLiteral("extra"), 1);
                     o.insert(QStringLiteral("limits"), limits);
                 })),
                 ProtocolError::FieldMismatch);
        QCOMPARE(validateProtocolFields(mutatedProtocol([](QJsonObject &o) {
                     QJsonObject obs = o.value(QStringLiteral("observables")).toObject();
                     QJsonArray f = obs.value(QStringLiteral("feature_fields")).toArray();
                     f.removeAt(2);
                     obs.insert(QStringLiteral("feature_fields"), f);
                     o.insert(QStringLiteral("observables"), obs);
                 })),
                 ProtocolError::FieldMismatch);
        QCOMPARE(validateProtocolFields(mutatedProtocol([](QJsonObject &o) {
                     QJsonObject obs = o.value(QStringLiteral("observables")).toObject();
                     obs.insert(QStringLiteral("field_values"), QJsonArray{QStringLiteral("prose")});
                     o.insert(QStringLiteral("observables"), obs);
                 })),
                 ProtocolError::FieldMismatch);
        QCOMPARE(validateProtocolFields(QByteArray("[]")), ProtocolError::InvalidJson);
    }

    // ---- Аргументы ----

    void arguments()
    {
        const ArgumentsResult good = parseArguments(cli({"--protocol", "p.json", "--manifest", "m.json"}));
        QCOMPARE(good.error, ArgumentsError::None);
        QCOMPARE(good.arguments.protocol, QStringLiteral("p.json"));
        QCOMPARE(good.arguments.manifest, QStringLiteral("m.json"));
        QCOMPARE(parseArguments(cli({"--manifest", "m.json", "--protocol", "p.json"})).error, ArgumentsError::None);

        QCOMPARE(parseArguments(cli({})).error, ArgumentsError::Missing);
        QCOMPARE(parseArguments(cli({"--protocol", "p.json"})).error, ArgumentsError::Missing);
        QCOMPARE(parseArguments(cli({"--manifest", "m.json"})).error, ArgumentsError::Missing);
        QCOMPARE(parseArguments(cli({"--protocol", "p", "--protocol", "q", "--manifest", "m"})).error,
                 ArgumentsError::Duplicate);
        QCOMPARE(parseArguments(cli({"--protocol", "p", "--manifest", "m", "--labels", "l"})).error,
                 ArgumentsError::Unknown);
        QCOMPARE(parseArguments(cli({"positional"})).error, ArgumentsError::Unknown);
        QCOMPARE(parseArguments(cli({"--protocol=p", "--manifest", "m"})).error, ArgumentsError::Unknown);
        QCOMPARE(parseArguments(cli({"--manifest", "m", "--protocol"})).error, ArgumentsError::MissingValue);
        QCOMPARE(parseArguments(cli({"--protocol", "--manifest", "m"})).error, ArgumentsError::BadValue);
        QCOMPARE(parseArguments(cli({"--protocol", "", "--manifest", "m"})).error, ArgumentsError::BadValue);
        QVERIFY(parseArguments(cli({"--protocol", "p", "--manifest", "m", "x"})).arguments.protocol.isEmpty());
    }

    // ---- Паспорт ----

    void manifest()
    {
        const ManifestResult good = parseManifest(QJsonDocument(goodManifest()).toJson());
        QCOMPARE(good.error, ManifestError::None);
        QCOMPARE(good.manifest.featuresPath, QString::fromLatin1(kFeaturesPath));
        QCOMPARE(good.manifest.featuresSha256, hex('a'));
        QCOMPARE(good.manifest.featuresBytes, qint64(100));
        QCOMPARE(good.manifest.featuresRows, qint64(2));
        QCOMPARE(good.manifest.notices.size(), std::size_t(1));

        QCOMPARE(parseManifest(QByteArray("[]")).error, ManifestError::InvalidJson);
        QCOMPARE(parseManifest(QByteArray("{")).error, ManifestError::InvalidJson);
        QCOMPARE(parseManifest(QByteArray(kMaxManifestBytes + 1, ' ')).error, ManifestError::TooLarge);

        QJsonObject o = goodManifest();
        o.insert(QStringLiteral("schema_version"), 2);
        QCOMPARE(manifestError(o), ManifestError::SchemaVersion);
        o.insert(QStringLiteral("schema_version"), 1.5);
        QCOMPARE(manifestError(o), ManifestError::SchemaVersion);
        o = goodManifest();
        o.insert(QStringLiteral("status"), QStringLiteral("approved-local-evaluation"));
        QCOMPARE(manifestError(o), ManifestError::StatusNotApproved);
        o = goodManifest();
        o.insert(QStringLiteral("scope"), QStringLiteral("release"));
        QCOMPARE(manifestError(o), ManifestError::ScopeMismatch);
        o = goodManifest();
        o.insert(QStringLiteral("release_approved"), true);
        QCOMPARE(manifestError(o), ManifestError::ReleaseApproved);
        o.insert(QStringLiteral("release_approved"), QStringLiteral("false"));
        QCOMPARE(manifestError(o), ManifestError::ReleaseApproved);
        o.remove(QStringLiteral("release_approved"));
        QCOMPARE(manifestError(o), ManifestError::ReleaseApproved);
        o = goodManifest();
        o.insert(QStringLiteral("protocol_sha256"), QString::fromLatin1(kProtocolSha256).toUpper());
        QCOMPARE(manifestError(o), ManifestError::ProtocolMismatch);
        o.insert(QStringLiteral("protocol_sha256"), hex('0'));
        QCOMPARE(manifestError(o), ManifestError::ProtocolMismatch);
        o.remove(QStringLiteral("protocol_sha256"));
        QCOMPARE(manifestError(o), ManifestError::ProtocolMismatch);

        o = goodManifest();
        o.insert(QStringLiteral("features"), QStringLiteral("build/experiment-005-data/features.tsv"));
        QCOMPARE(manifestError(o), ManifestError::BadFeatures);
        for (const char *path : {"build/experiment-005-data/../experiment-005-data/features.tsv",
                                 "build/experiment-005-data/labels.tsv", "/tmp/features.tsv",
                                 "build/experiment-005-data/features.tsv/", "data/features.tsv"})
            QCOMPARE(manifestError(withFeatures("path", QString::fromLatin1(path))), ManifestError::BadFeatures);
        QCOMPARE(manifestError(withFeatures("sha256", hex('A'))), ManifestError::BadFeatures);
        QCOMPARE(manifestError(withFeatures("sha256", QString(63, QLatin1Char('a')))), ManifestError::BadFeatures);
        QCOMPARE(manifestError(withFeatures("sha256", QJsonValue())), ManifestError::BadFeatures);
        QCOMPARE(manifestError(withFeatures("bytes", 0)), ManifestError::BadFeatures);
        QCOMPARE(manifestError(withFeatures("bytes", double(kMaxFeatureBytes + 1))), ManifestError::BadFeatures);
        QCOMPARE(manifestError(withFeatures("bytes", 10.5)), ManifestError::BadFeatures);
        QCOMPARE(manifestError(withFeatures("bytes", QStringLiteral("100"))), ManifestError::BadFeatures);
        QCOMPARE(manifestError(withFeatures("bytes", double(kMaxFeatureBytes))), ManifestError::None);
        QCOMPARE(manifestError(withFeatures("rows", 0)), ManifestError::BadFeatures);
        QCOMPARE(manifestError(withFeatures("rows", double(kMaxFeatureRows + 1))), ManifestError::BadFeatures);
        QCOMPARE(manifestError(withFeatures("rows", double(kMaxFeatureRows))), ManifestError::None);

        QCOMPARE(manifestError(withNotices(QJsonArray())), ManifestError::BadNotices);
        QJsonArray many;
        for (int i = 0; i <= kMaxManifestNotices; ++i)
            many.append(noticeEntry(QStringLiteral("data/notices/n%1.txt").arg(i), hex('b')));
        QCOMPARE(manifestError(withNotices(many)), ManifestError::BadNotices);
        many.removeLast();
        QCOMPARE(manifestError(withNotices(many)), ManifestError::None);
        const QJsonObject n = noticeEntry(QStringLiteral("data/notices/a.txt"), hex('b'));
        QCOMPARE(manifestError(withNotices({n, n})), ManifestError::BadNotices);
        QCOMPARE(manifestError(withNotices({noticeEntry(QStringLiteral("data/other/a.txt"), hex('b'))})),
                 ManifestError::BadNotices);
        QCOMPARE(manifestError(withNotices({noticeEntry(QStringLiteral("data/notices/../x.txt"), hex('b'))})),
                 ManifestError::BadNotices);
        QCOMPARE(manifestError(withNotices({noticeEntry(QStringLiteral("data/notices/a.txt"), hex('B'))})),
                 ManifestError::BadNotices);
        QCOMPARE(manifestError(withNotices({QJsonValue(QStringLiteral("data/notices/a.txt"))})),
                 ManifestError::BadNotices);
        o = goodManifest();
        o.remove(QStringLiteral("notices"));
        QCOMPARE(manifestError(o), ManifestError::BadNotices);

        // Отказ не оставляет частично заполненный паспорт.
        const ManifestResult bad = parseManifest(QJsonDocument(withFeatures("rows", 0)).toJson());
        QVERIFY(bad.manifest.featuresPath.isEmpty());
        QVERIFY(bad.manifest.notices.empty());
    }

    // ---- TSV: сырые пределы до декодирования ----

    void featureRawBounds()
    {
        const QString good = join(goodFields());
        QCOMPARE(parseFeatures(tsv({good}), 1).error, FeatureError::None);

        QByteArray huge(kMaxFeatureBytes + 1, '\r');
        huge.back() = '\n';
        QCOMPARE(parseFeatures(huge, 1).error, FeatureError::TooLarge);
        QCOMPARE(parseFeatures(QByteArray(), 0).error, FeatureError::Empty);
        QCOMPARE(parseFeatures(good.toUtf8(), 1).error, FeatureError::MissingFinalNewline);
        // Число строк проверяется до пустых строк и полей.
        QCOMPARE(parseFeatures(QByteArray(kMaxFeatureRows + 1, '\n'), kMaxFeatureRows + 1).error,
                 FeatureError::TooManyRows);
        QCOMPARE(parseFeatures(tsv({good}), 2).error, FeatureError::RowCountMismatch);
        QCOMPARE(parseFeatures(tsv({good, good}), 1).error, FeatureError::RowCountMismatch);
        QCOMPARE(parseFeatures(tsv({good + QLatin1Char('\r')}), 1).error, FeatureError::CarriageReturn);
        // CR проверяется раньше пустых строк.
        QCOMPARE(parseFeatures(QByteArray("\r\n\n"), 2).error, FeatureError::CarriageReturn);
        QCOMPARE(parseFeatures(tsv({good, QString(), good}), 3).error, FeatureError::EmptyLine);
        QCOMPARE(parseFeatures(tsv({QString(), good}), 2).error, FeatureError::EmptyLine);

        // Длина строки — по сырым байтам до UTF-8: 4096 допустимо, 4097 нет.
        QByteArray atLimit(kMaxFeatureLineBytes, '\xff');
        atLimit.append('\n');
        QCOMPARE(parseFeatures(atLimit, 1).error, FeatureError::InvalidUtf8);
        QByteArray overLimit(kMaxFeatureLineBytes + 1, '\xff');
        overLimit.append('\n');
        QCOMPARE(parseFeatures(overLimit, 1).error, FeatureError::LineTooLong);

        for (const char *bad : {"\xff", "\xc0\xaf", "\xed\xa0\x80", "\xe2\x82", "\xf4\x90\x80\x80"}) {
            QByteArray data = tsv({good});
            data.insert(3, bad);
            QCOMPARE(parseFeatures(data, 1).error, FeatureError::InvalidUtf8);
        }

        // Ошибка во второй строке — отказ всего файла, без частичного списка.
        const FeatureResult partial = parseFeatures(tsv({good, join(with(goodFields(), 0, QStringLiteral("en")))}), 2);
        QCOMPARE(partial.error, FeatureError::BadLayout);
        QVERIFY(partial.rows.empty());
    }

    // ---- TSV: поля ----

    void featureFields()
    {
        const FeatureResult parsed = parseFeatures(
            tsv({join(goodFields()),
                 join({QStringLiteral("ru"), ruLong(), QStringLiteral("x"), QStringLiteral("y"), ru3(), ru4(),
                       QStringLiteral("protected"), QStringLiteral("1"), QStringLiteral("1"), QStringLiteral("1"),
                       QStringLiteral("K"), QStringLiteral("K")}),
                 join({QStringLiteral("us"), usShort(), QString(), QString(), QString(), QString(),
                       QStringLiteral("unknown"), QStringLiteral("0"), QStringLiteral("0"), QStringLiteral("1"),
                       QStringLiteral("A"), QStringLiteral("A")})}),
            3);
        QCOMPARE(parsed.error, FeatureError::None);
        QCOMPARE(parsed.rows.size(), std::size_t(3));
        const FeatureRow &a = parsed.rows[0];
        QCOMPARE(a.layout, SourceLayout::Us);
        QCOMPARE(a.target, usLong());
        QVERIFY(a.left[0].isEmpty() && a.left[1].isEmpty());
        QCOMPARE(a.left[2], en3());
        QCOMPARE(a.left[3], en4());
        QCOMPARE(a.field, FieldKind::Prose);
        QVERIFY(!a.fullObserved && a.fullAlternative && !a.sharedAmbiguous);
        QCOMPARE(a.tokenModelAction, PolicyAction::Correct);
        QCOMPARE(a.tokenFullAction, PolicyAction::Correct);
        const FeatureRow &b = parsed.rows[1];
        QCOMPARE(b.layout, SourceLayout::Ru);
        QCOMPARE(b.left[3], ru4());
        QCOMPARE(b.field, FieldKind::Protected);
        QVERIFY(b.fullObserved && b.fullAlternative && b.sharedAmbiguous);
        QCOMPARE(parsed.rows[2].field, FieldKind::Unknown);

        QCOMPARE(lineError(goodFields().mid(0, 11)), FeatureError::BadColumns);
        QCOMPARE(lineError(goodFields() << QString()), FeatureError::BadColumns);
        QCOMPARE(lineError(goodFields() << QStringLiteral("x")), FeatureError::BadColumns);
        for (const char *layout : {"US", "en", "ru ", "", "usx"})
            QCOMPARE(lineError(with(goodFields(), 0, QString::fromLatin1(layout))), FeatureError::BadLayout);
        QCOMPARE(lineError(with(goodFields(), 1, QString())), FeatureError::EmptyTarget);
        for (const char *field : {"Prose", "", "code", "protected "})
            QCOMPARE(lineError(with(goodFields(), 6, QString::fromLatin1(field))), FeatureError::BadField);
        for (int index : {7, 8, 9}) {
            for (const char *flag : {"2", "00", "", "true", "-1"})
                QCOMPARE(lineError(with(goodFields(), index, QString::fromLatin1(flag))), FeatureError::BadFlag);
        }
        for (int index : {10, 11}) {
            for (const char *action : {"S", "k", "", "KC", "Keep"})
                QCOMPARE(lineError(with(goodFields(), index, QString::fromLatin1(action))), FeatureError::BadAction);
        }
    }

    void featureStringLimits()
    {
        // 32 UTF-16 допустимо и для кириллицы (64 байта), 33 — нет.
        QCOMPARE(lineError(with(goodFields(), 4, QString(32, QChar(u'я')))), FeatureError::None);
        QCOMPARE(lineError(with(goodFields(), 4, QString(33, QChar(u'я')))), FeatureError::FieldTooLong);
        QCOMPARE(lineError(with(goodFields(), 1, QString(33, QLatin1Char('f')))), FeatureError::FieldTooLong);
        // Символ вне BMP — две кодовые единицы UTF-16.
        QString emoji;
        for (int i = 0; i < 16; ++i)
            emoji += QStringLiteral("\U0001F600");
        QCOMPARE(lineError(with(goodFields(), 2, emoji)), FeatureError::None);
        QCOMPARE(lineError(with(goodFields(), 2, emoji + QStringLiteral("\U0001F600"))), FeatureError::FieldTooLong);

        // Cc и пробельные символы Unicode запрещены во всех строковых полях,
        // включая игнорируемые политикой left1/left2.
        const std::array<char16_t, 10> forbidden{{0x0007, 0x000B, 0x001B, 0x007F, 0x0085, 0x00A0, 0x2003, 0x2028,
                                                 0x3000, 0x0020}};
        for (const char16_t ch : forbidden) {
            for (int index : {1, 2, 3, 4, 5})
                QCOMPARE(lineError(with(goodFields(), index, QStringLiteral("ab") + QChar(ch) + QStringLiteral("cd"))),
                         FeatureError::BadCharacter);
        }
        // Неразрывный пробел внутри анкора не превращает его в два слова
        // (явная форма и та же строка с литеральным U+00A0 ниже).
        QCOMPARE(lineError(with(goodFields(), 5, QStringLiteral("wr") + QChar(char16_t(0x00A0)) + QStringLiteral("ite"))),
                 FeatureError::BadCharacter);
        QCOMPARE(lineError(with(goodFields(), 5, QStringLiteral("wr ite"))), FeatureError::BadCharacter);
    }

    void featureConsistency()
    {
        // Оба full-membership без общей неоднозначности — противоречие.
        QStringList both = with(with(goodFields(), 7, QStringLiteral("1")), 11, QStringLiteral("K"));
        QCOMPARE(lineError(both), FeatureError::InconsistentAmbiguity);
        QCOMPARE(lineError(with(both, 9, QStringLiteral("1"))), FeatureError::None);

        // tokenFullAction для длинной supported цели: obs ? K : alt ? C : A.
        QCOMPARE(lineError(with(goodFields(), 11, QStringLiteral("K"))), FeatureError::InconsistentFullAction);
        QCOMPARE(lineError(with(goodFields(), 11, QStringLiteral("A"))), FeatureError::InconsistentFullAction);
        const QStringList neither = with(goodFields(), 8, QStringLiteral("0"));
        QCOMPARE(lineError(with(neither, 11, QStringLiteral("C"))), FeatureError::InconsistentFullAction);
        QCOMPARE(lineError(with(neither, 11, QStringLiteral("A"))), FeatureError::None);
        const QStringList observedOnly = with(neither, 7, QStringLiteral("1"));
        QCOMPARE(lineError(with(observedOnly, 11, QStringLiteral("C"))), FeatureError::InconsistentFullAction);
        QCOMPARE(lineError(with(observedOnly, 11, QStringLiteral("K"))), FeatureError::None);
        // Модель для длинной цели здесь не проверяется (это делает подготовка).
        for (const char *model : {"K", "C", "A"})
            QCOMPARE(lineError(with(goodFields(), 10, QString::fromLatin1(model))), FeatureError::None);

        // Короткая (2..3) и unsupported цель: оба token-действия обязаны быть A,
        // хотя membership для неё может быть передан.
        for (const QString &target : {usShort(), QStringLiteral("jyf"), QStringLiteral("j"),
                                      QStringLiteral("a1b2c3"), QStringLiteral("abвгд")}) {
            const QStringList base = with(goodFields(), 1, target);
            QCOMPARE(lineError(base), FeatureError::InconsistentFullAction);
            const QStringList fullAbstain = with(base, 11, QStringLiteral("A"));
            QCOMPARE(lineError(fullAbstain), FeatureError::InconsistentModelAction);
            QCOMPARE(lineError(with(fullAbstain, 10, QStringLiteral("K"))), FeatureError::InconsistentModelAction);
            QCOMPARE(lineError(with(fullAbstain, 10, QStringLiteral("A"))), FeatureError::None);
        }
        // Длина цели считается в канонических клавишах: 4 — уже под guard.
        QCOMPARE(lineError(with(goodFields(), 1, QStringLiteral("ghbd"))), FeatureError::None);
    }

    // ---- Цели и guard ----

    void targetLengthsAndGuard()
    {
        QVERIFY(!isContextTarget(QStringLiteral("j"), SourceLayout::Us));
        QVERIFY(isContextTarget(usShort(), SourceLayout::Us));
        QVERIFY(isContextTarget(QStringLiteral("jyf"), SourceLayout::Us));
        QVERIFY(!isTokenGuardTarget(usShort(), SourceLayout::Us));
        QVERIFY(!isTokenGuardTarget(QStringLiteral("jyf"), SourceLayout::Us));
        QVERIFY(isTokenGuardTarget(QStringLiteral("ghbd"), SourceLayout::Us));
        QVERIFY(isContextTarget(QString(32, QLatin1Char('f')), SourceLayout::Us));
        QVERIFY(!isContextTarget(QString(33, QLatin1Char('f')), SourceLayout::Us));
        QVERIFY(!isContextTarget(QStringLiteral("a1"), SourceLayout::Us));
        QVERIFY(!isContextTarget(QStringLiteral("aб"), SourceLayout::Us));
        QVERIFY(!isContextTarget(QStringLiteral("ab"), SourceLayout::Ru));
        QVERIFY(isContextTarget(QStringLiteral("да"), SourceLayout::Ru));
        // Регистр и us-пунктуация клавиш букв — supported.
        QVERIFY(isContextTarget(QStringLiteral("Ghbdtn"), SourceLayout::Us));
        QVERIFY(isContextTarget(QStringLiteral("k.,"), SourceLayout::Us));

        QCOMPARE(expectedTokenFullAction(usLong(), SourceLayout::Us, true, true), PolicyAction::Keep);
        QCOMPARE(expectedTokenFullAction(usLong(), SourceLayout::Us, false, true), PolicyAction::Correct);
        QCOMPARE(expectedTokenFullAction(usLong(), SourceLayout::Us, false, false), PolicyAction::Abstain);
        QCOMPARE(expectedTokenFullAction(usShort(), SourceLayout::Us, false, true), PolicyAction::Abstain);
        QCOMPARE(expectedTokenFullAction(usShort(), SourceLayout::Us, true, false), PolicyAction::Abstain);
    }

    void shortTargetsOnlyForContext()
    {
        // Короткая цель, противоположный контекст: новые методы решают,
        // старые token-методы сохраняют guard >= 4 и воздерживаются.
        const FeatureRow shortRow =
            makeRow(SourceLayout::Us, usShort(), ru3(), ru4(), FieldKind::Prose, false, true, false);
        QCOMPARE(shortRow.tokenFullAction, PolicyAction::Abstain);
        QCOMPARE(codes(shortRow), QByteArray("KAACCS"));
        FeatureRow shortAmbiguous = shortRow;
        shortAmbiguous.sharedAmbiguous = true;
        QCOMPARE(codes(shortAmbiguous), QByteArray("KAACAS"));

        // Одна клавиша — недопустимая цель и для context-методов.
        QCOMPARE(codes(makeRow(SourceLayout::Us, QStringLiteral("j"), ru3(), ru4(), FieldKind::Prose, false, true,
                               false)),
                 QByteArray("KAAAAA"));
        // Unsupported цель.
        QCOMPARE(codes(makeRow(SourceLayout::Us, QStringLiteral("a1b2c3"), ru3(), ru4(), FieldKind::Prose, false,
                               true, false)),
                 QByteArray("KAAAAA"));

        // Длинная цель: тот же случай даёт C и старому tokenHunspell.
        FeatureRow longRow = makeRow(SourceLayout::Us, usLong(), ru3(), ru4(), FieldKind::Prose, false, true, false);
        longRow.tokenModelAction = PolicyAction::Correct;
        QCOMPARE(codes(longRow), QByteArray("KCCCCS"));
    }

    // ---- Анкоры ----

    void anchorScripts()
    {
        for (const QString &w : {en3(), QStringLiteral("PLEASE"), QStringLiteral("Write"), QStringLiteral("abcd"),
                                 QString(32, QLatin1Char('a'))})
            QCOMPARE(anchorScript(w), ContextVote::En);
        for (const QString &w : {ru3(), QStringLiteral("ПРОШУ"), QStringLiteral("Записать"), QStringLiteral("ёлка"),
                                 QStringLiteral("ЁЛКА"), QStringLiteral("объём"), QString(32, QChar(u'я'))})
            QCOMPARE(anchorScript(w), ContextVote::Ru);
        for (const QString &w : {
                 QString(), QStringLiteral("abc"), QStringLiteral("ещё"), QString(33, QLatin1Char('a')),
                 // Смешанная письменность: кириллическая «е» среди латиницы.
                 QStringLiteral("pl") + QChar(char16_t(0x0435)) + QStringLiteral("ase"),
                 QStringLiteral("при") + QLatin1Char('v') + QStringLiteral("ет"),
                 // Буквы вне ASCII EN и RU33.
                 QStringLiteral("naïve"), QStringLiteral("straße"), QStringLiteral("αβγδ"),
                 QStringLiteral("їжак"), QStringLiteral("ҚАЗАҚ"),
                 QStringLiteral("ｐｌｅａｓｅ"),
                 // Цифры, апостроф, дефис, us-пунктуация клавиш букв.
                 QStringLiteral("test1"), QStringLiteral("don't"), QStringLiteral("e-mail"), QStringLiteral("k.,jd"),
                 QStringLiteral("abćd")})
            QCOMPARE(anchorScript(w), ContextVote::None);
        // Явные формы: combining acute U+0301 и Ё/ё по кодам.
        QCOMPARE(anchorScript(QStringLiteral("abc") + QChar(char16_t(0x0301)) + QStringLiteral("d")),
                 ContextVote::None);
        QCOMPARE(anchorScript(QString(QChar(char16_t(0x0401))) + QString(3, QChar(char16_t(0x0451)))),
                 ContextVote::Ru);
        QCOMPARE(anchorScript(QString(4, QChar(char16_t(0x0450)))), ContextVote::None);
        QCOMPARE(anchorScript(QString(4, QChar(char16_t(0x0452)))), ContextVote::None);
    }

    void voteUsesOnlyLastTwoAnchors()
    {
        FeatureRow row = makeRow(SourceLayout::Us, usLong(), en3(), en4(), FieldKind::Prose, false, true, false);
        QCOMPARE(contextVote(row), ContextVote::En);
        row.left[0] = ru3();
        row.left[1] = ru4();
        QCOMPARE(contextVote(row), ContextVote::En);
        row.left[2] = ru3();
        row.left[3] = ru4();
        QCOMPARE(contextVote(row), ContextVote::Ru);

        // Только первые два слота — нет голоса.
        row.left = {{en3(), en4(), QString(), QString()}};
        QCOMPARE(contextVote(row), ContextVote::None);
        // Неполный контекст.
        row.left = {{QString(), QString(), QString(), en4()}};
        QCOMPARE(contextVote(row), ContextVote::None);
        row.left = {{QString(), QString(), en3(), QString()}};
        QCOMPARE(contextVote(row), ContextVote::None);
        // Смешанный контекст в обоих порядках.
        row.left = {{QString(), QString(), en3(), ru4()}};
        QCOMPARE(contextVote(row), ContextVote::None);
        row.left = {{QString(), QString(), ru3(), en4()}};
        QCOMPARE(contextVote(row), ContextVote::None);
        // Короткий или не-буквенный анкор.
        row.left = {{QString(), QString(), QStringLiteral("the"), en4()}};
        QCOMPARE(contextVote(row), ContextVote::None);
        row.left = {{QString(), QString(), QStringLiteral("e-mail"), en4()}};
        QCOMPARE(contextVote(row), ContextVote::None);
        // Регистр не мешает.
        row.left = {{QString(), QString(), QStringLiteral("ПРОШУ"), QStringLiteral("Записать")}};
        QCOMPARE(contextVote(row), ContextVote::Ru);

        // Изменение left1/left2 не меняет ни одно действие.
        FeatureRow base = makeRow(SourceLayout::Us, usLong(), ru3(), ru4(), FieldKind::Prose, false, true, false);
        const QByteArray expected = codes(base);
        for (const QString &w : {en3(), ru3(), QString(), QStringLiteral("x")}) {
            FeatureRow changed = base;
            changed.left[0] = w;
            changed.left[1] = w;
            QCOMPARE(codes(changed), expected);
            QCOMPARE(evaluateRow(changed).vote, ContextVote::Ru);
        }
    }

    // ---- Решения ----

    void baselinesIgnoreContextAndField()
    {
        for (const FieldKind field : {FieldKind::Prose, FieldKind::Protected, FieldKind::Unknown}) {
            for (const auto &anchors : {std::array<QString, 2>{{en3(), en4()}}, std::array<QString, 2>{{ru3(), ru4()}},
                                        std::array<QString, 2>{{QString(), QString()}}}) {
                for (const PolicyAction model : {PolicyAction::Keep, PolicyAction::Correct, PolicyAction::Abstain}) {
                    FeatureRow row =
                        makeRow(SourceLayout::Us, usLong(), anchors[0], anchors[1], field, false, true, false);
                    row.tokenModelAction = model;
                    QCOMPARE(decide(ContextMethod::AlwaysKeep, row), PolicyAction::Keep);
                    QCOMPARE(decide(ContextMethod::TokenTwoScore, row), model);
                    QCOMPARE(decide(ContextMethod::TokenHunspell, row), PolicyAction::Correct);
                }
            }
        }
    }

    void fieldGates()
    {
        // protected — Keep во всех context-вариантах, даже без голоса,
        // при недопустимой цели и общей неоднозначности.
        for (const QString &target : {usLong(), usShort(), QStringLiteral("j"), QStringLiteral("a1b2")}) {
            for (const bool ambiguous : {false, true}) {
                for (const auto &anchors :
                     {std::array<QString, 2>{{ru3(), ru4()}}, std::array<QString, 2>{{QString(), QString()}}}) {
                    const FeatureRow p = makeRow(SourceLayout::Us, target, anchors[0], anchors[1],
                                                 FieldKind::Protected, false, true, ambiguous);
                    QCOMPARE(decideContextAuto(p), PolicyAction::Keep);
                    QCOMPARE(decideGuardedContextAuto(p), PolicyAction::Keep);
                    QCOMPARE(decideContextSuggest(p), PolicyAction::Keep);
                    const FeatureRow u = makeRow(SourceLayout::Us, target, anchors[0], anchors[1],
                                                 FieldKind::Unknown, false, true, ambiguous);
                    QCOMPARE(decideContextAuto(u), PolicyAction::Abstain);
                    QCOMPARE(decideGuardedContextAuto(u), PolicyAction::Abstain);
                    QCOMPARE(decideContextSuggest(u), PolicyAction::Abstain);
                }
            }
        }
        // Prose без голоса — Abstain, даже если membership указывает на замену.
        const FeatureRow noVote =
            makeRow(SourceLayout::Us, usLong(), QString(), QString(), FieldKind::Prose, false, true, false);
        QCOMPARE(codes(noVote), QByteArray("KACAAA"));
    }

    void membershipTable()
    {
        struct Case {
            bool observed;
            bool alternative;
            bool ambiguous;
            bool voteMatches;
            const char *expected; // contextAuto, guarded, suggest
        };
        const std::array<Case, 10> cases{{
            {true, false, false, true, "KKK"},
            {true, false, false, false, "KKK"},
            {true, false, true, false, "KAK"},
            {false, true, false, true, "AAA"},
            {false, true, false, false, "CCS"},
            {false, true, true, false, "CAS"},
            {true, true, true, true, "KAK"},
            {true, true, true, false, "CAS"},
            {false, false, false, false, "AAA"},
            {false, false, true, true, "AAA"},
        }};
        for (const SourceLayout layout : {SourceLayout::Us, SourceLayout::Ru}) {
            const QString target = layout == SourceLayout::Us ? usLong() : ruLong();
            for (const Case &c : cases) {
                const bool english = (layout == SourceLayout::Us) == c.voteMatches;
                const FeatureRow row = makeRow(layout, target, english ? en3() : ru3(), english ? en4() : ru4(),
                                               FieldKind::Prose, c.observed, c.alternative, c.ambiguous);
                QByteArray got;
                got.append(policyActionCode(decideContextAuto(row)));
                got.append(policyActionCode(decideGuardedContextAuto(row)));
                got.append(policyActionCode(decideContextSuggest(row)));
                QCOMPARE(got, QByteArray(c.expected));
            }
        }
    }

    void contextSwitchChangesOnlyContextMethods()
    {
        for (const SourceLayout layout : {SourceLayout::Us, SourceLayout::Ru}) {
            const QString target = layout == SourceLayout::Us ? usLong() : ruLong();
            FeatureRow native = makeRow(layout, target, layout == SourceLayout::Us ? en3() : ru3(),
                                        layout == SourceLayout::Us ? en4() : ru4(), FieldKind::Prose, false, true,
                                        false);
            FeatureRow opposite = makeRow(layout, target, layout == SourceLayout::Us ? ru3() : en3(),
                                          layout == SourceLayout::Us ? ru4() : en4(), FieldKind::Prose, false, true,
                                          false);
            native.tokenModelAction = PolicyAction::Correct;
            opposite.tokenModelAction = PolicyAction::Correct;
            const ContextRow n = evaluateRow(native);
            const ContextRow o = evaluateRow(opposite);
            QCOMPARE(codes(n).left(3), codes(o).left(3));
            QCOMPARE(codes(n), QByteArray("KCCAAA"));
            QCOMPARE(codes(o), QByteArray("KCCCCS"));
            QVERIFY(n.vote != o.vote);
        }
    }

    void ambiguityVeto()
    {
        // Общая неоднозначность: contextAuto исправляет, guarded воздерживается,
        // suggest только предлагает.
        const FeatureRow ambiguous =
            makeRow(SourceLayout::Us, usLong(), ru3(), ru4(), FieldKind::Prose, false, true, true);
        QCOMPARE(decideContextAuto(ambiguous), PolicyAction::Correct);
        QCOMPARE(decideGuardedContextAuto(ambiguous), PolicyAction::Abstain);
        QCOMPARE(decideContextSuggest(ambiguous), PolicyAction::Suggest);
        FeatureRow clear = ambiguous;
        clear.sharedAmbiguous = false;
        QCOMPARE(decideGuardedContextAuto(clear), PolicyAction::Correct);
        // Вето действует и на Keep: guarded не сохраняет «уверенно».
        const FeatureRow keepAmbiguous =
            makeRow(SourceLayout::Us, usLong(), en3(), en4(), FieldKind::Prose, true, true, true);
        QCOMPARE(decideContextAuto(keepAmbiguous), PolicyAction::Keep);
        QCOMPARE(decideGuardedContextAuto(keepAmbiguous), PolicyAction::Abstain);
    }

    // Перебор наблюдаемых комбинаций: инварианты, не зависящие от данных.
    void exhaustiveInvariants()
    {
        const std::array<std::array<QString, 2>, 6> anchorSets{{
            {{en3(), en4()}},
            {{ru3(), ru4()}},
            {{en3(), ru4()}},
            {{QString(), en4()}},
            {{QString(), QString()}},
            {{QStringLiteral("the"), QStringLiteral("cat")}},
        }};
        int checked = 0;
        for (const SourceLayout layout : {SourceLayout::Us, SourceLayout::Ru}) {
            const std::array<QString, 4> targets{{layout == SourceLayout::Us ? usLong() : ruLong(),
                                                  layout == SourceLayout::Us ? usShort() : QStringLiteral("да"),
                                                  layout == SourceLayout::Us ? QStringLiteral("j") : QStringLiteral("д"),
                                                  QStringLiteral("1234")}};
            for (const QString &target : targets) {
                for (const FieldKind field : {FieldKind::Prose, FieldKind::Protected, FieldKind::Unknown}) {
                    for (const auto &anchors : anchorSets) {
                        for (int bits = 0; bits < 8; ++bits) {
                            const bool obs = bits & 1;
                            const bool alt = bits & 2;
                            const bool amb = bits & 4;
                            if (obs && alt && !amb)
                                continue;
                            FeatureRow row = makeRow(layout, target, anchors[0], anchors[1], field, obs, alt, amb);
                            const ContextRow r = evaluateRow(row);
                            const PolicyAction autoA = r.actions[3];
                            const PolicyAction guarded = r.actions[4];
                            const PolicyAction suggest = r.actions[5];
                            QCOMPARE(r.actions[0], PolicyAction::Keep);
                            QCOMPARE(r.actions[1], row.tokenModelAction);
                            QCOMPARE(r.actions[2], row.tokenFullAction);
                            for (std::size_t i = 0; i < 5; ++i)
                                QVERIFY(r.actions[i] != PolicyAction::Suggest);
                            QVERIFY(suggest != PolicyAction::Correct);
                            QCOMPARE(suggest, autoA == PolicyAction::Correct ? PolicyAction::Suggest : autoA);
                            QVERIFY(guarded == autoA || guarded == PolicyAction::Abstain);
                            if (field == FieldKind::Protected) {
                                QCOMPARE(autoA, PolicyAction::Keep);
                                QCOMPARE(guarded, PolicyAction::Keep);
                            } else if (field == FieldKind::Unknown) {
                                QCOMPARE(autoA, PolicyAction::Abstain);
                                QCOMPARE(guarded, PolicyAction::Abstain);
                            } else {
                                if (!isContextTarget(target, layout) || r.vote == ContextVote::None)
                                    QCOMPARE(autoA, PolicyAction::Abstain);
                                if (amb)
                                    QCOMPARE(guarded, PolicyAction::Abstain);
                                if (autoA == PolicyAction::Correct) {
                                    QVERIFY(alt);
                                    const bool matches = (r.vote == ContextVote::En) == (layout == SourceLayout::Us);
                                    QVERIFY(!matches);
                                }
                                if (autoA == PolicyAction::Keep)
                                    QVERIFY(obs);
                            }
                            // Детерминированность: повторное вычисление идентично.
                            QCOMPARE(codes(evaluateRow(row)), codes(r));
                            ++checked;
                        }
                    }
                }
            }
        }
        QVERIFY(checked > 1000);
    }

    // Одинаковые наблюдаемые байты (switchCorrect и borrowKeep) неизбежно
    // получают одно действие: ни одна политика не может исправить один и
    // сохранить другой.
    void identicalObservablesSameOutput()
    {
        const QString collision = join({QStringLiteral("us"), usLong(), QString(), QString(), ru3(), ru4(),
                                        QStringLiteral("prose"), QStringLiteral("0"), QStringLiteral("1"),
                                        QStringLiteral("0"), QStringLiteral("C"), QStringLiteral("C")});
        const QString other = join(goodFields());
        const FeatureResult parsed = parseFeatures(tsv({collision, other, collision}), 3);
        QCOMPARE(parsed.error, FeatureError::None);
        const QByteArray output = renderOutput(parsed.rows);
        const QList<QByteArray> lines = output.split('\n');
        QCOMPARE(lines.size(), qsizetype(4));
        QVERIFY(lines.at(3).isEmpty());
        QCOMPARE(lines.at(0).mid(1), lines.at(2).mid(1));
        QCOMPARE(lines.at(0), QByteArray("0\tR\tKCCCCS"));
        QCOMPARE(lines.at(2), QByteArray("2\tR\tKCCCCS"));
        QCOMPARE(lines.at(1), QByteArray("1\tU\tKCCAAA"));
        for (std::size_t m = 0; m < kContextMethods.size(); ++m)
            QCOMPARE(evaluateRow(parsed.rows[0]).actions[m], evaluateRow(parsed.rows[2]).actions[m]);
    }

    // ---- Вывод ----

    void outputFormat()
    {
        FeatureRow a = makeRow(SourceLayout::Us, usLong(), ru3(), ru4(), FieldKind::Prose, false, true, false);
        a.tokenModelAction = PolicyAction::Correct;
        FeatureRow b = a;
        b.field = FieldKind::Protected;
        FeatureRow c = makeRow(SourceLayout::Ru, ruLong(), QString(), QString(), FieldKind::Unknown, false, true, false);
        const QByteArray output = renderOutput({a, b, c});
        QCOMPARE(output, QByteArray("0\tR\tKCCCCS\n1\tR\tKCCKKK\n2\t-\tKACAAA\n"));
        // Ни слов, ни анкоров, ни имён полей.
        for (const QString &word : {usLong(), ruLong(), ru3(), ru4(), QStringLiteral("protected")})
            QVERIFY(!output.contains(word.toUtf8()));
        const QByteArray alphabet("0123456789\tURKCAS-\n");
        for (const char ch : output)
            QVERIFY(alphabet.contains(ch));

        QByteArray single;
        appendOutputRow(single, 99999, evaluateRow(a));
        QCOMPARE(single, QByteArray("99999\tR\tKCCCCS\n"));
        QCOMPARE(contextVoteCode(ContextVote::En), 'U');
        QCOMPARE(contextVoteCode(ContextVote::Ru), 'R');
        QCOMPARE(contextVoteCode(ContextVote::None), '-');
        QCOMPARE(policyActionCode(PolicyAction::Suggest), 'S');
    }

    void outputBound()
    {
        const FeatureRow row = makeRow(SourceLayout::Us, usLong(), ru3(), ru4(), FieldKind::Prose, false, true, false);
        std::vector<FeatureRow> rows(std::size_t(kMaxFeatureRows), row);
        const QByteArray output = renderOutput(rows);
        // Сумма цифр порядковых номеров 0..99999 плюс 10 байт на строку.
        QCOMPARE(qint64(output.size()), qint64(488890 + 10 * kMaxFeatureRows));
        QVERIFY(qint64(output.size()) < kMaxOutputBytes);
        QVERIFY(output.startsWith("0\tR\tKACCCS\n"));
        QVERIFY(output.endsWith("99999\tR\tKACCCS\n"));
        rows.push_back(row);
        QVERIFY(renderOutput(rows).isEmpty());
    }
};

QTEST_GUILESS_MAIN(ContextTests)
#include "context_tests.moc"
