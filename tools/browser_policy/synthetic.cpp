// SPDX-License-Identifier: MIT
// Entirely fabricated numeric weights and key sequences; no natural corpus.
#include "synthetic.h"
#include "decisionrouter.h"
#include "../frontcodedlexicon.h"
#include <QByteArray>
#include <QJsonArray>
#include <QString>
#include <cstdint>
using namespace rasklad;
namespace {
void u32(QByteArray &out, std::uint32_t value) {
    for (int shift = 0; shift < 32; shift += 8) out.append(char((value >> shift) & 255U));
}
QByteArray bytes(int en, int ru, bool varied) {
    QByteArray out("RSKN",4);
    for (auto value : {NgramModel::kFormatVersion, quint32(NgramModel::kScale),
                      quint32(NgramModel::kAlphabet), quint32(NgramModel::kTableSize)}) u32(out,value);
    std::uint32_t state = 0x12345678U;
    for (int language = 0; language < 2; ++language)
        for (int i = 0; i < NgramModel::kTableSize; ++i) {
            state = state * 1664525U + 1013904223U;
            const int value = varied ? -1-int((state >> 24) % 127) : (language == 0 ? en : ru);
            out.append(char(value));
        }
    return out;
}
QString action(const TokenScore &score, SourceLayout layout) {
    const auto a = decideTwoScore(score, layout, routing::frozenTwoScore()).action;
    if (a == DecisionAction::Keep) return QStringLiteral("keep");
    if (a == DecisionAction::Correct) return QStringLiteral("correct");
    return QStringLiteral("abstain");
}
QJsonObject expected(const TokenScore &s) {
    return {{QStringLiteral("ok"),s.ok},{QStringLiteral("keyCount"),s.keyCount},
            {QStringLiteral("steps"),s.steps},
            {QStringLiteral("raw"),QJsonArray{s.raw[0],s.raw[1]}}};
}
QJsonObject scoreCase(const NgramModel &model, const KeySequence &keys) {
    QJsonArray list; for (char k : keys) list.append(static_cast<unsigned char>(k));
    const auto s=model.score(keys);
    return {{QStringLiteral("keys"),list},{QStringLiteral("score"),expected(s)},
            {QStringLiteral("us"),action(s,SourceLayout::Us)},
            {QStringLiteral("ru"),action(s,SourceLayout::Ru)}};
}
}

namespace {
QJsonArray units(const QString &text) {
    QJsonArray out;for(QChar ch:text)out.append(int(ch.unicode()));return out;
}
QJsonObject pairJson(const routing::PairHit &p) {
    return {{QStringLiteral("observed"),p.observed},{QStringLiteral("alternative"),p.alternative}};
}
class OracleSource final:public routing::EvidenceSource {
public:
    routing::TokenEvidence value;
    std::optional<routing::TokenEvidence> evidence(const routing::EvidenceQuery &)const override{return value;}
};
QJsonArray routerOracle() {
    using namespace routing;
    QJsonArray out;
    const QString token=QStringLiteral("Ghbdtn");
    for(auto profile:{RouteProfile::SafeConfirmed,RouteProfile::RequestedHybrid})
    for(auto model:{DecisionAction::Keep,DecisionAction::Correct,DecisionAction::Abstain,static_cast<DecisionAction>(99)})
    for(bool fo:{false,true})for(bool fa:{false,true})
    for(bool co:{false,true})for(bool ca:{false,true})
    for(bool so:{false,true})for(bool sa:{false,true}) {
        OracleSource source;source.value={{fo,fa},{co,ca},{so,sa},model};
        const auto d=route({FieldContext::Prose,InputLayout::KnownUs,QStringView(token)},profile,&source);
        const auto modelName=model==DecisionAction::Keep?QStringLiteral("keep"):model==DecisionAction::Correct?
            QStringLiteral("correct"):model==DecisionAction::Abstain?QStringLiteral("abstain"):QStringLiteral("invalid");
        out.append(QJsonObject{
            {QStringLiteral("profile"),profile==RouteProfile::SafeConfirmed?QStringLiteral("safeConfirmed"):QStringLiteral("requestedHybrid")},
            {QStringLiteral("token"),token},
            {QStringLiteral("evidence"),QJsonObject{{QStringLiteral("full"),pairJson(source.value.full)},
                {QStringLiteral("control"),pairJson(source.value.control)},{QStringLiteral("stem"),pairJson(source.value.stem)},
                {QStringLiteral("model"),modelName}}},
            {QStringLiteral("expected"),QJsonObject{{QStringLiteral("action"),QString::fromLatin1(routeActionName(d.action))},
                {QStringLiteral("reason"),QString::fromLatin1(routeReasonName(d.reason))},
                {QStringLiteral("evidenceQueried"),d.evidenceQueried},{QStringLiteral("candidate"),d.candidate}}}});
    }
    return out;
}
QJsonArray converterOracle() {
    QJsonArray out;
    QString ascii;for(int i=33;i<=126;++i)ascii.append(QChar(i));
    QString surrogate;for(char16_t ch:{u'a',char16_t(0xd800),u'b',char16_t(0xdc00)})surrogate.append(QChar(ch));
    const QStringList texts{QString(),ascii,QStringLiteral("Ghbdtn, Vbh!"),QStringLiteral("Привет, Мир!"),
        QString::fromUtf16(u"a\u0301a\U0001D165"),QString::fromUtf16(u"#\ufe0f\u20e3"),
        QString::fromUtf16(u"a\U0001f600b"),surrogate,QStringLiteral(" \n\t ")};
    for(const auto &text:texts)for(auto direction:{LayoutDirection::UsToRu,LayoutDirection::RuToUs,static_cast<LayoutDirection>(99)})
        out.append(QJsonObject{{QStringLiteral("source"),units(text)},
            {QStringLiteral("direction"),direction==LayoutDirection::UsToRu?QStringLiteral("us-ru"):
                direction==LayoutDirection::RuToUs?QStringLiteral("ru-us"):QStringLiteral("invalid")},
            {QStringLiteral("expected"),units(convertLayout(text,direction))}});
    return out;
}
}


namespace {
QJsonArray keyArray(const KeySequence &key) {
    QJsonArray out;for(char k:key)out.append(int(static_cast<unsigned char>(k)));return out;
}
QJsonObject lexiconCase(const QByteArray &bytes,const std::vector<KeySequence> &queries) {
    const auto loaded=benchmark::FrontCodedLexicon::deserialize(bytes);QJsonArray checks;
    if(loaded)for(const auto &key:queries)checks.append(QJsonObject{{QStringLiteral("keys"),keyArray(key)},
        {QStringLiteral("expected"),loaded->contains(key)}});
    return {{QStringLiteral("bytesBase64"),QString::fromLatin1(bytes.toBase64())},
        {QStringLiteral("accepted"),bool(loaded)},{QStringLiteral("checks"),checks}};
}
QJsonObject lexiconOracle() {
    QJsonArray cases,mutations;int fixtureIndex=0;
    std::vector<KeySequence> queries{{},KeySequence(33,char(0)),KeySequence(4,char(33)),KeySequence(4,char(255))};
    for(int i=0;i<120;++i){KeySequence key;for(int j=0;j<1+i%32;++j)key.append(char((i*7+j*3)%33));queries.push_back(key);}
    QByteArray mutationBase;
    for(int count:{0,1,16,17,33,257}){
        std::vector<KeySequence> keys;
        for(int i=0;i<count;++i){KeySequence key;key.append(char(i/33));key.append(char(i%33));
            for(int j=0;j<i%5;++j){key.append(char(j));}
            keys.push_back(key);}
        const auto built=benchmark::FrontCodedLexicon::build(keys);
        if(!built)return {};
        auto checks=queries;checks.insert(checks.end(),keys.begin(),keys.end());
        const auto raw=built->serialize();cases.append(lexiconCase(raw,checks));
        if(count==17)mutationBase=raw;
    }
    std::vector<KeySequence> longest{KeySequence(1,char(0)),KeySequence(32,char(0)),KeySequence(32,char(32))};
    const auto longBuilt=benchmark::FrontCodedLexicon::build(longest);if(!longBuilt)return {};
    cases.append(lexiconCase(longBuilt->serialize(),longest));
    // Actual fixture pair tests stem ambiguity on fictional physical keys only.
    std::vector<KeySequence> stems{canonicalizeToken(QStringView(u"qzxv"),SourceLayout::Us).keys};
    const auto fixture=benchmark::FrontCodedLexicon::build(stems);if(!fixture)return {};
    fixtureIndex=cases.size();cases.append(lexiconCase(fixture->serialize(),stems));
    for(qsizetype offset=0;offset<mutationBase.size();++offset)for(unsigned int value:{0U,32U,33U,255U}){
        auto raw=mutationBase;raw[offset]=char(value);mutations.append(lexiconCase(raw,queries));
    }
    for(qsizetype size:{qsizetype(0),qsizetype(1),qsizetype(23),mutationBase.size()-1})mutations.append(lexiconCase(mutationBase.left(size),queries));
    auto extra=mutationBase;extra.append(char(0));mutations.append(lexiconCase(extra,queries));
    return {{QStringLiteral("cases"),cases},{QStringLiteral("mutations"),mutations},{QStringLiteral("fixtureIndex"),fixtureIndex}};
}
}

QJsonObject syntheticConformance() {
    QJsonArray models;
    const int constants[7][2]={{-50,-50},{-32,-16},{-16,-32},{-24,-24},{0,0},{-127,-127},{-32,-16}};
    for (int id = 0; id < 7; ++id) {
        auto raw = bytes(constants[id][0],constants[id][1],id==0);
        if (id==6) {
            // Make the dictionary-only fixture token uninformative for the model.
            const auto keys = canonicalizeToken(QStringView(u"abcd"), SourceLayout::Us).keys;
            for (int language=0;language<2;++language) {
                const int offset=NgramModel::kHeaderBytes+language*NgramModel::kTableSize;
                int a=NgramModel::kBos,b=NgramModel::kBos;
                for (qsizetype i=0;i<=keys.size();++i) {
                    const int x=i<keys.size()?static_cast<unsigned char>(keys.at(i)):NgramModel::kEos;
                    for (int index : {x,NgramModel::kUnigramSize+b*NgramModel::kAlphabet+x,
                         NgramModel::kUnigramSize+NgramModel::kBigramSize+(a*NgramModel::kAlphabet+b)*NgramModel::kAlphabet+x})
                        raw[offset+index]=char(-24);
                    a=b;b=x;
                }
            }
        }
        const auto model=NgramModel::deserialize(raw);
        if (!model) return {};
        QJsonArray cases;
        std::uint32_t state=0x87654321U;
        for(int n=1;n<=kMaxTokenKeys;++n) for(int pattern=0;pattern<5;++pattern) {
            KeySequence keys;
            for(int i=0;i<n;++i) {
                state=state*1664525U+1013904223U;
                const int key=pattern==0?0:pattern==1?32:pattern==2?(i%2?32:0):pattern==3?(i*7+n)%33:int(state%33);
                keys.append(char(key));
            }
            cases.append(scoreCase(*model,keys));
        }
        for(const auto &invalid : {KeySequence(), KeySequence(33,char(0)),
                                  KeySequence(4,char(33)), KeySequence(4,char(255))})
            cases.append(scoreCase(*model,invalid));
        for(const auto &token:{QStringLiteral("ghbdtn"),QStringLiteral("abcd"),QStringLiteral("hello"),QStringLiteral("test")})
            cases.append(scoreCase(*model,canonicalizeToken(QStringView(token),SourceLayout::Us).keys));
        models.append(QJsonObject{{QStringLiteral("id"),id},
            {QStringLiteral("bytesBase64"),QString::fromLatin1(raw.toBase64())},{QStringLiteral("cases"),cases}});
    }
    QJsonArray boundaries;
    const int values[][2]={{-992,-832},{-991,-832},{-576,-416},{-575,-575},{-576,-576},{0,0},{1,-100},{-3049,-100}};
    for(const auto &raw:values) {
        TokenScore s;s.ok=true;s.keyCount=7;s.steps=8;s.raw={raw[0],raw[1]};
        boundaries.append(QJsonObject{{QStringLiteral("score"),expected(s)},
            {QStringLiteral("us"),action(s,SourceLayout::Us)},{QStringLiteral("ru"),action(s,SourceLayout::Ru)}});
    }
    return {{QStringLiteral("schema"),1},{QStringLiteral("models"),models},
            {QStringLiteral("boundaries"),boundaries},{QStringLiteral("fixtureModelIndex"),6},
            {QStringLiteral("lexicons"),lexiconOracle()},
            {QStringLiteral("routerCases"),routerOracle()},{QStringLiteral("converterCases"),converterOracle()}};
}
