#pragma once
#include "git/gitclient.h"
#include <array>

struct MergePreview { QString repository, ref, head, target, fingerprint; QByteArray comparison; };
struct ConflictFile {
    QString repository,path,fingerprint,stateFingerprint,gitDirectory,operation;
    std::array<QByteArray,3> sides;
    std::array<bool,3> present{};
    std::array<QString,3> modes;
    QByteArray working;
    bool exists=false,text=false;
};
class MergeController final : public QObject {
    Q_OBJECT
public:
    explicit MergeController(GitClient *git,QObject *parent=nullptr):QObject(parent),git_(git){}
    void preview(const QString &ref,std::function<void(bool,MergePreview,QString)> callback);
    void merge(const MergePreview &expected,GitClient::CommandCallback callback);
    void loadConflict(const QString &path,std::function<void(bool,ConflictFile,QString)> callback);
    void resolve(const ConflictFile &expected,const QString &choice,const QByteArray &edited,GitClient::CommandCallback callback);
private:
    GitClient *git_;
};
