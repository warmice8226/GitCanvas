#include "mergecontroller.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStringDecoder>
#include <QUuid>
#include <memory>

namespace {
bool safe(const QString &root,const QString &path){
    if(path.isEmpty()||QDir::isAbsolutePath(path)||path.contains('\\')||path.contains(':')||path.contains(QChar::Null))return false;
    QString absolute=root;for(const auto &part:path.split('/')){if(part.isEmpty()||part==".."||part=="."||part.endsWith('.')||part.endsWith(' ')||part.compare(".git",Qt::CaseInsensitive)==0)return false;absolute=QDir(absolute).filePath(part);const QFileInfo f(absolute);if(f.isSymLink()||f.isJunction())return false;}return true;
}
bool text(const QByteArray &data){QStringDecoder decoder(QStringDecoder::Utf8);decoder(data);return !decoder.hasError()&&!data.contains('\0');}
bool saveFile(const QString &path,const QByteArray &data){QSaveFile file(path);return file.open(QIODevice::WriteOnly)&&file.write(data)==data.size()&&file.commit();}
}
void MergeController::preview(const QString &ref,std::function<void(bool,MergePreview,QString)> callback){
    if(ref.isEmpty()||ref.startsWith('-')||ref.contains(QChar::Null)){callback(false,{},tr("병합할 브랜치나 커밋을 선택하세요."));return;}
    git_->loadOperationState([=,this](bool ok,const QString &error){
        if(!ok){callback(false,{},error);return;}const auto state=git_->operationState();
        if(!state.operation.isEmpty()||!state.conflicts.isEmpty()||!state.locks.isEmpty()){callback(false,{},tr("진행 작업·충돌·잠금을 먼저 해결하세요."));return;}
        git_->inspect({"symbolic-ref","--quiet","HEAD"},[=,this](bool attached,const QByteArray &,const QString &){
        if(!attached){callback(false,{},tr("병합할 로컬 브랜치로 먼저 전환하세요. Detached HEAD에서는 병합하지 않습니다."));return;}
        git_->loadStatus([=,this](bool ok,QList<GitStatusEntry> files,QString error){
            if(!ok||!files.isEmpty()){callback(false,{},ok?tr("변경 파일과 미추적 파일을 먼저 커밋하거나 Stash에 보관하세요."):error);return;}
            git_->inspect({"rev-parse","--verify",ref+"^{commit}"},[=,this](bool ok,const QByteArray &target,const QString &error){
                if(!ok){callback(false,{},error);return;}
                git_->inspect({"rev-parse","--verify","HEAD"},[=,this](bool ok,const QByteArray &head,const QString &error){
                    if(!ok){callback(false,{},error);return;}
                    const auto base=QString::fromUtf8(head.trimmed()),other=QString::fromUtf8(target.trimmed());
                    git_->inspectLimited({"log","--left-right","--oneline","--max-count=100",base+"..."+other},[=,this](bool ok,const QByteArray &log,const QString &error){
                        if(!ok){callback(false,{},error);return;}
                        git_->inspectLimited({"diff","--no-ext-diff","--no-textconv","--no-color","--stat","--patch",base,other},[=](bool ok,const QByteArray &diff,const QString &error){
                            callback(ok,{state.repository,ref,base,other,state.fingerprint,QObject::tr("< 현재에만 있는 커밋 / > 대상에만 있는 커밋 (최대 100개)\n").toUtf8()+log+"\n"+diff},error);
                        });
                    });
                });
            });
        });
        });
    });
}
void MergeController::merge(const MergePreview &expected,GitClient::CommandCallback callback){
    if(expected.repository!=git_->repositoryPath()){callback(false,{},tr("저장소가 바뀌었습니다."));return;}
    preview(expected.ref,[=,this](bool ok,MergePreview current,QString error){
        if(!ok){callback(false,{},error);return;}
        if(current.head!=expected.head||current.target!=expected.target||current.fingerprint!=expected.fingerprint){callback(false,{},tr("비교 이후 브랜치·인덱스 상태가 바뀌었습니다. 다시 비교하세요."));return;}
        const auto backup="refs/gitcanvas/merge-backups/"+QUuid::createUuid().toString(QUuid::WithoutBraces);
        git_->inspect({"update-ref",backup,current.head},[=,this](bool ok,const QByteArray &,const QString &error){
            if(!ok){callback(false,{},error);return;}
            git_->inspect({"merge","--ff","--no-edit","--no-autostash","--no-overwrite-ignore",current.target},[=,this](bool ok,const QByteArray &out,const QString &error){
                git_->loadOperationState([=](bool checked,const QString &stateError){callback(ok&&checked,out+"\n"+backup.toUtf8(),ok&&checked?QString():error+"\n"+stateError+QObject::tr("\n병합 전 HEAD: ")+backup+QObject::tr("\n충돌이 있으면 해결·Stage한 뒤 계속하세요. Abort는 작업 상태 · 복구 탭에서 실행할 수 있습니다."));});
            });
        });
    });
}
void MergeController::loadConflict(const QString &path,std::function<void(bool,ConflictFile,QString)> callback){
    const auto repository=git_->repositoryPath();
    if(!safe(repository,path)){callback(false,{},tr("저장소 내부의 일반 파일만 편집할 수 있습니다."));return;}
    git_->loadOperationState([=,this](bool ok,const QString &error){
        if(!ok){callback(false,{},error);return;}const auto state=git_->operationState();
        if(!state.conflicts.contains(path)||!state.locks.isEmpty()){callback(false,{},tr("선택한 충돌이 없거나 Git 잠금이 있습니다. 목록을 새로고침하세요."));return;}
        git_->inspect({"--literal-pathspecs","ls-files","--unmerged","-z","--",path},[=,this](bool ok,const QByteArray &out,const QString &error){
            if(!ok){callback(false,{},error);return;}
            auto file=std::make_shared<ConflictFile>();file->repository=repository;file->path=path;file->gitDirectory=state.gitDirectory;file->operation=state.operation;file->stateFingerprint=state.fingerprint;
            std::array<QString,3> oids;
            for(const auto &entry:out.split('\0')){if(entry.isEmpty())continue;const auto tab=entry.indexOf('\t');const auto fields=entry.left(tab).split(' ');if(fields.size()!=3||QString::fromUtf8(entry.mid(tab+1))!=path){callback(false,{},tr("충돌 인덱스 형식을 읽을 수 없습니다."));return;}const auto at=fields[2].toInt()-1;if(at<0||at>2)continue;
                if(fields[0]!="100644"&&fields[0]!="100755"){callback(false,{},tr("심볼릭 링크·서브모듈 충돌은 외부 Git 도구에서 해결하세요."));return;}file->present[at]=true;file->modes[at]=QString::fromLatin1(fields[0]);oids[at]=QString::fromLatin1(fields[1]);}
            if(!file->present[0]&&!file->present[1]&&!file->present[2]){callback(false,{},tr("충돌 인덱스가 바뀌었습니다. 다시 읽으세요."));return;}
            if(file->present[1]&&file->present[2]&&file->modes[1]!=file->modes[2]){callback(false,{},tr("실행 권한이 서로 다른 파일 모드 충돌은 외부 Git 도구에서 해결하세요."));return;}
            QFile working(QDir(repository).filePath(path));file->exists=working.exists();
            if(file->exists){if(!QFileInfo(working).isFile()||!working.open(QIODevice::ReadOnly)||working.size()>8*1024*1024){callback(false,{},tr("8 MiB 이하 일반 충돌 파일만 처리할 수 있습니다."));return;}file->working=working.readAll();}
            file->fingerprint=QString::fromLatin1(QCryptographicHash::hash(state.fingerprint.toUtf8()+out+(file->exists?"exists":"missing")+file->working,QCryptographicHash::Sha256).toHex());
            auto next=std::make_shared<std::function<void(int)>>();std::weak_ptr<std::function<void(int)>> weak=next;
            *next=[=,this](int at){if(at==3){file->text=text(file->working);for(const auto &side:file->sides)file->text=file->text&&text(side);callback(true,*file,{});return;}auto owner=weak.lock();if(!file->present[at]){(*owner)(at+1);return;}
                git_->inspectLimited({"cat-file","blob",oids[at]},[=](bool ok,const QByteArray &data,const QString &error){if(!ok){callback(false,{},error);return;}file->sides[at]=data;(*owner)(at+1);});
            };(*next)(0);
        });
    });
}
void MergeController::resolve(const ConflictFile &expected,const QString &choice,const QByteArray &edited,GitClient::CommandCallback callback){
    if(expected.repository!=git_->repositoryPath()){callback(false,{},tr("저장소가 바뀌었습니다."));return;}
    loadConflict(expected.path,[=,this](bool ok,ConflictFile current,QString error){
        if(!ok){callback(false,{},error);return;}if(current.fingerprint!=expected.fingerprint){callback(false,{},tr("충돌 파일이나 인덱스가 바뀌었습니다. 다시 읽고 해결하세요."));return;}
        int side=QStringList{"base","ours","theirs"}.indexOf(choice);bool remove=choice=="delete";QByteArray data=edited;
        if(edited.size()>8*1024*1024){callback(false,{},tr("8 MiB 이하 결과만 저장할 수 있습니다."));return;}
        if(side>=0){remove=!current.present[side];data=current.sides[side];}
        else if(choice!="edit"&&!remove){callback(false,{},tr("지원하지 않는 해결 방식입니다."));return;}
        if(choice=="edit"&&(!current.text||!text(data)||QRegularExpression("(?m)^(<{7,} |={7,}\\r?$|>{7,} |\\|{7,} )").match(QString::fromUtf8(data)).hasMatch())){callback(false,{},tr("UTF-8 텍스트의 충돌 표시를 모두 해결한 뒤 저장하세요."));return;}
        const auto backup=QDir(current.gitDirectory).filePath("gitcanvas/conflict-backups/"+QUuid::createUuid().toString(QUuid::WithoutBraces));
        if(!safe(current.gitDirectory,"gitcanvas/conflict-backups")||!QDir().mkpath(backup)){callback(false,{},tr("충돌 백업 폴더를 만들지 못했습니다."));return;}
        bool saved=saveFile(QDir(backup).filePath("working.bin"),current.working);
        for(int i=0;i<3;++i)if(current.present[i])saved=saveFile(QDir(backup).filePath(QString::number(i+1)+".bin"),current.sides[i])&&saved;
        saved=saveFile(QDir(backup).filePath("manifest.json"),QJsonDocument(QJsonObject{{"repository",current.repository},{"path",current.path},{"existed",current.exists}}).toJson())&&saved;
        if(!saved){callback(false,{},tr("충돌 백업에 실패하여 원본을 변경하지 않았습니다."));return;}
        git_->loadOperationState([=,this](bool ok,const QString &error){
        if(!ok||git_->operationState().fingerprint!=current.stateFingerprint){callback(false,{},tr("백업 중 작업 상태가 바뀌어 저장하지 않았습니다.\n")+error);return;}
        const auto absolute=QDir(current.repository).filePath(current.path);
        // Recheck the working bytes immediately before the local write as well.
        QFile file(absolute);const bool exists=file.exists();QByteArray now;if(exists){if(!file.open(QIODevice::ReadOnly)){callback(false,{},file.errorString());return;}now=file.readAll();file.close();}
        if(exists!=current.exists||now!=current.working||!safe(current.repository,current.path)){callback(false,{},tr("백업 중 파일이 바뀌어 저장하지 않았습니다."));return;}
        if(remove){if(exists&&!QFile::remove(absolute)){callback(false,{},tr("파일을 삭제하지 못했습니다. 백업: ")+backup);return;}}
        else if(!saveFile(absolute,data)){callback(false,{},tr("파일을 저장하지 못했습니다. 백업: ")+backup);return;}
        git_->inspect({"--literal-pathspecs","add","-A","--",current.path},[=,this](bool ok,const QByteArray &out,const QString &error){
            git_->loadOperationState([=](bool checked,const QString &stateError){callback(ok&&checked,out+QObject::tr("\n충돌 백업: ").toUtf8()+backup.toUtf8(),ok&&checked?QString():error+"\n"+stateError+QObject::tr("\n작업 파일은 이미 변경되었을 수 있습니다. 백업: ")+backup);});
        });
        });
    });
}
