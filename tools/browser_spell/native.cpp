// SPDX-License-Identifier: MIT
// Default mode uses original fictional data. --stdin-profiles is an explicit
// smoke-test mode for bytes already verified by the isolated parent.
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
#include <cstring>

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
int main(int argc, char **argv){
    if(std::strcmp(Hunspell_get_library_version(),"1.7.5")!=0)return 1;
    if(argc==2 && std::strcmp(argv[1],"--stdin-profiles")==0){
        // A pipe may return a short read before EOF. Small fictional profiles
        // hid this; collect bounded chunks until EOF for real dictionary inputs.
        QByteArray bytes;char chunk[65536];std::size_t count;
        while((count=std::fread(chunk,1,sizeof(chunk),stdin))>0){
            if(bytes.size()+qint64(count)>16*1024*1024)return 1;
            bytes.append(chunk,qsizetype(count));
        }
        if(std::ferror(stdin))return 1;
        QJsonParseError error;const auto document=QJsonDocument::fromJson(bytes,&error);
        if(error.error!=QJsonParseError::NoError || !document.isObject())return 1;
        const auto object=document.object();
        if(object.keys()!=QStringList{QStringLiteral("profiles"),QStringLiteral("schema")} || object.value(QStringLiteral("schema")).toDouble()!=1)return 1;
        const auto profiles=object.value(QStringLiteral("profiles")).toObject();
        if(profiles.keys()!=QStringList{QStringLiteral("en"),QStringLiteral("ru")})return 1;
        QJsonObject outputProfiles;
        for(const auto &lang:profiles.keys()){
            const auto entry=profiles.value(lang).toObject();
            if(entry.keys()!=QStringList{QStringLiteral("affBase64"),QStringLiteral("dicBase64"),QStringLiteral("words")})return 1;
            const auto decode=[&](const char *name)->QByteArray{
                const auto value=entry.value(QString::fromLatin1(name));
                if(!value.isString())return {};
                const auto raw=value.toString().toLatin1();
                const auto decoded=QByteArray::fromBase64Encoding(raw,QByteArray::AbortOnBase64DecodingErrors);
                return decoded ? decoded.decoded : QByteArray{};
            };
            const auto aff=decode("affBase64"),dic=decode("dicBase64");
            if(aff.isEmpty() || aff.size()>1024*1024 || dic.isEmpty() || dic.size()>16*1024*1024)return 1;
            const auto array=entry.value(QStringLiteral("words")).toArray();
            if(array.isEmpty() || array.size()>64)return 1;
            QStringList words;
            for(const auto &value:array){
                const auto word=value.toString();
                if(!value.isString() || word.isEmpty() || word.size()>32 || word.contains(QChar::Null))return 1;
                words.append(word);
            }
            const auto result=profile(aff,dic,words);if(result.isEmpty())return 1;
            outputProfiles.insert(lang,result);
        }
        const QJsonObject result{{QStringLiteral("schema"),1},{QStringLiteral("nativeVersion"),QStringLiteral("1.7.5")},{QStringLiteral("profiles"),outputProfiles}};
        const auto bytesOut=QJsonDocument(result).toJson(QJsonDocument::Compact);
        return std::fwrite(bytesOut.constData(),1,std::size_t(bytesOut.size()),stdout)==std::size_t(bytesOut.size())?0:1;
    }
    if(argc!=1)return 1;
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
