#pragma once

// Production DecisionRouter (SYS-239, ADR-001).
//
// Pure decision table for one token. It performs no I/O, logging, network,
// process, timer or keystroke operations and keeps no global or mutable
// state. Dictionaries and the n-gram model are reached only through the
// EvidenceSource interface, and only after the context and token gates have
// passed: protected, unknown or unsupported input never reaches a provider.
//
// Semantics are the frozen experiment-006 router (tools/routerpolicy.cpp):
//   safeConfirmed    — the automatic profile accepted by ADR-001;
//   requestedHybrid  — the explicit-request profile ("hybrid"); it may return
//                      Suggest. Suggest is never an automatic edit.
// Regression tests (tests/decision) compare both profiles with the research
// functions for every combination of gate, membership, ambiguity and model
// action, including invalid metadata.
//
// A decision is advice. Even Correct must be shown as a preview and applied
// by an explicit user action in the manual-preview MVP; this module never
// changes text by itself.

#include "layoutconverter.h"
#include "ngramdetector.h"

#include <QString>
#include <QStringView>

#include <optional>

namespace rasklad::routing {

// Context of the text field, supplied by an input adapter. Only Prose
// permits evidence lookup. Unknown is the default: missing metadata is never
// replaced by a guess.
enum class FieldContext { Unknown, Prose, Protected };

// Authoritative layout the token was typed in. Unknown (or any value outside
// the enum) stops routing before evidence access.
enum class InputLayout { Unknown, KnownUs, KnownRu };

enum class RouteProfile { SafeConfirmed, RequestedHybrid };

// Only Correct is a candidate for an automatic edit (and the manual preview
// still requires Apply). Suggest is a proposal; Keep and Abstain change nothing.
enum class RouteAction { Keep, Correct, Suggest, Abstain };

enum class RouteReason {
    // Gates, before any evidence access.
    ProtectedField,
    UnknownContext,
    InvalidRequest,
    UnsupportedToken,
    TooShort,
    TooLong,
    // Evidence availability.
    EvidenceUnavailable,
    InvalidEvidence,
    // Decision table.
    ObservedWordKnown,
    SharedAmbiguity,
    DictionaryAndModelAgree,
    DictionaryOnly,
    ModelOnly,
    NoEvidence,
};

// Canonical key guard of the router: 4..32 keys.
inline constexpr int kMinRouteKeys = 4;
inline constexpr int kMaxRouteKeys = kMaxTokenKeys;
static_assert(kMinRouteKeys == kMinDecisionKeys, "router guard must equal the frozen token decision guard");

// Frozen twoScore thresholds of experiment 002 (minAlternative -13,
// maxObserved -9, gap 2.5). They are comparative n-gram scores, not
// probabilities or confidence percentages.
inline constexpr double kFrozenTwoScoreMinAlternative = -13.0;
inline constexpr double kFrozenTwoScoreMaxObserved = -9.0;
inline constexpr double kFrozenTwoScoreGap = 2.5;
[[nodiscard]] TwoScoreThresholds frozenTwoScore();

struct PairHit {
    // Typed text in the dictionary of the typed layout's language.
    bool observed = false;
    // Converted text in the dictionary of the other language.
    bool alternative = false;
    [[nodiscard]] bool both() const { return observed && alternative; }
};

// Evidence for one eligible token: full and control (no PFX/SFX) Hunspell
// pairs, the legacy stem-index pair on canonical keys and the frozen twoScore
// action of the pinned model.
struct TokenEvidence {
    PairHit full;
    PairHit control;
    PairHit stem;
    DecisionAction model = DecisionAction::Abstain;
};

// stemBoth OR controlBoth OR fullBoth (ADR-001 shared ambiguity guard).
[[nodiscard]] bool sharedAmbiguous(const TokenEvidence &evidence);

struct EvidenceQuery {
    // Exact typed text (case preserved): Hunspell is case-sensitive.
    QStringView token;
    SourceLayout layout = SourceLayout::Us;
    // canonicalizeToken(token, layout).keys, 4..32 keys.
    KeySequence keys;
};

class EvidenceSource
{
public:
    virtual ~EvidenceSource() = default;
    // nullopt: evidence unavailable (the router abstains). Implementations
    // must not store or log the token.
    [[nodiscard]] virtual std::optional<TokenEvidence> evidence(const EvidenceQuery &query) const = 0;
};

struct RouteRequest {
    FieldContext field = FieldContext::Unknown;
    InputLayout layout = InputLayout::Unknown;
    QStringView token;
};

struct RouteDecision {
    RouteAction action = RouteAction::Abstain;
    RouteReason reason = RouteReason::UnknownContext;
    // True only if the evidence source was called.
    bool evidenceQueried = false;
    // For Correct and Suggest: convertLayout(token, correction direction),
    // case preserved. Empty otherwise.
    QString candidate;
};

[[nodiscard]] bool isValidFieldContext(FieldContext field);
[[nodiscard]] bool isValidRouteProfile(RouteProfile profile);
// KnownUs -> Us, KnownRu -> Ru; Unknown and invalid values -> nullopt.
[[nodiscard]] std::optional<SourceLayout> sourceLayoutOf(InputLayout layout);
// Us -> UsToRu, Ru -> RuToUs. Precondition: isValidSourceLayout.
[[nodiscard]] LayoutDirection correctionDirection(SourceLayout typedLayout);

// Order (mandatory):
//   1. field Protected -> Keep/ProtectedField; field not Prose -> Abstain/UnknownContext;
//   2. layout not KnownUs/KnownRu -> Abstain/UnknownContext;
//      profile outside the enum -> Abstain/InvalidRequest;
//   3. canonicalizeToken not Ok -> Abstain (TooLong/UnsupportedToken), keys < 4 -> TooShort;
//   4. no source or nullopt evidence -> Abstain/EvidenceUnavailable;
//      model action outside the enum -> Abstain/InvalidEvidence;
//   5. full.observed -> Keep/ObservedWordKnown (regardless of the other word and model);
//   6. sharedAmbiguous -> Abstain/SharedAmbiguity;
//   7. safeConfirmed: full.alternative && model Correct -> Correct, otherwise Abstain;
//      requestedHybrid: as research hybrid — dictionary+model -> Correct,
//      dictionary only or model only -> Suggest, neither -> Abstain.
[[nodiscard]] RouteDecision route(const RouteRequest &request, RouteProfile profile, const EvidenceSource *source);

// Evaluate one fixed evidence value (for computing both profiles from one lookup).
class FixedEvidence final : public EvidenceSource
{
public:
    explicit FixedEvidence(std::optional<TokenEvidence> evidence)
        : m_evidence(evidence)
    {
    }
    [[nodiscard]] std::optional<TokenEvidence> evidence(const EvidenceQuery &) const override { return m_evidence; }

private:
    std::optional<TokenEvidence> m_evidence;
};

// Stable ASCII names for UI mapping and tests (no user text).
[[nodiscard]] const char *routeActionName(RouteAction action);
[[nodiscard]] const char *routeReasonName(RouteReason reason);

} // namespace rasklad::routing
