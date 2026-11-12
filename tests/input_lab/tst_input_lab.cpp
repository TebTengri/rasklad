// SYS-223 input-method-v2 wire laboratory tests. Real libwayland wire between a
// fake server and an IM client over a socketpair; synthetic protocol state and
// invented text only. Results say nothing about real compositors, applications,
// field purposes or Hyprland compatibility (docs/INPUT-LAB-001.md).
#include <QtTest>

#include "labharness.h"
#include "labmodel.h"

#include <cstdint>
#include <limits>
#include <string>
#include <string_view>

using namespace inputlab;

namespace {

QByteArray bytes(std::string_view s)
{
    return QByteArray(s.data(), static_cast<qsizetype>(s.size()));
}

const char* kindName(ImEventKind kind)
{
    switch (kind) {
    case ImEventKind::Activate: return "activate";
    case ImEventKind::Deactivate: return "deactivate";
    case ImEventKind::SurroundingText: return "surrounding_text";
    case ImEventKind::TextChangeCause: return "text_change_cause";
    case ImEventKind::ContentType: return "content_type";
    case ImEventKind::Done: return "done";
    case ImEventKind::Unavailable: return "unavailable";
    }
    return "?";
}

QByteArray kinds(const ImRecord& record)
{
    QByteArrayList parts;
    for (const ImEvent& e : record.events)
        parts << QByteArray(kindName(e.kind));
    return parts.join(',');
}

SyntheticField fieldAtEnd(std::string text, std::uint32_t purpose = tiv3::kPurposeNormal)
{
    SyntheticField f;
    f.cursor = static_cast<std::uint32_t>(text.size());
    f.text = std::move(text);
    f.purpose = purpose;
    return f;
}

SerialPolicy policyFor(bool strict)
{
    return strict ? SerialPolicy::Strict : SerialPolicy::UncheckedModel;
}

void addPolicyRows()
{
    QTest::addColumn<bool>("strict");
    QTest::newRow("strict-reference") << true;
    QTest::newRow("unchecked-model") << false;
}

const QByteArray kActivateSequence("activate,surrounding_text,text_change_cause,content_type,done");

} // namespace

class InputLabTest : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void utf8Model();
    void embeddedNulRejected();
    void wireOrderingOnActivateAndRefocus();
    void validReplacement_data() { addPolicyRows(); }
    void validReplacement();
    void staleSerialAfterNewTyping_data() { addPolicyRows(); }
    void staleSerialAfterNewTyping();
    void staleSerialAfterFocusChange_data() { addPolicyRows(); }
    void staleSerialAfterFocusChange();
    void pendingAcrossActivation_data() { addPolicyRows(); }
    void pendingAcrossActivation();
    void commitAfterDeactivate_data() { addPolicyRows(); }
    void commitAfterDeactivate();
    void invalidEditsRejected_data();
    void invalidEditsRejected();
    void unsupportedRequestsRejected_data();
    void unsupportedRequestsRejected();
    void secondInputMethodUnavailable_data() { addPolicyRows(); }
    void secondInputMethodUnavailable();
    void serverUnavailable();
    void disconnectCleansUp();
};

void InputLabTest::initTestCase()
{
    // The lab never uses these, but their presence means the binary was not
    // started through bin/test-input-lab (WAYLAND_DEBUG would also echo payloads).
    for (const char* name : {"WAYLAND_DISPLAY", "WAYLAND_SOCKET", "WAYLAND_DEBUG"})
        QVERIFY2(qEnvironmentVariableIsEmpty(name), "run via bin/test-input-lab");
}

void InputLabTest::utf8Model()
{
    QVERIFY(isValidUtf8(""));
    QVERIFY(isValidUtf8("ascii"));
    QVERIFY(isValidUtf8("привет"));
    QVERIFY(!isValidUtf8("\xD0"));             // truncated sequence
    QVERIFY(!isValidUtf8("\xC0\xAF"));         // overlong
    QVERIFY(!isValidUtf8("\xED\xA0\x80"));     // UTF-16 surrogate
    QVERIFY(!isValidUtf8("\xF4\x90\x80\x80")); // > U+10FFFF
    QVERIFY(!isValidUtf8("\x80"));             // lone continuation

    const std::string_view mir = "мир"; // 3 code points, 6 bytes
    QVERIFY(isUtf8Boundary(mir, 0));
    QVERIFY(!isUtf8Boundary(mir, 1));
    QVERIFY(isUtf8Boundary(mir, 2));
    QVERIFY(isUtf8Boundary(mir, 6));
    QVERIFY(!isUtf8Boundary(mir, 7));

    SyntheticField f = fieldAtEnd("мир");
    constexpr auto kMax = std::numeric_limits<std::uint32_t>::max();
    QVERIFY(applyImeEdit(f, 1, 0, "") == EditResult::InvalidRange);
    QVERIFY(applyImeEdit(f, kMax, kMax, "") == EditResult::InvalidRange);
    QVERIFY(applyImeEdit(f, 0, 1, "") == EditResult::InvalidRange);
    QVERIFY(applyImeEdit(f, 0, 0, "\xD0") == EditResult::InvalidUtf8);
    QCOMPARE(bytes(f.text), QByteArray("мир"));
    QCOMPARE(f.cursor, 6u);

    QVERIFY(applyImeEdit(f, 2, 0, "") == EditResult::Applied);
    QCOMPARE(bytes(f.text), QByteArray("ми"));
    QCOMPARE(f.cursor, 4u);

    SyntheticField big = fieldAtEnd(std::string(kMaxProtocolTextBytes - 1, 'a'));
    QVERIFY(applyImeEdit(big, 0, 0, "bb") == EditResult::TooLong);
    QVERIFY(!insertAtCursor(big, "bb"));
    QVERIFY(insertAtCursor(big, "b"));
    QCOMPARE(big.text.size(), kMaxProtocolTextBytes);
}

void InputLabTest::embeddedNulRejected()
{
    const std::string nulText("a\0b", 3);
    SyntheticField f = fieldAtEnd("word");
    QVERIFY(!isValidField(fieldAtEnd(nulText)));
    QVERIFY(applyImeEdit(f, 4, 0, nulText) == EditResult::EmbeddedNul);
    QVERIFY(!insertAtCursor(f, nulText));
    QCOMPARE(bytes(f.text), QByteArray("word"));
    QCOMPARE(f.cursor, 4u);

    LabHarness lab(SerialPolicy::Strict);
    QVERIFY(lab.start());
    QVERIFY(!lab.server().addField(fieldAtEnd(nulText)));
    const auto id = lab.server().addField(f);
    const auto im = lab.client().createInputMethod();
    QVERIFY(id && im);
    QVERIFY(lab.roundtrip());
    QVERIFY(lab.server().focus(*id));
    QVERIFY(lab.roundtrip());
    QVERIFY(!lab.server().userTypes(nulText));
    QVERIFY(!lab.client().commitString(*im, nulText));
    QVERIFY(lab.client().commitWithLatestSerial(*im));
    QVERIFY(lab.roundtrip());
    QCOMPARE(bytes(lab.server().field(*id).text), QByteArray("word"));
    QCOMPARE(bytes(lab.client().record(*im).current.surrounding), QByteArray("word"));
    QCOMPARE(lab.server().stats().applied, 0u);
}

void InputLabTest::wireOrderingOnActivateAndRefocus()
{
    LabHarness lab(SerialPolicy::Strict);
    QVERIFY(lab.start());
    QCOMPARE(lab.client().seatCapabilities().value_or(99u), 0u);
    const auto im = lab.client().createInputMethod();
    QVERIFY(im);
    QVERIFY(lab.roundtrip());
    QVERIFY(lab.server().hasInputMethod());
    QCOMPARE(kinds(lab.client().record(*im)), QByteArray());

    const auto a = lab.server().addField(fieldAtEnd("alpha"));
    const auto p = lab.server().addField(fieldAtEnd("", tiv3::kPurposePassword));
    QVERIFY(a && p);
    QVERIFY(lab.server().focus(*a));
    QVERIFY(lab.roundtrip());

    const ImRecord& rec = lab.client().record(*im);
    QCOMPARE(kinds(rec), kActivateSequence);
    QVERIFY(rec.current.active);
    QVERIFY(rec.current.hasSurrounding);
    QCOMPARE(bytes(rec.current.surrounding), QByteArray("alpha"));
    QCOMPARE(rec.current.cursor, 5u);
    QCOMPARE(rec.current.anchor, 5u);
    QCOMPARE(rec.current.cause, tiv3::kCauseInputMethod);
    QCOMPARE(rec.current.hint, tiv3::kHintNone);
    QCOMPARE(rec.current.purpose, tiv3::kPurposeNormal);
    QCOMPARE(rec.doneSeen, 1u);
    QCOMPARE(lab.server().doneCount(), 1u);

    // Focus change: deactivate+done, then the full activation sequence. The
    // purpose value is only transported here; the lab makes no policy decision.
    QVERIFY(lab.server().focus(*p));
    QVERIFY(lab.roundtrip());
    QCOMPARE(kinds(rec), kActivateSequence + ",deactivate,done," + kActivateSequence);
    QVERIFY(rec.current.active);
    QCOMPARE(rec.current.purpose, tiv3::kPurposePassword);
    QCOMPARE(rec.doneSeen, 3u);
    QCOMPARE(lab.server().doneCount(), 3u);
}

void InputLabTest::validReplacement()
{
    QFETCH(bool, strict);
    LabHarness lab(policyFor(strict));
    QVERIFY(lab.start());
    const auto im = lab.client().createInputMethod();
    QVERIFY(im);
    const auto id = lab.server().addField(fieldAtEnd("ghbdtn")); // invented layout slip
    QVERIFY(id);
    QVERIFY(lab.roundtrip());
    QVERIFY(lab.server().focus(*id));
    QVERIFY(lab.roundtrip());

    LabClient& c = lab.client();
    QVERIFY(c.deleteSurroundingText(*im, 6, 0)); // bytes, not characters
    QVERIFY(c.commitString(*im, "привет"));
    QVERIFY(c.commitWithLatestSerial(*im));
    QVERIFY(lab.roundtrip());

    const SyntheticField& f = lab.server().field(*id);
    QCOMPARE(bytes(f.text), QByteArray("привет"));
    QCOMPARE(f.cursor, 12u);                                         // UTF-8 bytes
    QCOMPARE(QString::fromStdString(f.text).size(), qsizetype(6));  // UTF-16 units differ
    QCOMPARE(lab.server().stats().applied, 1u);
    QCOMPARE(lab.server().stats().staleRejected, 0u);
    QCOMPARE(lab.server().stats().staleForwarded, 0u);

    const ImRecord& rec = c.record(*im);
    QCOMPARE(bytes(rec.current.surrounding), QByteArray("привет"));
    QCOMPARE(rec.current.cursor, 12u);
    QCOMPARE(rec.doneSeen, 2u);
    QCOMPARE(lab.server().doneCount(), 2u);
}

void InputLabTest::staleSerialAfterNewTyping()
{
    QFETCH(bool, strict);
    LabHarness lab(policyFor(strict));
    QVERIFY(lab.start());
    const auto im = lab.client().createInputMethod();
    QVERIFY(im);
    const auto id = lab.server().addField(fieldAtEnd("alpha brvo"));
    QVERIFY(id);
    QVERIFY(lab.roundtrip());
    QVERIFY(lab.server().focus(*id));
    QVERIFY(lab.roundtrip());

    LabClient& c = lab.client();
    const std::uint32_t seen = c.record(*im).doneSeen;
    QCOMPARE(seen, 1u);
    // IM computes a replacement for "brvo" against done #1; requests stay queued.
    QVERIFY(c.deleteSurroundingText(*im, 4, 0));
    QVERIFY(c.commitString(*im, "bravo"));
    QVERIFY(c.commit(*im, seen));
    // Before they arrive the user keeps typing: done #2 is issued first.
    QVERIFY(lab.server().userTypes(" c"));
    QCOMPARE(lab.server().doneCount(), 2u);
    QVERIFY(lab.roundtrip());

    const SyntheticField& f = lab.server().field(*id);
    const LabServer::Stats& s = lab.server().stats();
    if (strict) {
        QCOMPARE(bytes(f.text), QByteArray("alpha brvo c")); // new typing preserved
        QCOMPARE(s.staleRejected, 1u);
        QCOMPARE(s.applied, 0u);
        QCOMPARE(lab.server().doneCount(), 2u);
    } else {
        // Stale delete removes the user's fresh " c" and two old bytes.
        QCOMPARE(bytes(f.text), QByteArray("alpha brbravo"));
        QCOMPARE(s.staleForwarded, 1u);
        QCOMPARE(s.applied, 1u);
    }
    // In both modes the client has caught up with the server's done count.
    QCOMPARE(c.record(*im).doneSeen, lab.server().doneCount());
}

void InputLabTest::staleSerialAfterFocusChange()
{
    QFETCH(bool, strict);
    LabHarness lab(policyFor(strict));
    QVERIFY(lab.start());
    const auto im = lab.client().createInputMethod();
    QVERIFY(im);
    const auto a = lab.server().addField(fieldAtEnd("one tw"));
    const auto b = lab.server().addField(fieldAtEnd("three"));
    QVERIFY(a && b);
    QVERIFY(lab.roundtrip());
    QVERIFY(lab.server().focus(*a));
    QVERIFY(lab.roundtrip());

    LabClient& c = lab.client();
    const std::uint32_t seen = c.record(*im).doneSeen;
    QVERIFY(c.deleteSurroundingText(*im, 2, 0)); // meant for field A
    QVERIFY(c.commitString(*im, "two"));
    QVERIFY(c.commit(*im, seen));
    QVERIFY(lab.server().focus(*b)); // done #2 (deactivate), done #3 (activate B)
    QVERIFY(lab.roundtrip());

    QCOMPARE(bytes(lab.server().field(*a).text), QByteArray("one tw"));
    if (strict) {
        QCOMPARE(bytes(lab.server().field(*b).text), QByteArray("three"));
        QCOMPARE(lab.server().stats().staleRejected, 1u);
    } else {
        // Edit computed for A lands in B.
        QCOMPARE(bytes(lab.server().field(*b).text), QByteArray("thrtwo"));
        QCOMPARE(lab.server().stats().staleForwarded, 1u);
    }

    // A commit built on the latest done is accepted in both modes.
    QCOMPARE(c.record(*im).doneSeen, lab.server().doneCount());
    QVERIFY(c.commitString(*im, "!"));
    QVERIFY(c.commitWithLatestSerial(*im));
    QVERIFY(lab.roundtrip());
    QCOMPARE(bytes(lab.server().field(*b).text), strict ? QByteArray("three!") : QByteArray("thrtwo!"));
}

void InputLabTest::pendingAcrossActivation()
{
    QFETCH(bool, strict);
    LabHarness lab(policyFor(strict));
    QVERIFY(lab.start());
    const auto im = lab.client().createInputMethod();
    QVERIFY(im);
    const auto a = lab.server().addField(fieldAtEnd("one tw"));
    const auto b = lab.server().addField(fieldAtEnd("three"));
    QVERIFY(a && b);
    QVERIFY(lab.roundtrip());
    QVERIFY(lab.server().focus(*a));
    QVERIFY(lab.roundtrip());

    LabClient& c = lab.client();
    QVERIFY(c.commitString(*im, "stale")); // pending, never committed for A
    QVERIFY(lab.roundtrip());              // the server now holds it
    QVERIFY(lab.server().focus(*b));
    QVERIFY(lab.roundtrip());
    QVERIFY(c.commitWithLatestSerial(*im)); // fresh serial, nothing new pending
    QVERIFY(lab.roundtrip());

    QCOMPARE(bytes(lab.server().field(*a).text), QByteArray("one tw"));
    const LabServer::Stats& s = lab.server().stats();
    if (strict) {
        QCOMPARE(bytes(lab.server().field(*b).text), QByteArray("three"));
        QCOMPARE(s.pendingDroppedOnActivate, 1u);
        QCOMPARE(s.emptyCommits, 1u);
        QCOMPARE(s.applied, 0u);
    } else {
        QCOMPARE(bytes(lab.server().field(*b).text), QByteArray("threestale"));
        QCOMPARE(s.applied, 1u);
        QCOMPARE(s.staleForwarded, 0u); // the serial itself was fresh
    }
}

void InputLabTest::commitAfterDeactivate()
{
    QFETCH(bool, strict);
    LabHarness lab(policyFor(strict));
    QVERIFY(lab.start());
    const auto im = lab.client().createInputMethod();
    QVERIFY(im);
    const auto a = lab.server().addField(fieldAtEnd("one tw"));
    QVERIFY(a);
    QVERIFY(lab.roundtrip());
    QVERIFY(lab.server().focus(*a));
    QVERIFY(lab.roundtrip());
    QVERIFY(lab.server().deactivate());
    QVERIFY(!lab.server().deactivate()); // already inactive
    QVERIFY(lab.roundtrip());

    LabClient& c = lab.client();
    QVERIFY(!c.record(*im).current.active);
    QVERIFY(!c.record(*im).current.hasSurrounding);
    QCOMPARE(c.record(*im).doneSeen, 2u);
    QVERIFY(c.commitString(*im, "x"));
    QVERIFY(c.commitWithLatestSerial(*im)); // serial is fresh; the input method is inactive
    QVERIFY(lab.roundtrip());

    if (strict) {
        QCOMPARE(bytes(lab.server().field(*a).text), QByteArray("one tw"));
        QCOMPARE(lab.server().stats().inactiveIgnored, 1u);
    } else {
        // Model forwards to the still-focused but disabled field.
        QCOMPARE(bytes(lab.server().field(*a).text), QByteArray("one twx"));
        QCOMPARE(lab.server().stats().applied, 1u);
    }
}

void InputLabTest::invalidEditsRejected_data()
{
    QTest::addColumn<uint>("before");
    QTest::addColumn<uint>("after");
    QTest::addColumn<QByteArray>("commitText");
    QTest::newRow("delete-splits-code-point") << 1u << 0u << QByteArray();
    QTest::newRow("delete-before-start") << 7u << 0u << QByteArray();
    QTest::newRow("delete-after-end") << 0u << 1u << QByteArray();
    QTest::newRow("commit-truncated-utf8") << 0u << 0u << QByteArray("\xD0");
    QTest::newRow("commit-overlong-utf8") << 0u << 0u << QByteArray("\xC0\xAF");
    QTest::newRow("valid-delete-invalid-commit") << 2u << 0u << QByteArray("\xED\xA0\x80");
    QTest::newRow("commit-over-4000-bytes") << 0u << 0u << QByteArray(4001, 'a');
    QTest::newRow("result-over-4000-bytes") << 0u << 0u << QByteArray(3995, 'a');
}

void InputLabTest::invalidEditsRejected()
{
    QFETCH(uint, before);
    QFETCH(uint, after);
    QFETCH(QByteArray, commitText);
    LabHarness lab(SerialPolicy::Strict);
    QVERIFY(lab.start());
    const auto im = lab.client().createInputMethod();
    QVERIFY(im);
    const auto id = lab.server().addField(fieldAtEnd("мир"));
    QVERIFY(id);
    QVERIFY(lab.roundtrip());
    QVERIFY(lab.server().focus(*id));
    QVERIFY(lab.roundtrip());

    LabClient& c = lab.client();
    if (before || after)
        QVERIFY(c.deleteSurroundingText(*im, before, after));
    if (!commitText.isEmpty())
        QVERIFY(c.commitString(*im, std::string_view(commitText.constData(),
                                                     static_cast<std::size_t>(commitText.size()))));
    QVERIFY(c.commitWithLatestSerial(*im));
    QVERIFY(lab.roundtrip());

    // All-or-nothing: no partial delete, no state re-sent.
    QCOMPARE(bytes(lab.server().field(*id).text), QByteArray("мир"));
    QCOMPARE(lab.server().field(*id).cursor, 6u);
    QCOMPARE(lab.server().stats().invalidRejected, 1u);
    QCOMPARE(lab.server().stats().applied, 0u);
    QCOMPARE(lab.server().doneCount(), 1u);
}

void InputLabTest::unsupportedRequestsRejected_data()
{
    QTest::addColumn<int>("request");
    QTest::newRow("grab_keyboard") << static_cast<int>(UnsupportedRequest::GrabKeyboard);
    QTest::newRow("set_preedit_string") << static_cast<int>(UnsupportedRequest::SetPreeditString);
    QTest::newRow("wl_seat.get_keyboard") << static_cast<int>(UnsupportedRequest::SeatGetKeyboard);
}

void InputLabTest::unsupportedRequestsRejected()
{
    QFETCH(int, request);
    LabHarness lab(SerialPolicy::Strict);
    QVERIFY(lab.start());
    const auto im = lab.client().createInputMethod();
    QVERIFY(im);
    const auto a = lab.server().addField(fieldAtEnd("one tw"));
    QVERIFY(a);
    QVERIFY(lab.roundtrip());
    QVERIFY(lab.server().focus(*a));
    QVERIFY(lab.roundtrip());

    QVERIFY(lab.client().sendUnsupported(*im, static_cast<UnsupportedRequest>(request)));
    QVERIFY(!lab.roundtrip());                  // protocol error ends the connection
    QVERIFY(lab.client().displayError() != 0);  // EPROTO, or EPIPE if closed first

    lab.disconnectClient();
    QVERIFY(!lab.server().hasInputMethod());
    QCOMPARE(lab.server().imObjectCount(), std::size_t(0));
    QCOMPARE(bytes(lab.server().field(*a).text), QByteArray("one tw"));
    QCOMPARE(lab.server().stats().applied, 0u);
}

void InputLabTest::secondInputMethodUnavailable()
{
    QFETCH(bool, strict);
    LabHarness lab(policyFor(strict));
    QVERIFY(lab.start());
    const auto first = lab.client().createInputMethod();
    const auto second = lab.client().createInputMethod();
    QVERIFY(first && second);
    QVERIFY(lab.roundtrip());

    LabClient& c = lab.client();
    QCOMPARE(kinds(c.record(*second)), QByteArray("unavailable"));
    QVERIFY(c.record(*second).unavailable);
    QCOMPARE(kinds(c.record(*first)), QByteArray());

    const auto a = lab.server().addField(fieldAtEnd("one tw"));
    QVERIFY(a);
    QVERIFY(lab.server().focus(*a));
    QVERIFY(lab.roundtrip());
    QCOMPARE(kinds(c.record(*first)), kActivateSequence);
    QCOMPARE(kinds(c.record(*second)), QByteArray("unavailable")); // still the only event

    QVERIFY(c.commitString(*second, "intruder"));
    QVERIFY(c.commit(*second, 0));
    QVERIFY(lab.roundtrip());
    QCOMPARE(bytes(lab.server().field(*a).text), QByteArray("one tw"));
    QCOMPARE(lab.server().stats().inertIgnored, 1u);
    QCOMPARE(lab.server().stats().applied, 0u);
}

void InputLabTest::serverUnavailable()
{
    LabHarness lab(SerialPolicy::UncheckedModel); // inert check precedes either policy
    QVERIFY(lab.start());
    const auto im = lab.client().createInputMethod();
    QVERIFY(im);
    const auto a = lab.server().addField(fieldAtEnd("one tw"));
    const auto b = lab.server().addField(fieldAtEnd("three"));
    QVERIFY(a && b);
    QVERIFY(lab.roundtrip());
    QVERIFY(lab.server().focus(*a));
    QVERIFY(lab.roundtrip());

    QVERIFY(lab.server().makeUnavailable());
    QVERIFY(!lab.server().makeUnavailable());
    QVERIFY(lab.roundtrip());
    LabClient& c = lab.client();
    const ImRecord& rec = c.record(*im);
    QVERIFY(rec.unavailable);
    QCOMPARE(kinds(rec), kActivateSequence + ",unavailable");
    QVERIFY(!lab.server().hasInputMethod());

    QVERIFY(c.commitString(*im, "late"));
    QVERIFY(c.commitWithLatestSerial(*im));
    QVERIFY(lab.roundtrip());
    QCOMPARE(bytes(lab.server().field(*a).text), QByteArray("one tw"));
    QCOMPARE(lab.server().stats().inertIgnored, 1u);

    const auto eventsBefore = rec.events.size();
    QVERIFY(lab.server().focus(*b)); // nothing is sent to an inert object
    QVERIFY(lab.roundtrip());
    QCOMPARE(rec.events.size(), eventsBefore);
}

void InputLabTest::disconnectCleansUp()
{
    LabHarness lab(SerialPolicy::UncheckedModel); // even the permissive model applies nothing
    QVERIFY(lab.start());
    const auto im = lab.client().createInputMethod();
    QVERIFY(im);
    const auto a = lab.server().addField(fieldAtEnd("one tw"));
    const auto b = lab.server().addField(fieldAtEnd("three"));
    QVERIFY(a && b);
    QVERIFY(lab.roundtrip());
    QVERIFY(lab.server().focus(*a));
    QVERIFY(lab.roundtrip());
    QVERIFY(lab.client().commitString(*im, "orphan")); // pending on the server
    QVERIFY(lab.roundtrip());

    lab.disconnectClient();
    QVERIFY(!lab.hasClient());
    QVERIFY(!lab.server().hasInputMethod());
    QCOMPARE(lab.server().imObjectCount(), std::size_t(0));
    QVERIFY(!lab.roundtrip()); // no client: bounded failure, no hang

    QVERIFY(lab.server().focus(*b)); // compositor side keeps working without an IM
    QVERIFY(!lab.server().active());
    QCOMPARE(lab.server().doneCount(), 0u);
    QCOMPARE(bytes(lab.server().field(*a).text), QByteArray("one tw"));
    QCOMPARE(bytes(lab.server().field(*b).text), QByteArray("three"));
    QCOMPARE(lab.server().stats().applied, 0u);
}

QTEST_GUILESS_MAIN(InputLabTest)
#include "tst_input_lab.moc"
