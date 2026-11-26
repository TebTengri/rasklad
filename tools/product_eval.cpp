// SPDX-License-Identifier: MIT
// SYS-272: the product's decision on a Tatoeba export, for the dictionary update
// gate (tools/update_dictionaries.py). The profile is loaded exactly as the addon
// loads it (LocalEvidenceProvider::load with the compiled-in production pins), so
// a profile the addon would reject fails here too; each word then goes through
// the addon's own decision: rasklad::autocorrect::decide (src/autocorrect.h) over
// ProfileDecision::correctTyped, with the case rule, the frequent words and the
// trailing-key fallback, and no exception words.
//
// Cases, per language: every native-letter-only token of the frozen holdout
// sample (protocol seed, 5000 sentences) is a valid case (typed in its own
// layout; intent Keep) and a wrong case (the same keys in the other layout;
// intent Correct back to the token: only the whole word equal to it is a recovery;
// any other change, a partial one too, is a wrong conversion). Offline; reads only the given files; prints
// aggregate counts and rates, never a word. --unreviewed-update: the profile is a
// dictionary update the owner has not reviewed for distribution (SYS-256); it must
// say release_approved=false everywhere, and every other check stays.
#include "autocorrect.h"
#include "benchmarkprotocol.h"
#include "fcitxlabprofile.h"
#include "manualgesture.h"
#include "decisionrouter.h"
#include "evaluationdata.h"
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

// The token as the addon's buffer holds it: lowercase us keys and the capitals'
// mask. False if a character has no key or the word does not fit the buffer.
bool toKeys(const QString &token, std::string &keys, std::uint32_t &upper)
{
    keys.clear();
    upper = 0;
    const QList<uint> ucs4 = token.toUcs4();
    if (ucs4.isEmpty() || ucs4.size() > 32) return false;
    for (qsizetype i = 0; i < ucs4.size(); ++i) {
        const QString one = QString::fromUcs4(reinterpret_cast<const char32_t *>(&ucs4[i]), 1);
        const QString lower = one.toLower();
        if (lower != one) upper |= std::uint32_t{1} << i;
        const QList<uint> l = lower.toUcs4();
        if (l.size() != 1) return false;
        char key = 0;
        if (l[0] >= 'a' && l[0] <= 'z') key = char(l[0]);
        else key = manual::ruToUsKey(char32_t(l[0]));
        if (key == 0) return false;
        keys.push_back(key);
    }
    return true;
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
    std::fprintf(stderr, "usage: product_eval [--unreviewed-update] --root <profile root> --eval-en <tsv> --eval-ru <tsv>\n");
    return 2;
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QStringList args = app.arguments();
    const bool unreviewed = args.size() > 1 && args[1] == QLatin1String("--unreviewed-update");
    if (unreviewed)
        args.removeAt(1);
    QString root, evalPath[2];
    for (qsizetype i = 1; i + 1 < args.size(); i += 2) {
        if (args[i] == QLatin1String("--root")) root = args[i + 1];
        else if (args[i] == QLatin1String("--eval-en")) evalPath[0] = args[i + 1];
        else if (args[i] == QLatin1String("--eval-ru")) evalPath[1] = args[i + 1];
        else return usage();
    }
    if (args.size() != 7 || root.isEmpty() || evalPath[0].isEmpty() || evalPath[1].isEmpty()) return usage();

    profile::ProfileError error = profile::ProfileError::None;
    profile::ProfilePins pins = profile::productionPins();
    pins.requireReleaseApproval = !unreviewed;
    const auto provider = profile::LocalEvidenceProvider::load(root, pins, error);
    if (!provider || error != profile::ProfileError::None) {
        const QJsonObject out{{QStringLiteral("loaded"), false},
                              {QStringLiteral("error"), QString::fromLatin1(profile::profileErrorName(error))}};
        std::printf("%s\n", QJsonDocument(out).toJson(QJsonDocument::Compact).constData());
        return 3; // the addon would reject this profile too
    }

    fcitxlab::ProfileDecision decision(root.toUtf8().constData(), unreviewed);
    if (!decision.ready()) {
        std::fprintf(stderr, "product_eval: the addon's profile decision did not load\n");
        return 3;
    }
    const auto correct = [&decision](const std::string &typed, bool typedRu) {
        return decision.correctTyped(typed, typedRu);
    };
    const auto noException = [](const std::string &) { return false; };

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
        const bool ownRu = language == DetectorLanguage::Ru;
        QSet<QString> seen;
        for (const SentenceRecord &record : selection.selected) {
            for (const QString &token : tokenizeSentence(record.text)) {
                if (!isNativeLetterOnly(token, language)) continue;
                std::string keys;
                std::uint32_t upper = 0;
                if (!toKeys(token, keys, upper)) continue; // the addon never buffers it
                Counts c;
                std::string out;
                c.valid = 1;
                c.falseChange = autocorrect::decide(keys, ownRu, upper, correct, noException, out)
                                        != autocorrect::Result::None ? 1 : 0;
                out.clear();
                c.wrong = 1;
                const autocorrect::Result fix = autocorrect::decide(keys, !ownRu, upper, correct, noException, out);
                if (fix == autocorrect::Result::Whole && out == token.toStdString())
                    c.recovered = 1;
                else if (fix != autocorrect::Result::None)
                    c.wrongConversion = 1;
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
