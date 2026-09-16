#include "worktreecontroller.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QUuid>
#include <memory>
#include <algorithm>

namespace {
bool safePath(const QString &root, const QString &path) {
    if (path.isEmpty() || QDir::isAbsolutePath(path) || path.contains('\\') || path.contains(QChar::Null)) return false;
    const auto parts=path.split('/');
    QString current=root;
    for (const auto &part:parts) {
        if (part.isEmpty() || part=="." || part==".." || part.compare(".git",Qt::CaseInsensitive)==0 || part.contains(':') || part.endsWith('.') || part.endsWith(' ')) return false;
        current=QDir(current).filePath(part);
        if (QFileInfo(current).isSymLink() || QFileInfo(current).isJunction()) return false;
    }
    return true;
}
bool hashFile(QCryptographicHash &hash, const QString &path) {
    QFileInfo info(path);
    if (info.isSymLink()) { hash.addData(info.symLinkTarget().toUtf8()); return true; }
    if (!info.exists()) { hash.addData("missing"); return true; }
    if (info.isDir()) { hash.addData("directory"); return true; }
    QFile file(path); if (!file.open(QIODevice::ReadOnly)) return false;
    return hash.addData(&file);
}
QStringList literal(QStringList args,const QStringList &paths) {
    args.prepend("--literal-pathspecs");args.append("--");args.append(paths);return args;
}
// Copy before destructive commands; never delete the original on backup failure.
QString backupFiles(const WorktreeReview &review,const QStringList &paths,QString &error) {
    if(!safePath(review.gitDirectory,"gitcanvas/worktree-backups")){error=QObject::tr("복구 폴더 경로에 심볼릭 링크가 있어 중단했습니다.");return {};}
    const auto directory=QDir(review.gitDirectory).filePath("gitcanvas/worktree-backups/"+QUuid::createUuid().toString(QUuid::WithoutBraces));
    if (!QDir().mkpath(directory)) {error=QObject::tr("복구 폴더를 만들지 못했습니다. 원본은 변경하지 않았습니다.");return {};}
    QJsonArray manifest;
    const auto index=QDir(review.gitDirectory).filePath("index");
    if(QFileInfo::exists(index)&&!QFile::copy(index,QDir(directory).filePath("index"))) {error=QObject::tr("Stage 인덱스 백업 실패. 원본은 변경하지 않았습니다.");return {};}
    for (int i=0;i<paths.size();++i) {
        const auto source=QDir(review.repository).filePath(paths[i]);const QFileInfo info(source);
        if (info.exists() && (!info.isFile() || info.isSymLink())) {error=QObject::tr("일반 파일만 정리할 수 있습니다: %1").arg(paths[i]);return {};}
        const auto name=QString::number(i)+".bin";
        if (info.exists() && !QFile::copy(source,QDir(directory).filePath(name))) {error=QObject::tr("백업 실패. 원본은 변경하지 않았습니다: %1").arg(paths[i]);return {};}
        manifest.append(QJsonObject{{"path",paths[i]},{"existed",info.exists()},{"backup",name},{"permissions",int(info.permissions())}});
    }
    QSaveFile file(QDir(directory).filePath("manifest.json"));
    const auto data=QJsonDocument(QJsonObject{{"repository",review.repository},{"files",manifest}}).toJson();
    if (!file.open(QIODevice::WriteOnly)||file.write(data)!=data.size()||!file.commit()) {error=QObject::tr("복구 목록 저장 실패. 원본은 변경하지 않았습니다.");return {};}
    return directory;
}
}

void WorktreeController::review(ReviewCallback callback) {
    const auto repository=git_->repositoryPath();
    git_->loadOperationState([this,repository,callback](bool ok,const QString &error) {
        if (!ok) {callback(false,{},error);return;}
        const auto state=git_->operationState();
        git_->loadStatus([this,repository,state,callback](bool ok,QList<GitStatusEntry> files,QString error) {
            if (!ok) {callback(false,{},error);return;}
            git_->inspect({"ls-files","--cached","-z"},[this,repository,state,files,callback](bool ok,const QByteArray &tracked,const QString &error) mutable {
            if(!ok){callback(false,{},error);return;}
            for(const auto &raw:tracked.split('\0')) {
                if(raw.isEmpty())continue;const auto path=QString::fromUtf8(raw);
                if(std::none_of(files.cbegin(),files.cend(),[&](const GitStatusEntry &entry){return entry.path==path;}))files.append({" "," ",path,{}});
            }
            git_->inspect({"stash","list","--format=%gd%x00%H%x00%gs%x00"},[repository,state,files,callback](bool ok,const QByteArray &out,const QString &error) {
                if (!ok) {callback(false,{},error);return;}
                QCryptographicHash hash(QCryptographicHash::Sha256);hash.addData(state.fingerprint.toUtf8());hash.addData(out);
                QStringList paths{".gitignore"};
                for (const auto &entry:files) {paths.append(entry.path);if(!entry.originalPath.isEmpty())paths.append(entry.originalPath);}
                paths.removeDuplicates();paths.sort();
                for (const auto &path:paths) {
                    hash.addData(path.toUtf8());hash.addData(QByteArray(1,'\0'));
                    if (!hashFile(hash,QDir(repository).filePath(path))) {callback(false,{},QObject::tr("확인할 파일을 읽을 수 없습니다: %1").arg(path));return;}
                }
                callback(true,{repository,QString::fromLatin1(hash.result().toHex()),state.gitDirectory,files,out},{});
            });
            });
        });
    });
}

void WorktreeController::execute(const WorktreeReview &expected,const QString &action,const QStringList &paths,
                                 const QString &value,const QString &stashOid,GitClient::CommandCallback callback) {
    if (expected.repository!=git_->repositoryPath()) {callback(false,{},tr("저장소가 바뀌었습니다. 다시 확인하세요."));return;}
    review([=,this](bool ok,WorktreeReview current,QString error) {
        if (!ok) {callback(false,{},error);return;}
        if (expected.fingerprint!=current.fingerprint) {callback(false,{},tr("확인 이후 파일·브랜치·Stash 상태가 바뀌었습니다. 목록을 새로고침하고 다시 확인하세요."));return;}
        const auto state=git_->operationState();
        if (!state.known||!state.operation.isEmpty()||!state.conflicts.isEmpty()||!state.locks.isEmpty()) {
            callback(false,{},tr("진행 작업·충돌·잠금을 먼저 해결하세요. 파일은 변경하지 않았습니다."));return;
        }
        auto finish=[callback](bool ok,const QByteArray &out,const QString &error) {
            callback(ok,out,ok?QString():error+QObject::tr("\n실패한 Git 작업은 일부 파일을 이미 바꿨을 수 있습니다. 상태를 확인하세요. Stash 복원 충돌에서는 보관본을 유지합니다."));
        };
        if (action=="save"||action=="save-untracked") {
            QStringList args{"stash","push","-m",value.trimmed().isEmpty()?tr("GitCanvas 임시 보관"):value};
            if(action=="save-untracked")args.append("--include-untracked");
            git_->inspect(args,finish);return;
        }
        if (QStringList{"apply","apply-index","pop","drop","clear","branch"}.contains(action)) {
            QStringList selectors,oids;
            for (const auto &line:current.stashes.split('\n')) {const auto fields=line.split('\0');if(fields.size()>=3){selectors.append(QString::fromUtf8(fields[0]));oids.append(QString::fromUtf8(fields[1]));}}
            const auto index=oids.indexOf(stashOid);
            if (action!="clear"&&index<0) {callback(false,{},tr("선택한 Stash가 없습니다. 다시 선택하세요."));return;}
            auto drop=[=,this](GitClient::CommandCallback done) {
                const auto selector=selectors[index];
                git_->inspect({"rev-parse","--verify",selector},[=,this](bool ok,const QByteArray &out,const QString &error) {
                    if(!ok||QString::fromUtf8(out).trimmed()!=stashOid){done(false,{},tr("Stash 순서가 바뀌어 삭제하지 않았습니다. 복원된 파일과 보관 목록을 확인하세요.\n")+error);return;}
                    git_->inspect({"stash","drop",selector},done);
                });
            };
            auto perform=[=,this](const QString &backup) {
                auto done=[finish,backup](bool ok,const QByteArray &out,const QString &error){finish(ok,out+backup.toUtf8(),error+(backup.isEmpty()?QString():QObject::tr("\n보관본 복구 참조: ")+backup));};
                if(action=="clear"){
                    git_->inspect({"stash","list","--format=%gd%x00%H%x00%gs%x00"},[=,this](bool ok,const QByteArray &out,const QString &error){
                        if(!ok||out!=current.stashes){done(false,{},tr("백업 중 Stash 목록이 바뀌어 전체 삭제하지 않았습니다.\n")+error);return;}
                        git_->inspect({"stash","clear"},done);
                    });return;
                }
                if(action=="drop"){drop(done);return;}
                auto apply=[=,this] {
                    QStringList args{"stash","apply"};if(action=="apply-index"||action=="branch")args.append("--index");args.append(stashOid);
                    git_->inspect(args,[=](bool ok,const QByteArray &out,const QString &error){
                        if(!ok){done(false,out,error);return;}if(action=="pop"||action=="branch")drop(done);else done(true,out,{});
                    });
                };
                if(action=="branch") {
                    if(std::any_of(current.files.cbegin(),current.files.cend(),[](const GitStatusEntry &e){return e.isStaged()||e.isUnstaged();})){done(false,{},tr("보관본에서 브랜치를 만들려면 현재 변경을 먼저 보관하거나 커밋하세요."));return;}
                    git_->inspect({"check-ref-format","--branch",value},[=,this](bool ok,const QByteArray &,const QString &error){
                        if(!ok||value.startsWith('-')){done(false,{},error.isEmpty()?tr("브랜치 이름이 올바르지 않습니다."):error);return;}
                        git_->inspect({"switch","-c",value,stashOid+"^1"},[=](bool ok,const QByteArray &out,const QString &error){if(ok)apply();else done(false,out,error);});
                    });
                } else apply();
            };
            if(action=="apply"||action=="apply-index"){perform({});return;}
            // Keep deleted stash objects reachable, including their index/untracked parents.
            const QString prefix="refs/gitcanvas/stash-backups/"+QUuid::createUuid().toString(QUuid::WithoutBraces);
            const auto targets=action=="clear"?oids:QStringList{stashOid};
            auto next=std::make_shared<std::function<void(int)>>();
            std::weak_ptr<std::function<void(int)>> weak=next;
            *next=[=,this](int i) {
                if(i==targets.size()){
                    QStringList refs;for(int n=0;n<targets.size();++n)refs.append(prefix+"/"+QString::number(n));
                    perform(tr("\nStash 복구 참조:\n")+refs.join('\n'));return;
                }
                auto owner=weak.lock();
                git_->inspect({"update-ref",prefix+"/"+QString::number(i),targets[i]},[owner,i,finish](bool ok,const QByteArray &out,const QString &error){if(ok)(*owner)(i+1);else finish(false,out,error);});
            };
            (*next)(0);return;
        }
        if (!QStringList{"restore","discard","clean","remove","move","ignore"}.contains(action)) {callback(false,{},tr("지원하지 않는 작업입니다."));return;}
        auto targets=action=="ignore"?QStringList{".gitignore"}:paths;
        if(targets.isEmpty()){callback(false,{},tr("파일을 선택하세요."));return;}
        for(const auto &path:targets) {
            if(!safePath(current.repository,path)){callback(false,{},tr("저장소 내부의 일반 파일만 처리할 수 있습니다: %1").arg(path));return;}
            if(action=="ignore")continue;
            auto found=std::find_if(current.files.cbegin(),current.files.cend(),[&](const GitStatusEntry &entry){return entry.path==path;});
            if(found==current.files.cend() || (action=="clean")!=(found->indexStatus=="?")) {callback(false,{},tr("선택한 파일의 추적 상태가 맞지 않습니다. Clean은 미추적 파일만 처리합니다."));return;}
        }
        if(action=="move"&&(paths.size()!=1||!safePath(current.repository,value)||QFileInfo::exists(QDir(current.repository).filePath(value)))) {callback(false,{},tr("이동은 파일 하나를 선택하고, 존재하지 않는 저장소 내부 경로를 지정하세요."));return;}
        if(action=="discard")for(const auto &entry:current.files)if(paths.contains(entry.path)&&!entry.originalPath.isEmpty()) {
            if(!safePath(current.repository,entry.originalPath)){callback(false,{},tr("이름 변경 전 경로가 안전하지 않습니다."));return;}targets.append(entry.originalPath);
        }
        targets.removeDuplicates();
        const auto backup=backupFiles(current,targets,error);
        if(backup.isEmpty()){callback(false,{},error);return;}
        auto done=[finish,backup](bool ok,const QByteArray &out,const QString &error){finish(ok,out+QObject::tr("\n파일 복구 폴더: ").toUtf8()+backup.toUtf8(),error+(ok?QString():QObject::tr("\n파일 복구 폴더: ")+backup));};
        auto change=[=,this] {
        review([=,this](bool ok,WorktreeReview checked,QString error){
        if(!ok||checked.fingerprint!=current.fingerprint){done(false,{},tr("백업 중 상태가 바뀌었거나 재확인에 실패하여 정리하지 않았습니다.\n")+error);return;}
        if(action=="ignore") {
            QSaveFile file(QDir(current.repository).filePath(".gitignore"));const auto data=value.toUtf8();
            const bool saved=file.open(QIODevice::WriteOnly)&&file.write(data)==data.size()&&file.commit();done(saved,{},saved?QString():file.errorString());return;
        }
        if(action=="move"){git_->inspect(literal({"mv"},{paths[0],value}),done);return;}
        if(action=="remove"){git_->inspect(literal({"rm"},paths),done);return;}
        if(action=="clean"){git_->inspect(literal({"clean","-f"},paths),done);return;}
        QStringList args{"restore","--worktree"};if(action=="discard")args.append({"--source=HEAD","--staged"});
        git_->inspect(literal(args,targets),done);
        });
        };
        QStringList stagedPaths;
        for(const auto &entry:current.files)if(targets.contains(entry.path)&&entry.indexStatus!="?"&&entry.indexStatus!="D")stagedPaths.append(entry.path);
        if(stagedPaths.isEmpty()){change();return;}
        // Also materialize selected index contents, so a later GC/split-index expiry does not lose them.
        git_->inspect(literal({"checkout-index","--ignore-skip-worktree-bits","--prefix="+QDir::fromNativeSeparators(backup)+"/staged/"},stagedPaths),
            [change,done](bool ok,const QByteArray &out,const QString &error){if(ok)change();else done(false,out,QObject::tr("Stage 파일 백업 실패. 원본은 변경하지 않았습니다.\n")+error);});
    });
}
