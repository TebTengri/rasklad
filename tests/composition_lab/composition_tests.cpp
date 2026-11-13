#include "composer.h"
#include <QTest>
using namespace rasklad;
class Evidence final : public routing::EvidenceSource {
public:
    mutable int calls = 0;
    std::optional<routing::TokenEvidence> evidence(const routing::EvidenceQuery &q) const override {
        ++calls;
        routing::TokenEvidence e;
        if (q.token == QStringLiteral("ghbdtn")) {
            e.full.alternative = true;
            e.model = DecisionAction::Correct;
            return e;
        }
        if (q.token == QStringLiteral("hello")) { e.full.observed = true; return e; }
        return std::nullopt;
    }
};
class CompositionTests : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void boundaryUsesRouter() {
        Evidence evidence;
        composition::Composer c(routing::InputLayout::KnownUs, &evidence);
        QString shown;
        for (char32_t key : QStringLiteral("ghbdtn").toUcs4()) {
            const auto out = c.text(key);
            QVERIFY(out.commit.isEmpty());
            shown += QString::fromUcs4(&key, 1);
            QCOMPARE(out.preedit, shown);
        }
        QCOMPARE(evidence.calls, 0);
        const auto out = c.text(U' ');
        QCOMPARE(out.commit, QString::fromUtf8("привет "));
        QVERIFY(out.corrected && out.preedit.isEmpty());
        QCOMPARE(evidence.calls, 1);
        QCOMPARE(c.buffered(), 0);
    }
    void unavailableEvidenceKeepsOriginal() {
        composition::Composer c(routing::InputLayout::KnownUs, nullptr);
        for (char32_t key : QStringLiteral("ghbdtn").toUcs4()) (void)c.text(key);
        const auto out = c.text(U' ');
        QCOMPARE(out.commit, QStringLiteral("ghbdtn "));
        QVERIFY(!out.corrected);
    }
    void knownAndShortWordsKept() {
        Evidence evidence;
        composition::Composer c(routing::InputLayout::KnownUs, &evidence);
        for (char32_t key : QStringLiteral("hello").toUcs4()) (void)c.text(key);
        QCOMPARE(c.text(U' ').commit, QStringLiteral("hello "));
        for (char32_t key : QStringLiteral("x").toUcs4()) (void)c.text(key);
        QCOMPARE(c.text(U' ').commit, QStringLiteral("x "));
        QCOMPARE(evidence.calls, 1);
    }
    void longWordPreservedWithBoundedBuffer() {
        Evidence evidence;
        composition::Composer c(routing::InputLayout::KnownUs, &evidence);
        QString committed;
        const QString input(10000, QLatin1Char('z'));
        for (char32_t key : input.toUcs4()) {
            const auto out = c.text(key);
            committed += out.commit;
            QVERIFY(c.buffered() <= composition::Composer::kMaxBuffered);
        }
        committed += c.text(U' ').commit;
        QCOMPARE(committed, input + QLatin1Char(' '));
        QCOMPARE(evidence.calls, 0);
    }
    void backspaceOnlyEditsOwnedPreedit() {
        composition::Composer c(routing::InputLayout::KnownUs, nullptr);
        QVERIFY(!c.backspace().handled);
        (void)c.text(U'a');
        (void)c.text(U'b');
        QCOMPARE(c.backspace().preedit, QStringLiteral("a"));
        QVERIFY(c.backspace().preedit.isEmpty());
        QVERIFY(!c.backspace().handled);
    }
    void unsupportedInputIsLiteralAndNotAnalysed() {
        Evidence evidence;
        composition::Composer c(routing::InputLayout::KnownUs, &evidence);
        QString committed;
        const QString text = QString::fromUtf8("ghbdtn🙂 ");
        for (char32_t key : text.toUcs4()) {
            const auto out = c.text(key);
            QCOMPARE(QString::fromUtf8(out.commit.toUtf8()), out.commit);
            committed += out.commit;
        }
        QCOMPARE(committed, text);
        QCOMPARE(evidence.calls, 0);
    }
    void invalidScalarDoesNotChangeBuffer() {
        composition::Composer c(routing::InputLayout::KnownUs, nullptr);
        (void)c.text(U'a');
        QVERIFY(!c.text(char32_t(0)).handled);
        QVERIFY(!c.text(U'\t').handled);
        QVERIFY(!c.text(char32_t(0xd800)).handled);
        QVERIFY(!c.text(char32_t(0x110000)).handled);
        QCOMPARE(c.buffered(), 1);
        QCOMPARE(c.text(U' ').commit, QStringLiteral("a "));
    }
    void interruptedWordIsFinalizedLiterallyWithoutEvidence() {
        Evidence evidence;
        composition::Composer c(routing::InputLayout::KnownUs, &evidence);
        for (char32_t key : QStringLiteral("ghbdtn").toUcs4()) (void)c.text(key);
        const auto out = c.finishLiteral();
        QCOMPARE(out.commit, QStringLiteral("ghbdtn"));
        QVERIFY(out.preedit.isEmpty() && !out.corrected);
        QCOMPARE(c.buffered(), 0);
        QVERIFY(c.finishLiteral().commit.isEmpty());
        QCOMPARE(evidence.calls, 0);
        QCOMPARE(c.text(U' ').commit, QStringLiteral(" "));
        for (int i = 0; i < 33; ++i) (void)c.text(U'z');
        QVERIFY(c.finishLiteral().commit.isEmpty()); // already committed, never duplicate
        QCOMPARE(c.text(U'a').preedit, QStringLiteral("a"));
    }
    void unknownLayoutNotHandled() {
        composition::Composer c(routing::InputLayout::Unknown, nullptr);
        QVERIFY(!c.text(U'a').handled);
        QCOMPARE(c.buffered(), 0);
    }
};
QTEST_GUILESS_MAIN(CompositionTests)
#include "composition_tests.moc"
