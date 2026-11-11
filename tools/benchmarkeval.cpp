#include "benchmarkeval.h"

#include "evaluationdata.h"

#include <QCryptographicHash>
#include <QHash>
#include <QSet>

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <utility>

namespace rasklad::benchmark {
namespace {

void put(QJsonObject &object, const char *key, const QJsonValue &value)
{
    object.insert(QString::fromLatin1(key), value);
}

QJsonValue ratio(qint64 numerator, qint64 denominator)
{
    if (denominator <= 0)
        return QJsonValue(QJsonValue::Null);
    return QJsonValue(double(numerator) / double(denominator));
}

DecisionAction exactDecision(bool eligible, bool observed, bool alternative)
{
    if (!eligible)
        return DecisionAction::Abstain;
    if (observed)
        return DecisionAction::Keep;
    if (alternative)
        return DecisionAction::Correct;
    return DecisionAction::Abstain;
}

Membership membership(bool supported, bool observed, bool alternative)
{
    if (!supported)
        return Membership::Unsupported;
    if (observed && alternative)
        return Membership::Both;
    if (observed)
        return Membership::ObservedOnly;
    if (alternative)
        return Membership::AlternativeOnly;
    return Membership::Neither;
}

bool lexiconContains(const FrontCodedLexicon *lexicon, const KeySequence &keys)
{
    return lexicon && lexicon->contains(keys);
}

void addFixtures(std::vector<ChallengeFixture> &out, const char *category, SourceLayout layout,
                 std::initializer_list<const char *> utf8Tokens)
{
    for (const char *token : utf8Tokens)
        out.push_back({QString::fromLatin1(category), layout, QString::fromUtf8(token)});
}

QJsonObject roleJson(const RoleCounts &r, bool wrongRole, bool ambiguousRole)
{
    QJsonObject o;
    put(o, "count", r.count);
    put(o, "keep", r.keep);
    put(o, "abstain", r.abstain);
    if (ambiguousRole) {
        put(o, "unsafeChanges", r.changes());
        put(o, "changeRate", ratio(r.changes(), r.count));
    } else if (wrongRole) {
        put(o, "recovered", r.recovered);
        put(o, "wrongConversions", r.wrongConversion);
        put(o, "recoveryRate", ratio(r.recovered, r.count));
        put(o, "abstainRate", ratio(r.abstain, r.count));
    } else {
        put(o, "falseChanges", r.falseChange);
        put(o, "falseChangeRate", ratio(r.falseChange, r.count));
        put(o, "abstainRate", ratio(r.abstain, r.count));
    }
    return o;
}

} // namespace

const char *methodName(Method method)
{
    switch (method) {
    case Method::AlwaysKeep:
        return "alwaysKeep";
    case Method::ExactTrain:
        return "exactTrain";
    case Method::ExactFull:
        return "exactFull";
    case Method::SingleD:
        return "singleD";
    case Method::TwoScore:
        return "twoScore";
    case Method::ExactFullThenTwoScore:
        return "exactFullThenTwoScore";
    case Method::TwoScoreConfirmedByExactFull:
        return "twoScoreConfirmedByExactFull";
    }
    return "unknown";
}

bool isDecisionEligible(const CaseFeatures &features)
{
    return features.supported && features.keyCount >= kMinDecisionKeys
        && features.keyCount <= kMaxTokenKeys;
}

DecisionAction decideMethod(Method method, const CaseFeatures &f)
{
    const bool eligible = isDecisionEligible(f);
    switch (method) {
    case Method::AlwaysKeep:
        return DecisionAction::Keep;
    case Method::ExactTrain:
        return exactDecision(eligible, f.observedTrain, f.alternativeTrain);
    case Method::ExactFull:
        return exactDecision(eligible, f.observedFull, f.alternativeFull);
    case Method::SingleD:
        return eligible ? f.singleD : DecisionAction::Abstain;
    case Method::TwoScore:
        return eligible ? f.twoScore : DecisionAction::Abstain;
    case Method::ExactFullThenTwoScore:
        if (!eligible)
            return DecisionAction::Abstain;
        if (f.observedFull)
            return DecisionAction::Keep;
        if (f.alternativeFull)
            return DecisionAction::Correct;
        return f.twoScore;
    case Method::TwoScoreConfirmedByExactFull:
        if (!eligible)
            return DecisionAction::Abstain;
        if (f.observedFull)
            return DecisionAction::Keep;
        if (f.twoScore == DecisionAction::Correct && f.alternativeFull)
            return DecisionAction::Correct;
        return DecisionAction::Abstain;
    }
    return DecisionAction::Abstain;
}

bool FeatureContext::isValid() const
{
    return model && model->isValid() && train[0] && train[1] && full[0] && full[1];
}

CaseFeatures computeFeatures(const CanonicalToken &canonical, SourceLayout layout,
                             const FeatureContext &context)
{
    CaseFeatures f;
    if (canonical.status != CanonicalStatus::Ok || !isValidSourceLayout(layout) || !context.isValid())
        return f;
    f.supported = true;
    f.keyCount = int(canonical.keys.size());
    const std::size_t observed = std::size_t(observedLanguage(layout));
    const std::size_t alternative = std::size_t(alternativeLanguage(layout));
    f.observedTrain = lexiconContains(context.train[observed], canonical.keys);
    f.alternativeTrain = lexiconContains(context.train[alternative], canonical.keys);
    f.observedFull = lexiconContains(context.full[observed], canonical.keys);
    f.alternativeFull = lexiconContains(context.full[alternative], canonical.keys);
    const TokenScore score = context.model->score(canonical.keys);
    f.singleD = decideSingleD(score, layout, context.singleD).action;
    f.twoScore = decideTwoScore(score, layout, context.twoScore).action;
    return f;
}

DecisionAction decideLive(Method method, QStringView text, SourceLayout layout,
                          const FeatureContext &context)
{
    if (method == Method::AlwaysKeep)
        return DecisionAction::Keep;
    if (!context.isValid() || !isValidSourceLayout(layout))
        return DecisionAction::Abstain;
    const CanonicalToken canonical = canonicalizeToken(text, layout);
    if (canonical.status != CanonicalStatus::Ok || canonical.keys.size() < kMinDecisionKeys)
        return DecisionAction::Abstain;
    const std::size_t observed = std::size_t(observedLanguage(layout));
    const std::size_t alternative = std::size_t(alternativeLanguage(layout));
    auto twoScore = [&]() {
        return decideTwoScore(context.model->score(canonical.keys), layout, context.twoScore).action;
    };
    switch (method) {
    case Method::AlwaysKeep:
        return DecisionAction::Keep;
    case Method::ExactTrain:
        if (context.train[observed]->contains(canonical.keys))
            return DecisionAction::Keep;
        return context.train[alternative]->contains(canonical.keys) ? DecisionAction::Correct
                                                                    : DecisionAction::Abstain;
    case Method::ExactFull:
        if (context.full[observed]->contains(canonical.keys))
            return DecisionAction::Keep;
        return context.full[alternative]->contains(canonical.keys) ? DecisionAction::Correct
                                                                   : DecisionAction::Abstain;
    case Method::SingleD:
        return decideSingleD(context.model->score(canonical.keys), layout, context.singleD).action;
    case Method::TwoScore:
        return twoScore();
    case Method::ExactFullThenTwoScore:
        if (context.full[observed]->contains(canonical.keys))
            return DecisionAction::Keep;
        if (context.full[alternative]->contains(canonical.keys))
            return DecisionAction::Correct;
        return twoScore();
    case Method::TwoScoreConfirmedByExactFull:
        if (context.full[observed]->contains(canonical.keys))
            return DecisionAction::Keep;
        if (twoScore() == DecisionAction::Correct && context.full[alternative]->contains(canonical.keys))
            return DecisionAction::Correct;
        return DecisionAction::Abstain;
    }
    return DecisionAction::Abstain;
}

const char *membershipName(Membership m)
{
    switch (m) {
    case Membership::Unsupported:
        return "unsupported";
    case Membership::ObservedOnly:
        return "observedOnly";
    case Membership::AlternativeOnly:
        return "alternativeOnly";
    case Membership::Both:
        return "both";
    case Membership::Neither:
        return "neither";
    }
    return "unknown";
}

Membership fullMembership(const CaseFeatures &f)
{
    return membership(f.supported, f.observedFull, f.alternativeFull);
}

Membership trainMembership(const CaseFeatures &f)
{
    return membership(f.supported, f.observedTrain, f.alternativeTrain);
}

Outcome classifyOutcome(DecisionAction action, bool wrongIntent, bool ambiguous, bool conversionMatches)
{
    if (action == DecisionAction::Keep)
        return Outcome::Keep;
    if (action != DecisionAction::Correct)
        return Outcome::Abstain;
    if (ambiguous)
        return Outcome::AmbiguousChange;
    if (!wrongIntent)
        return Outcome::FalseChange;
    return conversionMatches ? Outcome::Recovered : Outcome::WrongConversion;
}

bool isUnsafe(Outcome outcome)
{
    return outcome == Outcome::FalseChange || outcome == Outcome::WrongConversion
        || outcome == Outcome::AmbiguousChange;
}

void RoleCounts::add(Outcome outcome)
{
    ++count;
    switch (outcome) {
    case Outcome::Keep:
        ++keep;
        break;
    case Outcome::Abstain:
        ++abstain;
        break;
    case Outcome::Recovered:
        ++recovered;
        break;
    case Outcome::FalseChange:
        ++falseChange;
        break;
    case Outcome::WrongConversion:
        ++wrongConversion;
        break;
    case Outcome::AmbiguousChange:
        ++ambiguousChange;
        break;
    }
}

qint64 RoleCounts::changes() const
{
    return recovered + falseChange + wrongConversion + ambiguousChange;
}

qint64 RoleCounts::unsafe() const
{
    return falseChange + wrongConversion + ambiguousChange;
}

void SliceCounts::add(bool wrongIntent, bool ambiguous, Outcome outcome)
{
    if (ambiguous)
        (wrongIntent ? ambiguousWrong : ambiguousValid).add(outcome);
    else
        (wrongIntent ? wrong : valid).add(outcome);
}

qint64 SliceCounts::unsafe() const
{
    return valid.unsafe() + wrong.unsafe() + ambiguousValid.unsafe() + ambiguousWrong.unsafe();
}

QJsonObject sliceJson(const SliceCounts &s)
{
    QJsonObject o;
    put(o, "valid", roleJson(s.valid, false, false));
    put(o, "wrong", roleJson(s.wrong, true, false));
    put(o, "ambiguousValid", roleJson(s.ambiguousValid, false, true));
    put(o, "ambiguousWrong", roleJson(s.ambiguousWrong, true, true));
    put(o, "unsafeChangesIncludingAmbiguous", s.unsafe());
    return o;
}

std::optional<double> expectedPrecision(double prevalence, double recoveredRate, double unsafeWrongRate,
                                        double validChangeRate)
{
    auto inUnit = [](double x) { return std::isfinite(x) && x >= 0.0 && x <= 1.0; };
    if (!inUnit(prevalence) || !inUnit(recoveredRate) || !inUnit(unsafeWrongRate)
        || !inUnit(validChangeRate))
        return std::nullopt;
    const double numerator = prevalence * recoveredRate;
    const double denominator =
        prevalence * (recoveredRate + unsafeWrongRate) + (1.0 - prevalence) * validChangeRate;
    if (!(denominator > 0.0))
        return std::nullopt;
    return numerator / denominator;
}

QJsonObject expectedPrecisionJson(const SliceCounts &overall)
{
    const qint64 allWrong = overall.wrong.count + overall.ambiguousWrong.count;
    const qint64 allValid = overall.valid.count + overall.ambiguousValid.count;
    QJsonObject o;
    put(o, "label",
        QStringLiteral("analytic scenario, not deployment precision: assumes the natural valid mix and "
                       "the synthetic wrong-layout mix represent a stream with the given wrong-layout "
                       "prevalence, i.e. that the measured conditional rates r, u, f transfer to that "
                       "stream; the point value is a mixture identity over these marginal rates, while "
                       "repeated words are not independent trials, so no confidence interval is given"));
    put(o, "formula", QStringLiteral("P = p*r / (p*(r+u) + (1-p)*f)"));
    if (allWrong <= 0 || allValid <= 0) {
        put(o, "available", false);
        return o;
    }
    const double r = double(overall.wrong.recovered) / double(allWrong);
    const double u = double(overall.wrong.wrongConversion + overall.ambiguousWrong.changes()) / double(allWrong);
    const double f = double(overall.valid.falseChange + overall.ambiguousValid.changes()) / double(allValid);
    put(o, "available", true);
    put(o, "recoveredRateR", r);
    put(o, "unsafeWrongRateU", u);
    put(o, "validChangeRateF", f);
    QJsonObject scenarios;
    const std::array<std::pair<const char *, double>, 3> prevalences{
        {{"p0.001", 0.001}, {"p0.01", 0.01}, {"p0.05", 0.05}}};
    for (const auto &[name, p] : prevalences) {
        const std::optional<double> precision = expectedPrecision(p, r, u, f);
        put(scenarios, name, precision ? QJsonValue(*precision) : QJsonValue(QJsonValue::Null));
    }
    put(o, "precision", scenarios);
    put(o, "nullMeans", QStringLiteral("no changes at all (denominator 0)"));
    return o;
}

PairedDelta pairedDelta(const std::vector<Outcome> &method, const std::vector<Outcome> &baseline,
                        const std::vector<std::size_t> &indices)
{
    PairedDelta d;
    for (const std::size_t i : indices) {
        if (i >= method.size() || i >= baseline.size())
            continue;
        ++d.cases;
        const Outcome m = method[i];
        const Outcome b = baseline[i];
        const bool mr = m == Outcome::Recovered;
        const bool br = b == Outcome::Recovered;
        const bool mu = isUnsafe(m);
        const bool bu = isUnsafe(b);
        if (mr && !br)
            ++d.addedRecoveries;
        if (br && !mr)
            ++d.lostRecoveries;
        if (mr && br)
            ++d.bothRecovered;
        if (mu && !bu)
            ++d.addedUnsafe;
        if (bu && !mu)
            ++d.preventedUnsafe;
        if (mu && bu)
            ++d.bothUnsafe;
    }
    return d;
}

QJsonObject pairedJson(const PairedDelta &d)
{
    QJsonObject o;
    put(o, "pairedCases", d.cases);
    put(o, "addedRecoveries", d.addedRecoveries);
    put(o, "lostRecoveries", d.lostRecoveries);
    put(o, "netRecoveries", d.addedRecoveries - d.lostRecoveries);
    put(o, "addedUnsafeChanges", d.addedUnsafe);
    put(o, "preventedUnsafeChanges", d.preventedUnsafe);
    put(o, "netUnsafeChanges", d.addedUnsafe - d.preventedUnsafe);
    put(o, "bothRecovered", d.bothRecovered);
    put(o, "bothUnsafe", d.bothUnsafe);
    return o;
}

SourceLayout homeLayout(DetectorLanguage language)
{
    return language == DetectorLanguage::Ru ? SourceLayout::Ru : SourceLayout::Us;
}

SourceLayout otherLayout(SourceLayout layout)
{
    return layout == SourceLayout::Us ? SourceLayout::Ru : SourceLayout::Us;
}

LayoutDirection correctionDirection(SourceLayout typedLayout)
{
    return typedLayout == SourceLayout::Us ? LayoutDirection::UsToRu : LayoutDirection::RuToUs;
}

const char *languageName(DetectorLanguage language)
{
    return language == DetectorLanguage::Ru ? "ru" : "en";
}

const char *layoutName(SourceLayout layout)
{
    return layout == SourceLayout::Ru ? "ru" : "us";
}

const char *canonicalStatusName(CanonicalStatus status)
{
    switch (status) {
    case CanonicalStatus::Ok:
        return "ok";
    case CanonicalStatus::Empty:
        return "empty";
    case CanonicalStatus::TooLong:
        return "tooLong";
    case CanonicalStatus::MixedScript:
        return "mixedScript";
    case CanonicalStatus::UnsupportedCharacter:
        return "unsupportedCharacter";
    case CanonicalStatus::InvalidLayout:
        return "invalidLayout";
    }
    return "unknown";
}

NaturalCases buildNaturalCases(const std::array<std::vector<QString>, 2> &tokens,
                               const FeatureContext &context)
{
    NaturalCases result;
    QHash<QByteArray, quint32> typeIds;
    auto assignType = [&typeIds](EvalCase &c, std::size_t li, const KeySequence &keys,
                                 const QString &text) {
        QByteArray key;
        key.append(static_cast<char>(li));
        key.append(c.wrong ? '\1' : '\0');
        if (c.status == CanonicalStatus::Ok) {
            key.append('k');
            key.append(keys);
        } else {
            key.append('t');
            key.append(text.toUtf8());
        }
        const auto it = typeIds.constFind(key);
        if (it != typeIds.constEnd()) {
            c.typeId = it.value();
            c.typeRepresentative = false;
            return false;
        }
        c.typeId = quint32(typeIds.size());
        c.typeRepresentative = true;
        typeIds.insert(key, c.typeId);
        return true;
    };

    for (std::size_t li = 0; li < 2; ++li) {
        const DetectorLanguage language = DetectorLanguage(li);
        const SourceLayout home = homeLayout(language);
        const SourceLayout away = otherLayout(home);
        NaturalTokenStats &stats = result.stats[li];
        for (const QString &token : tokens[li]) {
            EvalCase valid;
            valid.language = language;
            valid.layout = home;
            valid.wrong = false;
            valid.text = token;
            const CanonicalToken canonical = canonicalizeToken(token, home);
            valid.status = canonical.status;
            valid.length = canonical.status == CanonicalStatus::Ok ? int(canonical.keys.size())
                                                                   : int(token.size());
            valid.features = computeFeatures(canonical, home, context);
            valid.ambiguous = valid.features.observedFull && valid.features.alternativeFull;
            valid.nativeLetterOnly = isNativeLetterOnly(token, language);
            valid.conversionMatches = true;

            ++stats.tokens;
            switch (canonical.status) {
            case CanonicalStatus::Ok:
                ++stats.supported;
                if (!valid.nativeLetterOnly)
                    ++stats.supportedNotLetterOnly;
                if (canonical.hadUppercase)
                    ++stats.hadUppercase;
                break;
            case CanonicalStatus::MixedScript:
                ++stats.mixedScript;
                break;
            case CanonicalStatus::TooLong:
                ++stats.tooLong;
                break;
            default:
                ++stats.unsupportedCharacter;
                break;
            }
            if (isDecisionEligible(valid.features))
                ++stats.eligible;
            if (valid.nativeLetterOnly)
                ++stats.nativeLetterOnly;
            if (valid.ambiguous)
                ++stats.ambiguous;
            if (assignType(valid, li, canonical.keys, token))
                ++stats.validTypes;

            if (valid.nativeLetterOnly) {
                EvalCase wrong;
                wrong.language = language;
                wrong.layout = away;
                wrong.wrong = true;
                wrong.text = convertLayout(token, correctionDirection(home));
                wrong.conversionMatches = convertLayout(wrong.text, correctionDirection(away)) == token;
                const CanonicalToken wrongCanonical = canonicalizeToken(wrong.text, away);
                wrong.status = wrongCanonical.status;
                wrong.length = wrongCanonical.status == CanonicalStatus::Ok
                    ? int(wrongCanonical.keys.size())
                    : int(wrong.text.size());
                if (wrongCanonical.status != CanonicalStatus::Ok || wrongCanonical.keys != canonical.keys)
                    ++stats.wrongCanonicalMismatches;
                wrong.features = computeFeatures(wrongCanonical, away, context);
                wrong.ambiguous = valid.ambiguous;
                wrong.nativeLetterOnly = false;
                ++stats.wrongCopies;
                if (assignType(wrong, li, wrongCanonical.keys, wrong.text))
                    ++stats.wrongTypes;
                result.cases.push_back(std::move(valid));
                result.cases.push_back(std::move(wrong));
            } else {
                result.cases.push_back(std::move(valid));
            }
        }
    }
    return result;
}

QJsonObject naturalStatsJson(const NaturalTokenStats &s)
{
    QJsonObject o;
    put(o, "tokens", s.tokens);
    put(o, "supported", s.supported);
    put(o, "eligibleLen4Plus", s.eligible);
    put(o, "nativeLetterOnly", s.nativeLetterOnly);
    put(o, "supportedNotLetterOnly", s.supportedNotLetterOnly);
    put(o, "mixedScript", s.mixedScript);
    put(o, "unsupportedCharacter", s.unsupportedCharacter);
    put(o, "tooLong", s.tooLong);
    put(o, "hadUppercase", s.hadUppercase);
    put(o, "ambiguousOccurrences", s.ambiguous);
    put(o, "wrongCopies", s.wrongCopies);
    put(o, "wrongCanonicalMismatches", s.wrongCanonicalMismatches);
    put(o, "validTypes", s.validTypes);
    put(o, "wrongTypes", s.wrongTypes);
    return o;
}

std::vector<ChallengeFixture> challengeFixtures()
{
    std::vector<ChallengeFixture> out;
    const SourceLayout us = SourceLayout::Us;
    const SourceLayout ru = SourceLayout::Ru;
    addFixtures(out, "url", us,
                {"https://example.invalid/docs", "http://test.example.invalid:8080/a?b=c",
                 "example.invalid", "www.example.invalid", "docs.example.invalid", "cdn.example.invalid",
                 "example.invalid/index.html", "sub.domain.example.invalid"});
    addFixtures(out, "email", us,
                {"user@example.invalid", "first.last@example.invalid", "noreply@mail.example.invalid",
                 "a.b+tag@example.invalid", "admin@example.invalid"});
    addFixtures(out, "command", us,
                {"grep", "sudo", "chmod", "mkdir", "rsync", "awk", "sed", "ls", "cd", "systemctl",
                 "journalctl", "qmake6", "make", "fsck", "htop", "lsblk", "xargs", "pacman", "nmcli",
                 "hyprctl"});
    addFixtures(out, "path", us,
                {"/usr/bin/env", "~/.config", "./build", "../src", "/etc/hosts", "build/tools",
                 "src/main.cpp", "C:\\Temp", "%APPDATA%", "~/notes.txt"});
    addFixtures(out, "code", us,
                {"nullptr", "QString", "std::vector", "argv", "argc", "malloc", "printf", "ifdef", "endl",
                 "camelCase", "snake_case", "kMaxKeys", "fooBar", "toUtf8", "int32_t", "x++", "a[i]",
                 "{}", "self.value", "constexpr", "sizeof", "foo();"});
    addFixtures(out, "hash", us,
                {"deadbeef", "cafebabe", "3f9a1c0b", "e3b0c442", "a1b2c3d4", "facefeed", "badc0ffee",
                 "0xFFFF", "abcdef12", "bada55"});
    addFixtures(out, "number", us,
                {"2026", "3.14", "1e9", "0x1F", "42", "1,000", "-17", "12:30", "v2.5", "100%"});
    // Смешанная письменность: кириллица в us-токене и латиница в ru-токене.
    addFixtures(out, "mixedScript", us, {"\xD0\xA2" "est", "pr\xD0\xBE" "xy", "\xD1\x81" "ase"});
    addFixtures(out, "mixedScript", ru,
                {"M\xD0\xB0\xD1\x81\xD0\xBA\xD0\xB0", "\xD1\x82\xD0\xB5\xD1\x81" "t",
                 "\xD0\x92\xD0\xB0" "sya"});
    addFixtures(out, "name", us,
                {"Zhang", "Nguyen", "Okonkwo", "Bjornstad", "Quixley", "Vantorp", "Ishikawa", "Kowalczyk",
                 "Xochitl", "Dvorak"});
    addFixtures(out, "name", ru,
                {"\xD0\xAF\xD1\x80\xD0\xBE\xD0\xBF\xD0\xBE\xD0\xBB\xD0\xBA",
                 "\xD0\x95\xD0\xB2\xD0\xBB\xD0\xB0\xD0\xBC\xD0\xBF\xD0\xB8\xD1\x8F",
                 "\xD0\xA2\xD0\xB8\xD1\x85\xD0\xBE\xD0\xBC\xD0\xB8\xD1\x80\xD0\xBE\xD0\xB2",
                 "\xD0\x97\xD0\xBB\xD0\xB0\xD1\x82\xD0\xBE\xD1\x8F\xD1\x80",
                 "\xD0\x91\xD1\x83\xD0\xB4\xD0\xB8\xD0\xBC\xD0\xB8\xD1\x80",
                 "\xD0\x90\xD0\xB3\xD0\xB0\xD1\x84\xD1\x8C\xD1\x8F"});
    addFixtures(out, "acronym", us, {"NATO", "HTTP", "JSON", "UTF", "XKB", "QML", "SQL", "GPU", "RFC", "DNS"});
    addFixtures(out, "acronym", ru,
                {"\xD0\xA1\xD0\xA1\xD0\xA1\xD0\xA0", "\xD0\x9C\xD0\x93\xD0\xA3",
                 "\xD0\x93\xD0\x9E\xD0\xA1\xD0\xA2", "\xD0\xA0\xD0\x96\xD0\x94", "\xD0\x9E\xD0\xA1",
                 "\xD0\x92\xD0\xA3\xD0\x97"});
    return out;
}

ChallengeCase makeChallengeCase(const QString &category, SourceLayout layout, QStringView text,
                                const FeatureContext &context)
{
    ChallengeCase c;
    c.category = category;
    c.layout = layout;
    const CanonicalToken canonical = canonicalizeToken(text, layout);
    c.status = canonical.status;
    c.length = canonical.status == CanonicalStatus::Ok ? int(canonical.keys.size()) : int(text.size());
    c.features = computeFeatures(canonical, layout, context);
    c.ambiguous = c.features.observedFull && c.features.alternativeFull;
    return c;
}

NoiseResult generateNoiseKeys(const QByteArray &seed, int count, int minLength, int maxLength,
                              qint64 &counter, qint64 maxAttempts,
                              const std::function<bool(const KeySequence &)> &excluded)
{
    NoiseResult result;
    // Ключ длины n использует байты дайджеста 1..n; SHA-256 даёт 32 байта.
    if (count < 0 || minLength < 1 || maxLength > 31 || minLength > maxLength || maxAttempts < 0)
        return result;
    const int span = maxLength - minLength + 1;
    QSet<KeySequence> seen;
    while (result.keys.size() < std::size_t(count) && result.attempts < maxAttempts) {
        QByteArray input = seed;
        input.append('\0');
        input.append(QByteArray::number(counter));
        ++counter;
        ++result.attempts;
        const QByteArray digest = QCryptographicHash::hash(input, QCryptographicHash::Sha256);
        const int length = minLength + int(static_cast<unsigned char>(digest.at(0)) % unsigned(span));
        KeySequence keys;
        keys.reserve(length);
        for (int j = 0; j < length; ++j) {
            const unsigned value = static_cast<unsigned char>(digest.at(1 + j));
            keys.append(static_cast<char>(value % unsigned(NgramModel::kLetterKeys)));
        }
        if (excluded && excluded(keys)) {
            ++result.rejectedExcluded;
            continue;
        }
        if (seen.contains(keys)) {
            ++result.rejectedDuplicate;
            continue;
        }
        seen.insert(keys);
        result.keys.push_back(keys);
    }
    result.complete = result.keys.size() == std::size_t(count);
    return result;
}

QString keysToLayoutText(const KeySequence &keys, SourceLayout layout)
{
    const QString usBase = keysToUsBase(keys);
    if (layout == SourceLayout::Ru)
        return convertLayout(usBase, LayoutDirection::UsToRu);
    return usBase;
}

double percentileNearestRank(const std::vector<double> &sorted, double p)
{
    if (sorted.empty())
        return 0.0;
    const auto rank = std::size_t(std::ceil(p / 100.0 * double(sorted.size())));
    return sorted[std::min(sorted.size() - 1, rank == 0 ? 0 : rank - 1)];
}

} // namespace rasklad::benchmark
