// SPDX-License-Identifier: MIT
// Evaluate every finite evidence-policy state using the production C++ router.
// No profiles, models, dictionaries, user input, files or network are opened.
#include "decisionrouter.h"
#include "synthetic.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>
#include <QVector>
#include <cstdio>

using namespace rasklad;
using namespace rasklad::routing;

namespace {
class Source final : public EvidenceSource {
public:
    TokenEvidence value;
    std::optional<TokenEvidence> evidence(const EvidenceQuery &) const override { return value; }
};
QJsonObject canonicalMap(SourceLayout layout)
{
    QJsonObject out;
    for (unsigned int code = 0; code <= 0xffff; ++code) {
        const QString text{QChar(static_cast<char16_t>(code))};
        const auto canonical = canonicalizeToken(QStringView(text), layout);
        if (canonical.status == CanonicalStatus::Ok && canonical.keys.size() == 1)
            out.insert(text, static_cast<unsigned char>(canonical.keys.at(0)));
    }
    return out;
}
}
int main(int argc, char **argv)
{
    if (argc == 2 && QByteArray(argv[1]) == QByteArray("--synthetic")) {
        const auto data = syntheticConformance();
        if (data.isEmpty()) return 1;
        const auto json = QJsonDocument(data).toJson(QJsonDocument::Compact);
        return std::fwrite(json.constData(), 1, std::size_t(json.size()), stdout) == std::size_t(json.size()) ? 0 : 1;
    }
    if (argc != 1) return 2;
    const auto thresholds = frozenTwoScore();
    const QString token = QStringLiteral("ghbdtn");
    QJsonArray outcomes, table;
    QVector<QPair<QString, QString>> unique;
    for (const auto profile : {RouteProfile::SafeConfirmed, RouteProfile::RequestedHybrid}) {
        for (const auto model : {DecisionAction::Keep, DecisionAction::Correct,
                                DecisionAction::Abstain, static_cast<DecisionAction>(99)}) {
            for (int bits = 0; bits < 64; ++bits) {
                Source source;
                source.value = {{bool(bits & 1), bool(bits & 2)},
                                {bool(bits & 4), bool(bits & 8)},
                                {bool(bits & 16), bool(bits & 32)}, model};
                const auto d = route({FieldContext::Prose, InputLayout::KnownUs, QStringView(token)}, profile, &source);
                if (!d.evidenceQueried) return 1;
                const QPair<QString, QString> key{QString::fromLatin1(routeActionName(d.action)),
                                                QString::fromLatin1(routeReasonName(d.reason))};
                auto index = unique.indexOf(key);
                if (index < 0) {
                    index = unique.size();
                    unique.append(key);
                    outcomes.append(QJsonObject{{QStringLiteral("action"), key.first},
                                                {QStringLiteral("reason"), key.second}});
                }
                table.append(int(index));
            }
        }
    }
    const QJsonObject data{
        {QStringLiteral("schema"), 1},
        {QStringLiteral("modelSpec"),QJsonObject{
            {QStringLiteral("magic"),QStringLiteral("RSKN")},
            {QStringLiteral("version"),int(NgramModel::kFormatVersion)},
            {QStringLiteral("scale"),NgramModel::kScale},{QStringLiteral("alphabet"),NgramModel::kAlphabet},
            {QStringLiteral("letterKeys"),NgramModel::kLetterKeys},{QStringLiteral("bos"),NgramModel::kBos},
            {QStringLiteral("eos"),NgramModel::kEos},{QStringLiteral("tableSize"),NgramModel::kTableSize},
            {QStringLiteral("headerBytes"),NgramModel::kHeaderBytes},
            {QStringLiteral("serializedBytes"),NgramModel::kSerializedBytes},
            {QStringLiteral("minQuantized"),NgramModel::kMinQuantized},
            {QStringLiteral("minStepRaw"),NgramModel::kMinStepRaw},
            {QStringLiteral("orderWeights"),QJsonArray{NgramModel::kOrderWeights[0],NgramModel::kOrderWeights[1],NgramModel::kOrderWeights[2]}},
            {QStringLiteral("minTokenKeys"),kMinTokenKeys},{QStringLiteral("maxTokenKeys"),kMaxTokenKeys},
            {QStringLiteral("minDecisionKeys"),kMinDecisionKeys},
            {QStringLiteral("twoScore"),QJsonObject{{QStringLiteral("enabled"),thresholds.enabled},
                {QStringLiteral("minAlternative"),thresholds.minAlternative},
                {QStringLiteral("maxObserved"),thresholds.maxObserved},{QStringLiteral("gap"),thresholds.gap}}}}},
        {QStringLiteral("minKeys"), kMinRouteKeys}, {QStringLiteral("maxKeys"), kMaxRouteKeys},
        {QStringLiteral("canonical"), QJsonObject{
            {QStringLiteral("us"), canonicalMap(SourceLayout::Us)},
            {QStringLiteral("ru"), canonicalMap(SourceLayout::Ru)}}},
        {QStringLiteral("profiles"), QJsonArray{QStringLiteral("safeConfirmed"), QStringLiteral("requestedHybrid")}},
        {QStringLiteral("models"), QJsonArray{QStringLiteral("keep"), QStringLiteral("correct"),
                                            QStringLiteral("abstain"), QStringLiteral("invalid")}},
        {QStringLiteral("outcomes"), outcomes}, {QStringLiteral("table"), table}};
    const QByteArray json = QJsonDocument(data).toJson(QJsonDocument::Compact);
    return std::fwrite(json.constData(), 1, std::size_t(json.size()), stdout) == std::size_t(json.size()) ? 0 : 1;
}
