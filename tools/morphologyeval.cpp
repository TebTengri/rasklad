#include "morphologyeval.h"

#include "layoutconverter.h"

namespace rasklad::morphology {

using benchmark::CaseFeatures;
using benchmark::Method;

namespace {

CaseFeatures withLexicon(const CaseFeatures &base, const PairMembership &pair)
{
    CaseFeatures f = base;
    f.observedFull = pair.observed;
    f.alternativeFull = pair.alternative;
    return f;
}

} // namespace

const char *morphMethodName(MorphMethod method)
{
    switch (method) {
    case MorphMethod::AlwaysKeep:
        return "alwaysKeep";
    case MorphMethod::StemExact:
        return "stemExact";
    case MorphMethod::TwoScore:
        return "twoScore";
    case MorphMethod::StemThenModel:
        return "stemThenModel";
    case MorphMethod::ModelConfirmedStem:
        return "modelConfirmedStem";
    case MorphMethod::HunspellNoPfxSfx:
        return "hunspellNoPfxSfx";
    case MorphMethod::HunspellFull:
        return "hunspellFull";
    case MorphMethod::MorphThenModel:
        return "morphThenModel";
    case MorphMethod::ModelConfirmedMorph:
        return "modelConfirmedMorph";
    }
    return "unknown";
}

DecisionAction decideMorph(MorphMethod method, const MorphFeatures &f)
{
    switch (method) {
    case MorphMethod::AlwaysKeep:
        return benchmark::decideMethod(Method::AlwaysKeep, f.base);
    case MorphMethod::StemExact:
        return benchmark::decideMethod(Method::ExactFull, f.base);
    case MorphMethod::TwoScore:
        return benchmark::decideMethod(Method::TwoScore, f.base);
    case MorphMethod::StemThenModel:
        return benchmark::decideMethod(Method::ExactFullThenTwoScore, f.base);
    case MorphMethod::ModelConfirmedStem:
        return benchmark::decideMethod(Method::TwoScoreConfirmedByExactFull, f.base);
    case MorphMethod::HunspellNoPfxSfx:
        return benchmark::decideMethod(Method::ExactFull, withLexicon(f.base, f.control));
    case MorphMethod::HunspellFull:
        return benchmark::decideMethod(Method::ExactFull, withLexicon(f.base, f.full));
    case MorphMethod::MorphThenModel:
        return benchmark::decideMethod(Method::ExactFullThenTwoScore, withLexicon(f.base, f.full));
    case MorphMethod::ModelConfirmedMorph:
        return benchmark::decideMethod(Method::TwoScoreConfirmedByExactFull, withLexicon(f.base, f.full));
    }
    return DecisionAction::Abstain;
}

bool stemAmbiguous(const MorphFeatures &f)
{
    return f.base.observedFull && f.base.alternativeFull;
}

bool controlAmbiguous(const MorphFeatures &f)
{
    return f.control.observed && f.control.alternative;
}

bool fullAmbiguous(const MorphFeatures &f)
{
    return f.full.observed && f.full.alternative;
}

bool sharedAmbiguous(const MorphFeatures &f)
{
    return stemAmbiguous(f) || controlAmbiguous(f) || fullAmbiguous(f);
}

benchmark::Membership pairMembership(bool supported, const PairMembership &pair)
{
    using benchmark::Membership;
    if (!supported)
        return Membership::Unsupported;
    if (pair.observed && pair.alternative)
        return Membership::Both;
    if (pair.observed)
        return Membership::ObservedOnly;
    if (pair.alternative)
        return Membership::AlternativeOnly;
    return Membership::Neither;
}

PairMembership queryPair(const SpellPair &spell, QStringView text, SourceLayout layout, const CaseFeatures &base)
{
    PairMembership pair;
    if (!spell.isValid() || !isValidSourceLayout(layout) || !benchmark::isDecisionEligible(base))
        return pair;
    const QString observedText = text.toString();
    const QString alternativeText = convertLayout(observedText, benchmark::correctionDirection(layout));
    pair.observed = spell.engines[std::size_t(observedLanguage(layout))]->accepts(observedText);
    pair.alternative = spell.engines[std::size_t(alternativeLanguage(layout))]->accepts(alternativeText);
    return pair;
}

DecisionAction decideMorphLive(MorphMethod method, QStringView text, SourceLayout layout,
                               const benchmark::FeatureContext &context, const SpellPair &control,
                               const SpellPair &full)
{
    switch (method) {
    case MorphMethod::AlwaysKeep:
        return benchmark::decideLive(Method::AlwaysKeep, text, layout, context);
    case MorphMethod::StemExact:
        return benchmark::decideLive(Method::ExactFull, text, layout, context);
    case MorphMethod::TwoScore:
        return benchmark::decideLive(Method::TwoScore, text, layout, context);
    case MorphMethod::StemThenModel:
        return benchmark::decideLive(Method::ExactFullThenTwoScore, text, layout, context);
    case MorphMethod::ModelConfirmedStem:
        return benchmark::decideLive(Method::TwoScoreConfirmedByExactFull, text, layout, context);
    case MorphMethod::HunspellNoPfxSfx:
    case MorphMethod::HunspellFull:
    case MorphMethod::MorphThenModel:
    case MorphMethod::ModelConfirmedMorph:
        break;
    }
    const SpellPair &spell = method == MorphMethod::HunspellNoPfxSfx ? control : full;
    if (!context.isValid() || !spell.isValid() || !isValidSourceLayout(layout))
        return DecisionAction::Abstain;
    const CanonicalToken canonical = canonicalizeToken(text, layout);
    if (canonical.status != CanonicalStatus::Ok || canonical.keys.size() < kMinDecisionKeys)
        return DecisionAction::Abstain;
    const SpellChecker &observed = *spell.engines[std::size_t(observedLanguage(layout))];
    const SpellChecker &alternative = *spell.engines[std::size_t(alternativeLanguage(layout))];
    if (observed.accepts(text))
        return DecisionAction::Keep;
    const QString alternativeText = convertLayout(text.toString(), benchmark::correctionDirection(layout));
    auto twoScore = [&]() {
        return decideTwoScore(context.model->score(canonical.keys), layout, context.twoScore).action;
    };
    if (method == MorphMethod::HunspellNoPfxSfx || method == MorphMethod::HunspellFull)
        return alternative.accepts(alternativeText) ? DecisionAction::Correct : DecisionAction::Abstain;
    if (method == MorphMethod::MorphThenModel)
        return alternative.accepts(alternativeText) ? DecisionAction::Correct : twoScore();
    // modelConfirmedMorph: тот же порядок, что TwoScoreConfirmedByExactFull опыта 002.
    if (twoScore() == DecisionAction::Correct && alternative.accepts(alternativeText))
        return DecisionAction::Correct;
    return DecisionAction::Abstain;
}

QByteArray exactCaseTypeKey(DetectorLanguage language, bool wrongRole, const QString &text)
{
    QByteArray key;
    key.append(language == DetectorLanguage::Ru ? '\1' : '\0');
    key.append(wrongRole ? '\1' : '\0');
    key.append(text.toUtf8());
    return key;
}

} // namespace rasklad::morphology
