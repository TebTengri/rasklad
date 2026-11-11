#include "benchmarkprotocol.h"

#include "evaluationdata.h"

#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QStringList>

#include <initializer_list>

namespace rasklad::benchmark {
namespace {

void put(QJsonObject &object, const char *key, const QJsonValue &value)
{
    object.insert(QString::fromLatin1(key), value);
}

QJsonValue text(const char *latin1)
{
    return QJsonValue(QString::fromLatin1(latin1));
}

QJsonArray textArray(std::initializer_list<const char *> items)
{
    QJsonArray array;
    for (const char *item : items)
        array.append(QString::fromLatin1(item));
    return array;
}

} // namespace

QString protocolErrorText(ProtocolError error)
{
    switch (error) {
    case ProtocolError::None:
        return QStringLiteral("ok");
    case ProtocolError::TooLarge:
        return QStringLiteral("protocol exceeds size limit");
    case ProtocolError::InvalidJson:
        return QStringLiteral("protocol is not a JSON object");
    case ProtocolError::Mismatch:
        return QStringLiteral("protocol differs from the frozen protocol implemented by this tool");
    }
    return QStringLiteral("unknown protocol error");
}

QJsonObject expectedProtocol()
{
    QJsonObject root;
    put(root, "schema_version", 1);
    put(root, "experiment", text("SYS-233 experiment 002"));
    put(root, "freeze_date", text("2026-11-11"));
    put(root, "seed", text(kHoldoutSeed));
    put(root, "model_training",
        text("experiment-001 train split only; original add-alpha/int8 algorithm unchanged"));

    QJsonObject single;
    put(single, "gap", kSingleDGap);
    QJsonObject two;
    put(two, "gap", kTwoScoreGap);
    put(two, "minAlternative", kTwoScoreMinAlternative);
    put(two, "maxObserved", kTwoScoreMaxObserved);
    QJsonObject thresholds;
    put(thresholds, "singleD", single);
    put(thresholds, "twoScore", two);
    put(root, "thresholds", thresholds);

    QJsonObject selection;
    put(selection, "limit_per_language", kSampleLimitPerLanguage);
    put(selection, "rule",
        text("deduplicate exact text keeping smallest numeric ID; rank SHA256(seed + NUL + ASCII "
             "sentence ID); take smallest ranks before token filtering"));
    put(root, "sentence_selection", selection);

    QJsonObject tokenization;
    put(tokenization, "rule",
        text("split on Unicode whitespace, trim outer characters from the fixed set; retain internal "
             "punctuation; no model-dependent filtering"));
    put(tokenization, "outer_trim_characters", outerTrimCharacters());
    put(tokenization, "limitation",
        text("oracle token boundaries before wrong-layout conversion; not end-to-end keyboard "
             "segmentation"));
    put(root, "tokenization", tokenization);

    put(root, "short_token_minimum", kShortTokenMinimum);
    put(root, "methods",
        textArray({"alwaysKeep", "exactTrain", "exactFull", "singleD", "twoScore",
                   "exactFullThenTwoScore", "twoScoreConfirmedByExactFull"}));

    QJsonObject cascades;
    put(cascades, "exactFullThenTwoScore",
        text("observed exact hit -> Keep; else alternative exact hit -> Correct; else twoScore"));
    put(cascades, "twoScoreConfirmedByExactFull",
        text("observed exact hit -> Keep; else Correct only if twoScore says Correct AND alternative "
             "exact hit; otherwise Abstain"));
    put(root, "cascade_rules", cascades);

    put(root, "wrong_generation",
        text("only native-language letter-only tokens of length 1..32; generate after selection and "
             "tokenization; retain case"));
    put(root, "ambiguity",
        text("if canonical key belongs to both full dictionaries, any change counts unsafe; report "
             "separately, never forced positive"));
    put(root, "metrics",
        textArray({"valid false changes per occurrence and unique type", "wrong recovered/abstain",
                   "unsafe ambiguity changes", "per-language/length/membership-known-vs-OOV",
                   "expected precision at 0.1%,1%,5% synthetic wrong-layout prevalence; not measured "
                   "deployment precision"}));

    QJsonObject challenge;
    put(challenge, "version", kChallengeVersion);
    put(challenge, "noise_per_layout", kNoisePerLayout);
    QJsonArray lengths;
    lengths.append(kNoiseMinLength);
    lengths.append(kNoiseMaxLength);
    put(challenge, "noise_lengths", lengths);
    put(challenge, "noise_seed", text(kNoiseSeed));
    put(challenge, "rule",
        text("synthetic intent-Keep noise; exclude dictionary hits; also all full-lexicon ambiguous "
             "keys with length>=4 and original fixture code/URL/name tokens"));
    put(root, "challenge", challenge);

    QJsonObject hypotheses;
    put(hypotheses, "H1", text("compact model adds OOV recovery over exactTrain/Full"));
    put(hypotheses, "H2",
        text("exactFullThenTwoScore improves recovery over exactFull without adding unsafe changes"));
    put(hypotheses, "H3",
        text("twoScoreConfirmedByExactFull reduces false changes versus exactFull at a coverage cost"));
    put(hypotheses, "H4", text("twoScore beats singleD on hard negatives at nonzero useful recovery"));
    put(root, "hypotheses", hypotheses);

    QJsonObject gate;
    put(gate, "automatic_input", text("disabled regardless of this experiment"));
    put(gate, "continuation",
        text("report each hypothesis as supported/refuted/inconclusive on this sample; incremental "
             "recovery counts only alongside incremental unsafe changes; no threshold tuning on new "
             "test; do not claim proof of absence of false corrections"));
    put(root, "decision_gate", gate);

    put(root, "resource_method",
        text("front-coded exact blocks vs ngram serialization; fresh inference-only child processes, "
             "current RSS/peak/cold startup scoped explicitly; no lexicon/training data retained in "
             "probe; warm latency separate"));
    return root;
}

bool jsonEqual(const QJsonValue &a, const QJsonValue &b)
{
    if (a.type() != b.type())
        return false;
    switch (a.type()) {
    case QJsonValue::Null:
    case QJsonValue::Undefined:
        return true;
    case QJsonValue::Bool:
        return a.toBool() == b.toBool();
    case QJsonValue::Double:
        return a.toDouble() == b.toDouble();
    case QJsonValue::String:
        return a.toString() == b.toString();
    case QJsonValue::Array: {
        const QJsonArray x = a.toArray();
        const QJsonArray y = b.toArray();
        if (x.size() != y.size())
            return false;
        for (qsizetype i = 0; i < x.size(); ++i) {
            if (!jsonEqual(x.at(i), y.at(i)))
                return false;
        }
        return true;
    }
    case QJsonValue::Object: {
        const QJsonObject x = a.toObject();
        const QJsonObject y = b.toObject();
        if (x.size() != y.size())
            return false;
        for (auto it = x.constBegin(); it != x.constEnd(); ++it) {
            if (!y.contains(it.key()) || !jsonEqual(it.value(), y.value(it.key())))
                return false;
        }
        return true;
    }
    }
    return false;
}

ProtocolResult validateProtocol(const QByteArray &bytes)
{
    ProtocolResult result;
    if (bytes.size() > kMaxProtocolBytes) {
        result.error = ProtocolError::TooLarge;
        return result;
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(bytes, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        result.error = ProtocolError::InvalidJson;
        return result;
    }
    const QJsonObject actual = document.object();
    const QJsonObject expected = expectedProtocol();
    for (auto it = expected.constBegin(); it != expected.constEnd(); ++it) {
        if (!actual.contains(it.key()) || !jsonEqual(it.value(), actual.value(it.key()))) {
            result.error = ProtocolError::Mismatch;
            result.mismatchedField = it.key();
            return result;
        }
    }
    if (actual.size() != expected.size()) {
        result.error = ProtocolError::Mismatch;
        result.mismatchedField = QStringLiteral("unexpected field");
        return result;
    }
    result.sha256 =
        QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
    return result;
}

SingleDThresholds frozenSingleD()
{
    return SingleDThresholds{true, kSingleDGap};
}

TwoScoreThresholds frozenTwoScore()
{
    return TwoScoreThresholds{true, kTwoScoreMinAlternative, kTwoScoreMaxObserved, kTwoScoreGap};
}

} // namespace rasklad::benchmark
