// SPDX-License-Identifier: MIT
// Entirely fabricated numeric weights and key sequences; no natural corpus.
#include "synthetic.h"
#include "decisionrouter.h"
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
            {QStringLiteral("boundaries"),boundaries},{QStringLiteral("fixtureModelIndex"),6}};
}
