#pragma once
#include "git/gitclient.h"
#include <QList>

struct RebaseEntry {QString oid,subject,message,action="pick";};
struct RebasePlan {QString repository,gitDirectory,head,base,fingerprint;bool root=false;QList<RebaseEntry> entries;};
struct RecoveryEntry {QString ref,oid,date,subject;bool backup=false;};
class RewriteController final:public QObject {
    Q_OBJECT
public:
    explicit RewriteController(GitClient *git,QObject *parent=nullptr):QObject(parent),git_(git){}
    void plan(const QString &base,bool root,std::function<void(bool,RebasePlan,QString)> callback);
    void start(const RebasePlan &plan,GitClient::CommandCallback callback);
    void recoveryLog(int limit,std::function<void(bool,QList<RecoveryEntry>,QString)> callback);
    void recover(const RecoveryEntry &entry,const QString &action,const QString &branch,
                 const QString &expectedRepository,const QString &fingerprint,GitClient::CommandCallback callback);
private:
    GitClient *git_;
};
