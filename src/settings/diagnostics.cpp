#include "diagnostics.h"
#include "git/gitclient.h"
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>
#include <QSysInfo>
#include <QSet>

namespace {
QString safeCommand(const QString &name) {
    static const QSet<QString> commands={"add","am","apply","archive","bisect","blame","branch","bundle","cat-file","check-ignore","check-ref-format","checkout","cherry-pick","clean","clone","commit","config","count-objects","describe","diff","diff-tree","fetch","for-each-ref","format-patch","fsck","gc","hash-object","init","lfs","log","ls-files","ls-remote","ls-tree","maintenance","merge","merge-base","mv","notes","pull","push","read-tree","rebase","reflog","remote","repack","reset","restore","rev-list","rev-parse","revert","rm","show","show-ref","stash","status","submodule","switch","symbolic-ref","tag","update-index","update-ref","var","version","worktree"};
    return commands.contains(name)?name:QString("other");
}
bool recent(const QJsonObject &entry) {
    const auto date=QDateTime::fromString(entry.value("time").toString(),Qt::ISODateWithMs);
    const auto age=date.secsTo(QDateTime::currentDateTimeUtc());
    return date.isValid()&&age>=0&&age<=7*24*60*60;
}
QJsonObject restoredEntry(const QJsonObject &entry) {
    // A retained file is untrusted. Reconstruct only schema fields, never copy text.
    if(!recent(entry))return {};
    QJsonObject clean{{"time",entry.value("time").toString()}};
    if(entry.value("kind")=="ui") {clean.insert("kind","ui");clean.insert("action","ui_action");return clean;}
    if(entry.value("kind")!="git")return {};
    const int exitCode=entry.value("exit_code").toInt(-1);
    const bool success=entry.value("success").toBool()&&(exitCode==0||(exitCode==1&&entry.value("command")=="diff"));
    auto result=Diagnostics::classify(success?QString():QString("failure"),success?0:exitCode,!success&&entry.value("data_may_have_changed").toBool());result.insert("exit_code",exitCode);
    result.insert("kind","git");result.insert("time",clean.value("time"));result.insert("command",safeCommand(entry.value("command").toString()));
    result.insert("success",success);result.insert("elapsed_ms",qBound(qint64(0),entry.value("elapsed_ms").toInteger(),qint64(86400000)));return result;
}
}

Diagnostics::Diagnostics(GitClient *git,QObject *parent,const QString &storageDirectory):QObject(parent){
    directory_=storageDirectory.isEmpty()?QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)+"/diagnostics":storageDirectory;
    if(QSettings().value("diagnostics/retain",false).toBool()){
        QFile file(directory_+"/recent.json");
        if(file.open(QIODevice::ReadOnly)&&file.size()<=1024*1024){
            const auto values=QJsonDocument::fromJson(file.readAll()).object().value("entries").toArray();
            for(const auto &value:values){const auto clean=restoredEntry(value.toObject());if(!clean.isEmpty())entries_.append(clean);}
        }
        file.close();while(entries_.size()>500)entries_.removeAt(0);
        if(entries_.isEmpty())QFile::remove(file.fileName());else persist();
    }
    connect(git,&GitClient::commandFinished,this,[this](const QString &command,bool ok,int exitCode,qint64 elapsed,const QString &error,bool writes){
        auto entry=classify(ok?QString():error,ok?0:exitCode,!ok&&writes);entry.insert("exit_code",exitCode);entry.insert("kind","git");entry.insert("command",safeCommand(command));entry.insert("success",ok);entry.insert("elapsed_ms",elapsed);append(entry);
    });
}
QString Diagnostics::redact(QString text){
    text.replace(QRegularExpression("(?is)-----BEGIN [^-]*PRIVATE KEY-----.*?(?:-----END [^-]*PRIVATE KEY-----|$)"),"<private key redacted>");
    text.replace(QRegularExpression("(?i)\\b(?:gh[pousr]_[A-Za-z0-9_]+|github_pat_[A-Za-z0-9_]+|glpat-[A-Za-z0-9_-]+)"),"<token>");
    text.replace(QRegularExpression("(?i)(authorization\\s*[:=]\\s*)(?:bearer|basic)\\s+[^\\s]+"),"\\1<redacted>");
    text.replace(QRegularExpression("(?i)((?:access[_-]?token|token|password|passwd|secret|client_secret|private_token)\\s*[=:]\\s*)[^\\s&;]+"),"\\1<redacted>");
    text.replace(QRegularExpression("(https?://)[^/\\s@]+@"),"\\1<credentials>@");
    text.replace(QRegularExpression("[A-Za-z0-9._%+-]+@[A-Za-z0-9.-]+\\.[A-Za-z]{2,}"),"<email>");
    const auto home=QDir::homePath();if(!home.isEmpty()){text.replace(home,"<home>",Qt::CaseInsensitive);text.replace(QDir::toNativeSeparators(home),"<home>",Qt::CaseInsensitive);}
    return text;
}
QJsonObject Diagnostics::classify(const QString &error,int exitCode,bool changed){
    // Hints, not a claim about the cause: Git messages vary with version/locale.
    const auto lower=error.toLower();QString code="git_failure",next="Inspect the repository state and the operation log before retrying.";
    if(exitCode==0&&error.isEmpty()){code="success";next.clear();}
    else if(exitCode<0){code="process_unavailable";next="Check the Git executable and PATH in Git environment settings.";}
    else if(lower.contains("authentication")||lower.contains("permission denied")||lower.contains("401")){code="authentication_or_permission";next="Check the active account, credentials, repository access, and organization SSO.";}
    else if(lower.contains("conflict")||lower.contains("충돌")){code="possible_conflict";next="Read operation state; resolve conflicts before Continue, or review Abort.";}
    else if(lower.contains("lock")){code="possible_lock";next="Check for another Git process before inspecting repository locks.";}
    else if(lower.contains("resolve host")||lower.contains("connection")||lower.contains("timed out")){code="network_or_timeout";next="Check the network and remote address. Read remote state before retrying a write.";}
    return {{"code",code},{"exit_code",exitCode},{"data_may_have_changed",changed},{"recovery",next}};
}
void Diagnostics::append(QJsonObject entry){QJsonArray kept;for(const auto &value:entries_)if(recent(value.toObject()))kept.append(value);entries_=kept;entry.insert("time",QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));entries_.append(entry);while(entries_.size()>500)entries_.removeAt(0);persist();emit changed();}
void Diagnostics::recordAction(const QString &identifier){
    // Identifiers only: never persist button text, command arguments, paths or user input.
    const auto safe=QRegularExpression("^[A-Za-z][A-Za-z0-9_]{0,79}$").match(identifier).hasMatch()&&redact(identifier)==identifier?identifier:QString("ui_action");append({{"kind","ui"},{"action",safe}});
}
QJsonObject Diagnostics::report()const{QJsonArray kept;for(const auto &entry:entries_)if(recent(entry.toObject()))kept.append(entry);auto language=QSettings().value("ui/language","system").toString();if(language!="ko"&&language!="en")language="system";return {{"schema_version",1},{"application","GitCanvas"},{"version",QCoreApplication::applicationVersion()},{"qt_version",qVersion()},{"os",QSysInfo::productType()},{"os_version",QSysInfo::productVersion()},{"architecture",QSysInfo::currentCpuArchitecture()},{"language",language},{"saved_at",QDateTime::currentDateTimeUtc().toString(Qt::ISODate)},{"entries",kept}};}
bool Diagnostics::exportReport(const QString &path,QString *error)const{QSaveFile file(path);if(!file.open(QIODevice::WriteOnly)){if(error)*error=file.errorString();return false;}const auto bytes=QJsonDocument(report()).toJson();if(file.write(bytes)!=bytes.size()||!file.commit()){if(error)*error=file.errorString();return false;}return true;}
void Diagnostics::persist(){if(!QSettings().value("diagnostics/retain",false).toBool())return;QDir().mkpath(directory_);QString error;exportReport(directory_+"/recent.json",&error);if(storageError_!=error){storageError_=error;emit storageErrorChanged();}}
void Diagnostics::removeStored(){QFile file(directory_+"/recent.json");QString error;if(file.exists()&&!file.remove())error=file.errorString();if(storageError_!=error){storageError_=error;emit storageErrorChanged();}}
void Diagnostics::clear(){entries_={};removeStored();emit changed();}
void Diagnostics::setRetention(bool enabled){QSettings().setValue("diagnostics/retain",enabled);if(enabled)persist();else removeStored();}
