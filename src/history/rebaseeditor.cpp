#include "rebaseeditor.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcessEnvironment>
#include <QSaveFile>
#include <QSet>

namespace {
QByteArray read(const QString &path){QFile file(path);return file.open(QIODevice::ReadOnly)?file.readAll():QByteArray();}
QString quote(QString value){value.replace('\\','/');return "'"+value.replace("'","'\\''")+"'";}
}
void configureRebaseEditor(QProcessEnvironment &environment,const QString &directory,bool starting){
    const auto planPath=QDir(directory).filePath("gitcanvas/rebase-active.json");
    const auto plan=QJsonDocument::fromJson(read(planPath)).object();if(plan.value("version").toInt()!=1)return;
    if(!starting&&QString::fromUtf8(read(QDir(directory).filePath("rebase-merge/orig-head"))).trimmed()!=plan.value("head").toString())return;
    const auto command=quote(QCoreApplication::applicationFilePath())+" --gitcanvas-rebase-editor "+quote(planPath);
    environment.insert("GIT_SEQUENCE_EDITOR",command+" sequence");environment.insert("GIT_EDITOR",command+" message");
}
void finishRebaseEditor(const QString &directory){
    if(directory.isEmpty()||QFileInfo::exists(QDir(directory).filePath("rebase-merge"))||QFileInfo::exists(QDir(directory).filePath("rebase-apply")))return;
    QFile::remove(QDir(directory).filePath("gitcanvas/rebase-active.json"));
}
int runRebaseEditor(const QStringList &args){
    if(args.size()!=5||args[1]!="--gitcanvas-rebase-editor")return 2;
    const auto plan=QJsonDocument::fromJson(read(args[2])).object();if(plan.value("version").toInt()!=1)return 3;
    const auto directory=QFileInfo(plan.value("gitDirectory").toString()).canonicalFilePath();
    const auto target=QFileInfo(args[4]).canonicalFilePath();
    if(directory.isEmpty()||target.isEmpty()||!QDir::fromNativeSeparators(target).startsWith(QDir::fromNativeSeparators(directory)+"/",Qt::CaseInsensitive))return 4;
    const auto entries=plan.value("entries").toArray();QByteArray output;
    if(args[3]=="sequence"){
        if(QFileInfo(target).fileName()!="git-rebase-todo")return 5;
        QSet<QString> generated;
        for(const auto &line:read(target).split('\n')){if(line.startsWith('#')||line.trimmed().isEmpty())continue;const auto words=line.simplified().split(' ');if(words.size()<2||words[0]!="pick")return 6;QString found;for(const auto &entry:entries){const auto oid=entry.toObject().value("oid").toString();if(oid.startsWith(QString::fromLatin1(words[1]))){if(!found.isEmpty())return 7;found=oid;}}if(found.isEmpty()||generated.contains(found))return 8;generated.insert(found);}
        if(generated.size()!=entries.size())return 9;
        for(const auto &value:entries){const auto e=value.toObject();const auto action=e.value("action").toString();if(!QStringList{"pick","reword","edit","squash","fixup","drop"}.contains(action))return 10;output+=action.toUtf8()+" "+e.value("oid").toString().toLatin1()+"\n";}
    }else if(args[3]=="message"){
        const auto lines=read(QDir(directory).filePath("rebase-merge/done")).trimmed().split('\n');const auto words=lines.last().simplified().split(' ');
        if(words.size()>=2&&words[0]=="reword"){
            for(const auto &value:entries){const auto entry=value.toObject();if(entry.value("oid").toString().startsWith(QString::fromLatin1(words[1]))){output=entry.value("message").toString().toUtf8();break;}}
            if(output.trimmed().isEmpty())return 11;
        }else return 0; // Squash keeps Git's combined message; no terminal editor is launched.
    }else return 12;
    QSaveFile file(target);if(!file.open(QIODevice::WriteOnly)||file.write(output)!=output.size()||!file.commit())return 13;return 0;
}
