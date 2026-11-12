#include "decisionrouter.h"

namespace rasklad::routing {

namespace {

bool isValidDecisionAction(DecisionAction action)
{
    switch (action) {
    case DecisionAction::Keep:
    case DecisionAction::Correct:
    case DecisionAction::Abstain:
        return true;
    }
    return false;
}

RouteDecision decision(RouteAction action, RouteReason reason, bool queried)
{
    RouteDecision d;
    d.action = action;
    d.reason = reason;
    d.evidenceQueried = queried;
    return d;
}

} // namespace

TwoScoreThresholds frozenTwoScore()
{
    return TwoScoreThresholds{true, kFrozenTwoScoreMinAlternative, kFrozenTwoScoreMaxObserved, kFrozenTwoScoreGap};
}

bool sharedAmbiguous(const TokenEvidence &evidence)
{
    return evidence.stem.both() || evidence.control.both() || evidence.full.both();
}

bool isValidFieldContext(FieldContext field)
{
    switch (field) {
    case FieldContext::Unknown:
    case FieldContext::Prose:
    case FieldContext::Protected:
        return true;
    }
    return false;
}

bool isValidRouteProfile(RouteProfile profile)
{
    switch (profile) {
    case RouteProfile::SafeConfirmed:
    case RouteProfile::RequestedHybrid:
        return true;
    }
    return false;
}

std::optional<SourceLayout> sourceLayoutOf(InputLayout layout)
{
    switch (layout) {
    case InputLayout::KnownUs:
        return SourceLayout::Us;
    case InputLayout::KnownRu:
        return SourceLayout::Ru;
    case InputLayout::Unknown:
        break;
    }
    return std::nullopt;
}

LayoutDirection correctionDirection(SourceLayout typedLayout)
{
    return typedLayout == SourceLayout::Ru ? LayoutDirection::RuToUs : LayoutDirection::UsToRu;
}

RouteDecision route(const RouteRequest &request, RouteProfile profile, const EvidenceSource *source)
{
    // 1. Field context. Protected first: protected text is kept even when
    // other metadata is missing. Anything but Prose (including values
    // outside the enum) is unknown.
    if (request.field == FieldContext::Protected)
        return decision(RouteAction::Keep, RouteReason::ProtectedField, false);
    if (request.field != FieldContext::Prose)
        return decision(RouteAction::Abstain, RouteReason::UnknownContext, false);

    // 2. Authoritative layout and a known profile.
    const std::optional<SourceLayout> layout = sourceLayoutOf(request.layout);
    if (!layout)
        return decision(RouteAction::Abstain, RouteReason::UnknownContext, false);
    if (!isValidRouteProfile(profile))
        return decision(RouteAction::Abstain, RouteReason::InvalidRequest, false);

    // 3. Canonical keys 4..32. canonicalizeToken is bounded: it rejects more
    // than 32 UTF-16 units before looking at characters.
    const CanonicalToken canonical = canonicalizeToken(request.token, *layout);
    switch (canonical.status) {
    case CanonicalStatus::Ok:
        break;
    case CanonicalStatus::TooLong:
        return decision(RouteAction::Abstain, RouteReason::TooLong, false);
    case CanonicalStatus::Empty:
    case CanonicalStatus::MixedScript:
    case CanonicalStatus::UnsupportedCharacter:
    case CanonicalStatus::InvalidLayout:
        return decision(RouteAction::Abstain, RouteReason::UnsupportedToken, false);
    }
    const qsizetype keys = canonical.keys.size();
    if (canonical.status != CanonicalStatus::Ok || keys < 1)
        return decision(RouteAction::Abstain, RouteReason::UnsupportedToken, false);
    if (keys < kMinRouteKeys)
        return decision(RouteAction::Abstain, RouteReason::TooShort, false);
    if (keys > kMaxRouteKeys)
        return decision(RouteAction::Abstain, RouteReason::TooLong, false);

    // 4. Evidence, only now.
    if (!source)
        return decision(RouteAction::Abstain, RouteReason::EvidenceUnavailable, false);
    EvidenceQuery query;
    query.token = request.token;
    query.layout = *layout;
    query.keys = canonical.keys;
    const std::optional<TokenEvidence> evidence = source->evidence(query);
    if (!evidence)
        return decision(RouteAction::Abstain, RouteReason::EvidenceUnavailable, true);
    if (!isValidDecisionAction(evidence->model))
        return decision(RouteAction::Abstain, RouteReason::InvalidEvidence, true);

    // 5. The typed word is a word of its own language: keep it.
    if (evidence->full.observed)
        return decision(RouteAction::Keep, RouteReason::ObservedWordKnown, true);
    // 6. Shared ambiguity: no change.
    if (sharedAmbiguous(*evidence))
        return decision(RouteAction::Abstain, RouteReason::SharedAmbiguity, true);

    // 7. Candidate logic.
    const bool dictionary = evidence->full.alternative;
    const bool model = evidence->model == DecisionAction::Correct;
    RouteDecision out;
    out.evidenceQueried = true;
    if (dictionary && model) {
        out.action = RouteAction::Correct;
        out.reason = RouteReason::DictionaryAndModelAgree;
    } else if (dictionary) {
        out.action = profile == RouteProfile::RequestedHybrid ? RouteAction::Suggest : RouteAction::Abstain;
        out.reason = RouteReason::DictionaryOnly;
    } else if (model) {
        out.action = profile == RouteProfile::RequestedHybrid ? RouteAction::Suggest : RouteAction::Abstain;
        out.reason = RouteReason::ModelOnly;
    } else {
        out.action = RouteAction::Abstain;
        out.reason = RouteReason::NoEvidence;
    }
    if (out.action == RouteAction::Correct || out.action == RouteAction::Suggest)
        out.candidate = convertLayout(request.token.toString(), correctionDirection(*layout));
    return out;
}

const char *routeActionName(RouteAction action)
{
    switch (action) {
    case RouteAction::Keep:
        return "keep";
    case RouteAction::Correct:
        return "correct";
    case RouteAction::Suggest:
        return "suggest";
    case RouteAction::Abstain:
        return "abstain";
    }
    return "abstain";
}

const char *routeReasonName(RouteReason reason)
{
    switch (reason) {
    case RouteReason::ProtectedField:
        return "protectedField";
    case RouteReason::UnknownContext:
        return "unknownContext";
    case RouteReason::InvalidRequest:
        return "invalidRequest";
    case RouteReason::UnsupportedToken:
        return "unsupportedToken";
    case RouteReason::TooShort:
        return "tooShort";
    case RouteReason::TooLong:
        return "tooLong";
    case RouteReason::EvidenceUnavailable:
        return "evidenceUnavailable";
    case RouteReason::InvalidEvidence:
        return "invalidEvidence";
    case RouteReason::ObservedWordKnown:
        return "observedWordKnown";
    case RouteReason::SharedAmbiguity:
        return "sharedAmbiguity";
    case RouteReason::DictionaryAndModelAgree:
        return "dictionaryAndModelAgree";
    case RouteReason::DictionaryOnly:
        return "dictionaryOnly";
    case RouteReason::ModelOnly:
        return "modelOnly";
    case RouteReason::NoEvidence:
        return "noEvidence";
    }
    return "unknownContext";
}

} // namespace rasklad::routing
