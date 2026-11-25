// SPDX-License-Identifier: MIT
// SYS-272: the product's decision on a Tatoeba export, for the dictionary update
// gate (tools/update_dictionaries.py). The profile is loaded exactly as the addon
// loads it (LocalEvidenceProvider::load with the compiled-in production pins), so
// a profile the addon would reject fails here too; each word then goes through
// routing::route(SafeConfirmed), as the addon's correctTyped does.
//
// Cases, per language: every native-letter-only token of the frozen holdout
// sample (protocol seed, 5000 sentences) is a valid case (typed in its own
// layout; intent Keep) and a wrong case (the same keys in the other layout;
// intent Correct back to the token). Offline; reads only the given files; prints
// aggregate counts and rates, never a word.
#include "benchmarkprotocol.h"
#include "decisionrouter.h"
#include "evaluationdata.h"
#include "layoutconverter.h"
#include "localprofile.h"
#include "profileverify.h"

#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QStringList>

#include <array>
#include <cstdio>

using namespace rasklad;
using namespace rasklad::benchmark;

namespace {

struct Counts {
    qint64 valid = 0, falseChange = 0;
    qint64 wrong = 0, recovered = 0, wrongConversion = 0;
};

void add(Counts &into, const Counts &c)
{
    into.valid += c.valid;
    into.falseChange += c.falseChange;
    into.wrong += c.wrong;
    into.recovered += c.recovered;
    into.wrongConversion += c.wrongConversion;
}

double rate(qint64 part, qint64 whole) { return whole > 0 ? double(part) / double(whole) : 0.0; }

QJsonObject json(const Counts &c)
{
    QJsonObject valid{{QStringLiteral("count"), c.valid},
                      {QStringLiteral("falseChangeRate"), rate(c.falseChange, c.valid)}};
    QJsonObject wrong{{QStringLiteral("count"), c.wrong},
                      {QStringLiteral("recoveryRate"), rate(c.recovered, c.wrong)},
                      {QStringLiteral("wrongConversionRate"), rate(c.wrongConversion, c.wrong)}};
    return {{QStringLiteral("valid"), valid}, {QStringLiteral("wrong"), wrong}};
}

int usage()
{
    std::fprintf(stderr, "usage: product_eval --root <profile root> --eval-en <tsv> --eval-ru <tsv>\n");
    return 2;
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    const QStringList args = app.arguments();
    QString root, evalPath[2];
    for (qsizetype i = 1; i + 1 < args.size(); i += 2) {
        if (args[i] == QLatin1String("--root")) root = args[i + 1];
        else if (args[i] == QLatin1String("--eval-en")) evalPath[0] = args[i + 1];
        else if (args[i] == QLatin1String("--eval-ru")) evalPath[1] = args[i + 1];
        else return usage();
    }
    if (args.size() != 7 || root.isEmpty() || evalPath[0].isEmpty() || evalPath[1].isEmpty()) return usage();

    profile::ProfileError error = profile::ProfileError::None;
    const auto provider = profile::LocalEvidenceProvider::load(root, profile::productionPins(), error);
    if (!provider || error != profile::ProfileError::None) {
        const QJsonObject out{{QStringLiteral("loaded"), false},
                              {QStringLiteral("error"), QString::fromLatin1(profile::profileErrorName(error))}};
        std::printf("%s\n", QJsonDocument(out).toJson(QJsonDocument::Compact).constData());
        return 3; // the addon would reject this profile too
    }

    std::array<Counts, 2> occ{}, types{};
    for (const DetectorLanguage language : {DetectorLanguage::En, DetectorLanguage::Ru}) {
        const std::size_t li = std::size_t(language);
        QFile f(evalPath[li]);
        if (!f.open(QIODevice::ReadOnly)) return usage();
        TsvParseResult tsv = parseTatoebaTsv(f.readAll(), QString::fromLatin1(isoLanguageFor(language)));
        if (tsv.error != TsvError::None) {
            std::fprintf(stderr, "product_eval: %s\n", qPrintable(tsvErrorText(tsv.error)));
            return 4;
        }
        SelectionResult selection = selectSentences(std::move(tsv.records), QByteArray(kHoldoutSeed),
                                                    kSampleLimitPerLanguage);
        if (selection.report.selected != kSampleLimitPerLanguage) {
            std::fprintf(stderr, "product_eval: fewer unique sentences than the frozen sample size\n");
            return 4;
        }
        const bool en = language == DetectorLanguage::En;
        const routing::InputLayout own = en ? routing::InputLayout::KnownUs : routing::InputLayout::KnownRu;
        const routing::InputLayout other = en ? routing::InputLayout::KnownRu : routing::InputLayout::KnownUs;
        const LayoutDirection toOther = en ? LayoutDirection::UsToRu : LayoutDirection::RuToUs;
        QSet<QString> seen;
        for (const SentenceRecord &record : selection.selected) {
            for (const QString &token : tokenizeSentence(record.text)) {
                if (!isNativeLetterOnly(token, language)) continue;
                Counts c;
                c.valid = 1;
                const routing::RouteDecision keep = routing::route(
                    {routing::FieldContext::Prose, own, QStringView(token)}, routing::RouteProfile::SafeConfirmed,
                    provider.get());
                c.falseChange = keep.action == routing::RouteAction::Correct ? 1 : 0;
                const QString typed = convertLayout(token, toOther);
                c.wrong = 1;
                const routing::RouteDecision fix = routing::route(
                    {routing::FieldContext::Prose, other, QStringView(typed)}, routing::RouteProfile::SafeConfirmed,
                    provider.get());
                if (fix.action == routing::RouteAction::Correct)
                    (fix.candidate == token ? c.recovered : c.wrongConversion) = 1;
                add(occ[li], c);
                if (!seen.contains(token)) {
                    seen.insert(token);
                    add(types[li], c);
                }
            }
        }
    }

    QJsonObject methods;
    for (const auto &[unit, counts] : {std::pair{"occurrences", &occ}, std::pair{"uniqueTypes", &types}}) {
        Counts all;
        add(all, (*counts)[0]);
        add(all, (*counts)[1]);
        QJsonObject u = json(all);
        u.insert(QStringLiteral("byIntendedLanguage"),
                 QJsonObject{{QStringLiteral("en"), json((*counts)[0])}, {QStringLiteral("ru"), json((*counts)[1])}});
        methods.insert(QString::fromLatin1(unit), u);
    }
    const QJsonObject out{{QStringLiteral("loaded"), true},
                          {QStringLiteral("methods"), QJsonObject{{QStringLiteral("production"), methods}}}};
    std::printf("%s\n", QJsonDocument(out).toJson(QJsonDocument::Indented).constData());
    return 0;
}
