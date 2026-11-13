// SPDX-License-Identifier: MIT
// Only original tiny fabricated .aff/.dic are written/read; no real data.
#include "profileverify.h"
#include <hunspell/hunspell.h>
#include <hunspell/hunversion.h>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QTemporaryDir>
#include <QFile>
#include <QSet>
#include <cstdio>

namespace {
bool write(const QString &name,const QByteArray &data){QFile f(name);return f.open(QIODevice::WriteOnly)&&f.write(data)==data.size();}
QJsonObject profile(const QByteArray &aff,const QByteArray &dic,const QStringList &words){
    QTemporaryDir directory;if(!directory.isValid())return {};
    QByteArray control;if(!rasklad::profile::removePfxSfxRows(aff,control))return {};
    const auto a=directory.filePath(QStringLiteral("full.aff")),c=directory.filePath(QStringLiteral("control.aff")),d=directory.filePath(QStringLiteral("words.dic"));
    if(!write(a,aff)||!write(c,control)||!write(d,dic))return {};
    auto *full=Hunspell_create(QFile::encodeName(a).constData(),QFile::encodeName(d).constData());
    auto *plain=Hunspell_create(QFile::encodeName(c).constData(),QFile::encodeName(d).constData());
    if(!full||!plain){if(full)Hunspell_destroy(full);if(plain)Hunspell_destroy(plain);return {};}
    QJsonArray cases;QSet<QString> used;
    for(const auto &base:words)for(const auto &word:{base,base.toUpper(),base.left(1).toUpper()+base.mid(1)}){
        if(used.contains(word)){continue;}
        used.insert(word);const auto token=word.toUtf8();
        cases.append(QJsonObject{{QStringLiteral("token"),word},{QStringLiteral("full"),Hunspell_spell(full,token.constData())!=0},
            {QStringLiteral("control"),Hunspell_spell(plain,token.constData())!=0}});
    }
    Hunspell_destroy(full);Hunspell_destroy(plain);
    return {{QStringLiteral("affBase64"),QString::fromLatin1(aff.toBase64())},
        {QStringLiteral("dicBase64"),QString::fromLatin1(dic.toBase64())},
        {QStringLiteral("controlBase64"),QString::fromLatin1(control.toBase64())},{QStringLiteral("cases"),cases}};
}
}
int main(){
    const QByteArray common("SET UTF-8\nKEEPCASE K\nNEEDAFFIX N\nFORBIDDENWORD F\nONLYINCOMPOUND O\nCOMPOUNDFLAG C\nCOMPOUNDMIN 3\n");
    const auto en=profile(common+QByteArray("PFX R Y 1\nPFX R 0 re .\nSFX S Y 1\nSFX S 0 s .\n"),
        QByteArray("9\nbook/RS\niPhone/K\nstem/NS\nbanned/F\nrain/C\nbow/C\nonly/OC\nhello\nworld\n"),
        {QStringLiteral("book"),QStringLiteral("books"),QStringLiteral("rebook"),QStringLiteral("rebooks"),
         QStringLiteral("iPhone"),QStringLiteral("iphone"),QStringLiteral("stem"),QStringLiteral("stems"),
         QStringLiteral("banned"),QStringLiteral("rain"),QStringLiteral("rainbow"),QStringLiteral("only"),
         QStringLiteral("rainonly"),QStringLiteral("hello"),QStringLiteral("world"),QStringLiteral("ghbdtn"),QStringLiteral("qzxv")});
    const auto ru=profile(common+QStringLiteral("PFX R Y 1\nPFX R 0 пере .\nSFX S Y 1\nSFX S 0 ы .\n").toUtf8(),
        QStringLiteral("9\nкот/RS\nАйФон/K\nкорень/NS\nзапрет/F\nдождь/C\nлес/C\nтолько/OC\nпривет\nвода\n").toUtf8(),
        {QStringLiteral("кот"),QStringLiteral("коты"),QStringLiteral("перекот"),QStringLiteral("перекоты"),
         QStringLiteral("АйФон"),QStringLiteral("айфон"),QStringLiteral("корень"),QStringLiteral("кореньы"),
         QStringLiteral("запрет"),QStringLiteral("дождьлес"),QStringLiteral("только"),QStringLiteral("дождьтолько"),
         QStringLiteral("привет"),QStringLiteral("вода"),QStringLiteral("модельнет")});
    if(en.isEmpty()||ru.isEmpty())return 1;
    QJsonArray rows;
    for(const auto &raw:{QByteArray("SET UTF-8\nPFX R Y 1\nPFX R 0 re .\nKEEPCASE K\n"),
        QByteArray("SET UTF-8\r\n \tSFX S Y 1\r\nSFX S 0 s .\r\nNEEDAFFIX N"),
        QByteArray("\xef\xbb\xbfSET UTF-8\nSFX S Y 1\nSFX S 0 s .\n"),QByteArray("PFXbad X\n"),
        QByteArray("SET UTF-8\n"),QByteArray("\xef\xbb\xbfPFX R Y 1\n"),QByteArray("PFX\0bad",7)}){
        QByteArray out;const bool accepted=rasklad::profile::removePfxSfxRows(raw,out);
        rows.append(QJsonObject{{QStringLiteral("sourceBase64"),QString::fromLatin1(raw.toBase64())},
            {QStringLiteral("accepted"),accepted},{QStringLiteral("expectedBase64"),QString::fromLatin1(out.toBase64())}});
    }
    const QJsonObject result{{QStringLiteral("schema"),1},{QStringLiteral("nativeVersion"),QString::fromLatin1(Hunspell_get_library_version())},
        {QStringLiteral("profiles"),QJsonObject{{QStringLiteral("en"),en},{QStringLiteral("ru"),ru}}},{QStringLiteral("controlCases"),rows}};
    const auto json=QJsonDocument(result).toJson(QJsonDocument::Compact);
    return std::fwrite(json.constData(),1,std::size_t(json.size()),stdout)==std::size_t(json.size())?0:1;
}
