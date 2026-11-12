// SYS-239: tests of the production DecisionRouter (src/decisionrouter.h).
// Synthetic tokens only; the research router (tools/routerpolicy) is the
// reference for exact parity.

#include "decisionrouter.h"
#include "layoutconverter.h"
#include "routerpolicy.h"

#include <QString>
#include <QTest>

#include <array>
#include <optional>
#include <vector>

using namespace rasklad;
using namespace rasklad::routing;

namespace {

class CountingSource final : public EvidenceSource
{
public:
    explicit CountingSource(std::optional<TokenEvidence> evidence)
        : m_evidence(evidence)
    {
    }

    [[nodiscard]] std::optional<TokenEvidence> evidence(const EvidenceQuery &query) const override
    {
        ++calls;
        lastKeys = query.keys;
        lastLayout = query.layout;
        return m_evidence;
    }

    mutable int calls = 0;
    mutable KeySequence lastKeys;
    mutable SourceLayout lastLayout = SourceLayout::Us;

private:
    std::optional<TokenEvidence> m_evidence;
};

TokenEvidence makeEvidence(int bits, DecisionAction model)
{
    TokenEvidence e;
    e.full.observed = bits & 1;
    e.full.alternative = bits & 2;
    e.control.observed = bits & 4;
    e.control.alternative = bits & 8;
    e.stem.observed = bits & 16;
    e.stem.alternative = bits & 32;
    e.model = model;
    return e;
}

RouteRequest request(FieldContext field, InputLayout layout, const QString &token)
{
    RouteRequest r;
    r.field = field;
    r.layout = layout;
    r.token = token;
    return r;
}

context::PolicyAction toPolicy(RouteAction action)
{
    switch (action) {
    case RouteAction::Keep:
        return context::PolicyAction::Keep;
    case RouteAction::Correct:
        return context::PolicyAction::Correct;
    case RouteAction::Suggest:
        return context::PolicyAction::Suggest;
    case RouteAction::Abstain:
        return context::PolicyAction::Abstain;
    }
    return context::PolicyAction::Abstain;
}

context::PolicyAction modelToPolicy(DecisionAction action)
{
    switch (action) {
    case DecisionAction::Keep:
        return context::PolicyAction::Keep;
    case DecisionAction::Correct:
        return context::PolicyAction::Correct;
    case DecisionAction::Abstain:
        return context::PolicyAction::Abstain;
    }
    return context::PolicyAction::Abstain;
}

QString repeated(QChar ch, int count)
{
    return QString(count, ch);
}

std::vector<QString> parityTokens()
{
    return {
        QStringLiteral("ghbdtn"),
        QStringLiteral("Ghbdtn"),
        QStringLiteral("hello"),
        QStringLiteral("k.,jdm"),
        QString::fromUtf8("привет"),
        QString::fromUtf8("Мир!"),
        QStringLiteral("abc"),
        QString::fromUtf8("абв"),
        QStringLiteral("ab1d"),
        QString::fromUtf8("abвг"),
        QString(),
        QString(QChar(0xFFFD)),
        repeated(QLatin1Char('a'), 32),
        repeated(QLatin1Char('a'), 33),
        repeated(QChar(0x0444), 32),
    };
}

} // namespace

class DecisionTests : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void guardsMatchFrozenResearch()
    {
        QCOMPARE(kMinRouteKeys, router::kMinRouterKeys);
        QCOMPARE(kMaxRouteKeys, kMaxTokenKeys);
        const TwoScoreThresholds t = frozenTwoScore();
        QVERIFY(t.enabled);
        QCOMPARE(t.minAlternative, -13.0);
        QCOMPARE(t.maxObserved, -9.0);
        QCOMPARE(t.gap, 2.5);
    }

    void protectedAndUnknownNeverReachEvidence()
    {
        CountingSource source(makeEvidence(2, DecisionAction::Correct));
        const QString token = QStringLiteral("ghbdtn");
        for (const RouteProfile profile : {RouteProfile::SafeConfirmed, RouteProfile::RequestedHybrid}) {
            RouteDecision d = route(request(FieldContext::Protected, InputLayout::KnownUs, token), profile, &source);
            QCOMPARE(d.action, RouteAction::Keep);
            QCOMPARE(d.reason, RouteReason::ProtectedField);
            QVERIFY(!d.evidenceQueried);
            // Protected wins even without layout metadata.
            d = route(request(FieldContext::Protected, InputLayout::Unknown, token), profile, &source);
            QCOMPARE(d.action, RouteAction::Keep);
            d = route(request(FieldContext::Unknown, InputLayout::KnownUs, token), profile, &source);
            QCOMPARE(d.action, RouteAction::Abstain);
            QCOMPARE(d.reason, RouteReason::UnknownContext);
            d = route(request(FieldContext(42), InputLayout::KnownUs, token), profile, &source);
            QCOMPARE(d.action, RouteAction::Abstain);
            QCOMPARE(d.reason, RouteReason::UnknownContext);
            d = route(request(FieldContext::Prose, InputLayout::Unknown, token), profile, &source);
            QCOMPARE(d.action, RouteAction::Abstain);
            QCOMPARE(d.reason, RouteReason::UnknownContext);
            d = route(request(FieldContext::Prose, InputLayout(9), token), profile, &source);
            QCOMPARE(d.reason, RouteReason::UnknownContext);
        }
        const RouteDecision invalidProfile =
            route(request(FieldContext::Prose, InputLayout::KnownUs, token), RouteProfile(7), &source);
        QCOMPARE(invalidProfile.action, RouteAction::Abstain);
        QCOMPARE(invalidProfile.reason, RouteReason::InvalidRequest);
        QCOMPARE(source.calls, 0);
    }

    void tokenGatesBeforeEvidence_data()
    {
        QTest::addColumn<QString>("token");
        QTest::addColumn<int>("layout");
        QTest::addColumn<int>("reason");
        QTest::newRow("short-us") << QStringLiteral("abc") << int(InputLayout::KnownUs) << int(RouteReason::TooShort);
        QTest::newRow("short-ru") << QString::fromUtf8("абв") << int(InputLayout::KnownRu) << int(RouteReason::TooShort);
        QTest::newRow("long-33") << repeated(QLatin1Char('a'), 33) << int(InputLayout::KnownUs)
                                 << int(RouteReason::TooLong);
        QTest::newRow("huge") << repeated(QLatin1Char('a'), 65536) << int(InputLayout::KnownUs)
                              << int(RouteReason::TooLong);
        QTest::newRow("digit") << QStringLiteral("ab1d") << int(InputLayout::KnownUs)
                               << int(RouteReason::UnsupportedToken);
        QTest::newRow("mixed") << QString::fromUtf8("abвг") << int(InputLayout::KnownUs)
                               << int(RouteReason::UnsupportedToken);
        QTest::newRow("cyrillic-in-us") << QString::fromUtf8("привет") << int(InputLayout::KnownUs)
                                        << int(RouteReason::UnsupportedToken);
        QTest::newRow("latin-in-ru") << QStringLiteral("hello") << int(InputLayout::KnownRu)
                                     << int(RouteReason::UnsupportedToken);
        QTest::newRow("empty") << QString() << int(InputLayout::KnownUs) << int(RouteReason::UnsupportedToken);
        QTest::newRow("sentinel") << QString(QChar(0xFFFD)) << int(InputLayout::KnownUs)
                                  << int(RouteReason::UnsupportedToken);
        QTest::newRow("emoji") << QString::fromUtf8("ab😀cd") << int(InputLayout::KnownUs)
                               << int(RouteReason::UnsupportedToken);
        QTest::newRow("space") << QStringLiteral("ab cd") << int(InputLayout::KnownUs)
                               << int(RouteReason::UnsupportedToken);
    }

    void tokenGatesBeforeEvidence()
    {
        QFETCH(QString, token);
        QFETCH(int, layout);
        QFETCH(int, reason);
        CountingSource source(makeEvidence(2, DecisionAction::Correct));
        for (const RouteProfile profile : {RouteProfile::SafeConfirmed, RouteProfile::RequestedHybrid}) {
            const RouteDecision d = route(request(FieldContext::Prose, InputLayout(layout), token), profile, &source);
            QCOMPARE(d.action, RouteAction::Abstain);
            QCOMPARE(int(d.reason), reason);
            QVERIFY(!d.evidenceQueried);
            QVERIFY(d.candidate.isEmpty());
        }
        QCOMPARE(source.calls, 0);
    }

    void eligibleBoundsQueryWithCanonicalKeys()
    {
        for (const int length : {4, 32}) {
            CountingSource source(makeEvidence(0, DecisionAction::Abstain));
            const QString token = repeated(QLatin1Char('q'), length);
            const RouteDecision d =
                route(request(FieldContext::Prose, InputLayout::KnownUs, token), RouteProfile::SafeConfirmed, &source);
            QCOMPARE(source.calls, 1);
            QVERIFY(d.evidenceQueried);
            QCOMPARE(source.lastKeys.size(), qsizetype(length));
            QCOMPARE(source.lastLayout, SourceLayout::Us);
            QCOMPARE(d.reason, RouteReason::NoEvidence);
        }
        CountingSource ru(makeEvidence(0, DecisionAction::Abstain));
        static_cast<void>(route(request(FieldContext::Prose, InputLayout::KnownRu, QString::fromUtf8("привет")),
                                RouteProfile::SafeConfirmed, &ru));
        QCOMPARE(ru.calls, 1);
        QCOMPARE(ru.lastLayout, SourceLayout::Ru);
        QCOMPARE(ru.lastKeys, canonicalizeToken(QStringLiteral("ghbdtn"), SourceLayout::Us).keys);
    }

    void decisionTable_data()
    {
        QTest::addColumn<int>("bits");
        QTest::addColumn<int>("model");
        QTest::addColumn<int>("safeAction");
        QTest::addColumn<int>("hybridAction");
        QTest::addColumn<int>("reason");
        const int K = int(DecisionAction::Keep);
        const int C = int(DecisionAction::Correct);
        const int A = int(DecisionAction::Abstain);
        // bits: 1 full.obs, 2 full.alt, 4 ctrl.obs, 8 ctrl.alt, 16 stem.obs, 32 stem.alt
        QTest::newRow("observed-keeps-even-with-alt-and-model") << (1 | 2) << C << int(RouteAction::Keep)
                                                                << int(RouteAction::Keep)
                                                                << int(RouteReason::ObservedWordKnown);
        QTest::newRow("observed-keeps") << 1 << A << int(RouteAction::Keep) << int(RouteAction::Keep)
                                        << int(RouteReason::ObservedWordKnown);
        QTest::newRow("stem-both") << (2 | 16 | 32) << C << int(RouteAction::Abstain) << int(RouteAction::Abstain)
                                   << int(RouteReason::SharedAmbiguity);
        QTest::newRow("control-both") << (2 | 4 | 8) << C << int(RouteAction::Abstain) << int(RouteAction::Abstain)
                                      << int(RouteReason::SharedAmbiguity);
        QTest::newRow("dictionary-and-model") << (2 | 8 | 32) << C << int(RouteAction::Correct)
                                              << int(RouteAction::Correct)
                                              << int(RouteReason::DictionaryAndModelAgree);
        QTest::newRow("dictionary-only-model-keep") << 2 << K << int(RouteAction::Abstain) << int(RouteAction::Suggest)
                                                    << int(RouteReason::DictionaryOnly);
        QTest::newRow("dictionary-only-model-abstain") << 2 << A << int(RouteAction::Abstain)
                                                       << int(RouteAction::Suggest) << int(RouteReason::DictionaryOnly);
        QTest::newRow("model-only") << 0 << C << int(RouteAction::Abstain) << int(RouteAction::Suggest)
                                    << int(RouteReason::ModelOnly);
        QTest::newRow("model-only-control-alt") << (8 | 32) << C << int(RouteAction::Abstain)
                                                << int(RouteAction::Suggest) << int(RouteReason::ModelOnly);
        QTest::newRow("nothing") << 0 << K << int(RouteAction::Abstain) << int(RouteAction::Abstain)
                                 << int(RouteReason::NoEvidence);
    }

    void decisionTable()
    {
        QFETCH(int, bits);
        QFETCH(int, model);
        QFETCH(int, safeAction);
        QFETCH(int, hybridAction);
        QFETCH(int, reason);
        CountingSource source(makeEvidence(bits, DecisionAction(model)));
        const QString token = QStringLiteral("Ghbdtn");
        const RouteDecision safe =
            route(request(FieldContext::Prose, InputLayout::KnownUs, token), RouteProfile::SafeConfirmed, &source);
        const RouteDecision hybrid =
            route(request(FieldContext::Prose, InputLayout::KnownUs, token), RouteProfile::RequestedHybrid, &source);
        QCOMPARE(int(safe.action), safeAction);
        QCOMPARE(int(hybrid.action), hybridAction);
        QCOMPARE(int(safe.reason), reason);
        QCOMPARE(int(hybrid.reason), reason);
        const QString expected = QString::fromUtf8("Привет");
        QCOMPARE(safe.candidate, safe.action == RouteAction::Correct ? expected : QString());
        const bool hybridCandidate = hybrid.action == RouteAction::Correct || hybrid.action == RouteAction::Suggest;
        QCOMPARE(hybrid.candidate, hybridCandidate ? expected : QString());
    }

    void evidenceFailures()
    {
        const QString token = QStringLiteral("ghbdtn");
        RouteDecision d = route(request(FieldContext::Prose, InputLayout::KnownUs, token), RouteProfile::SafeConfirmed,
                                nullptr);
        QCOMPARE(d.reason, RouteReason::EvidenceUnavailable);
        QVERIFY(!d.evidenceQueried);
        CountingSource none(std::nullopt);
        d = route(request(FieldContext::Prose, InputLayout::KnownUs, token), RouteProfile::RequestedHybrid, &none);
        QCOMPARE(d.action, RouteAction::Abstain);
        QCOMPARE(d.reason, RouteReason::EvidenceUnavailable);
        QVERIFY(d.evidenceQueried);
        CountingSource invalid(makeEvidence(2, DecisionAction(7)));
        d = route(request(FieldContext::Prose, InputLayout::KnownUs, token), RouteProfile::RequestedHybrid, &invalid);
        QCOMPARE(d.action, RouteAction::Abstain);
        QCOMPARE(d.reason, RouteReason::InvalidEvidence);
    }

    void ruLayoutCandidate()
    {
        CountingSource source(makeEvidence(2, DecisionAction::Correct));
        const RouteDecision d = route(request(FieldContext::Prose, InputLayout::KnownRu, QString::fromUtf8("Руддщ")),
                                      RouteProfile::SafeConfirmed, &source);
        QCOMPARE(d.action, RouteAction::Correct);
        QCOMPARE(d.candidate, QStringLiteral("Hello"));
    }

    // Exact parity with tools/routerpolicy decideSafeConfirmed/decideHybrid,
    // including that evidence is read exactly when the evidence-free part of
    // the research common gate (prose field, supported 4..32-key target) passes.
    void parityWithResearchRouter()
    {
        const std::array<std::pair<FieldContext, context::FieldKind>, 4> fields{{
            {FieldContext::Prose, context::FieldKind::Prose},
            {FieldContext::Protected, context::FieldKind::Protected},
            {FieldContext::Unknown, context::FieldKind::Unknown},
            {FieldContext(42), context::FieldKind(42)},
        }};
        const std::array<std::pair<InputLayout, SourceLayout>, 4> layouts{{
            {InputLayout::KnownUs, SourceLayout::Us},
            {InputLayout::KnownRu, SourceLayout::Ru},
            {InputLayout::Unknown, SourceLayout(7)},
            {InputLayout(9), SourceLayout(8)},
        }};
        const std::array<DecisionAction, 3> models{{DecisionAction::Keep, DecisionAction::Correct,
                                                    DecisionAction::Abstain}};
        const std::vector<QString> tokens = parityTokens();
        int compared = 0;
        int queried = 0;
        for (const auto &[field, kind] : fields) {
            for (const auto &[inputLayout, sourceLayout] : layouts) {
                for (std::size_t t = 0; t < tokens.size(); ++t) {
                    for (int bits = 0; bits < 64; ++bits) {
                        for (const DecisionAction model : models) {
                            const TokenEvidence evidence = makeEvidence(bits, model);
                            context::FeatureRow row;
                            row.layout = sourceLayout;
                            row.target = tokens[t];
                            row.field = kind;
                            row.fullObserved = evidence.full.observed;
                            row.fullAlternative = evidence.full.alternative;
                            row.sharedAmbiguous = sharedAmbiguous(evidence);
                            row.tokenModelAction = modelToPolicy(model);
                            // Research gates that need no evidence: prose field and a
                            // supported 4..32-key target. Only then may evidence be read.
                            const bool gatePasses = kind == context::FieldKind::Prose
                                && router::isRouterTarget(tokens[t], sourceLayout);
                            if (gatePasses) {
                                // The rest of the research gate is evidence-based.
                                const bool researchGate = !router::commonRouterGate(row).has_value();
                                QVERIFY(researchGate == (!row.fullObserved && !row.sharedAmbiguous));
                            }

                            const RouteRequest req = request(field, inputLayout, tokens[t]);
                            CountingSource safeSource(evidence);
                            const RouteDecision safe = route(req, RouteProfile::SafeConfirmed, &safeSource);
                            CountingSource hybridSource(evidence);
                            const RouteDecision hybrid = route(req, RouteProfile::RequestedHybrid, &hybridSource);

                            const QByteArray where = QByteArray("field ") + QByteArray::number(int(kind))
                                + " layout " + QByteArray::number(int(sourceLayout)) + " token "
                                + QByteArray::number(qulonglong(t)) + " bits " + QByteArray::number(bits)
                                + " model " + QByteArray::number(int(model));
                            QVERIFY2(toPolicy(safe.action) == router::decideSafeConfirmed(row), where.constData());
                            QVERIFY2(toPolicy(hybrid.action) == router::decideHybrid(row), where.constData());
                            QVERIFY2(safe.evidenceQueried == gatePasses, where.constData());
                            QVERIFY2(safeSource.calls == (gatePasses ? 1 : 0), where.constData());
                            QVERIFY2(hybridSource.calls == (gatePasses ? 1 : 0), where.constData());
                            if (hybrid.action == RouteAction::Correct || hybrid.action == RouteAction::Suggest) {
                                QVERIFY2(hybrid.candidate
                                             == convertLayout(tokens[t], correctionDirection(sourceLayout)),
                                         where.constData());
                            }
                            queried += gatePasses ? 1 : 0;
                            ++compared;
                        }
                    }
                }
            }
        }
        QCOMPARE(compared, 4 * 4 * int(tokens.size()) * 64 * 3);
        QVERIFY(queried > 0);
    }

    void fixedEvidenceIsDeterministic()
    {
        const FixedEvidence fixed(makeEvidence(2, DecisionAction::Correct));
        const RouteDecision a =
            route(request(FieldContext::Prose, InputLayout::KnownUs, QStringLiteral("ghbdtn")),
                  RouteProfile::SafeConfirmed, &fixed);
        QCOMPARE(a.action, RouteAction::Correct);
        QCOMPARE(QByteArray(routeActionName(a.action)), QByteArray("correct"));
        QCOMPARE(QByteArray(routeReasonName(a.reason)), QByteArray("dictionaryAndModelAgree"));
    }
};

QTEST_GUILESS_MAIN(DecisionTests)
#include "decision_tests.moc"
