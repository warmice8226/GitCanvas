#include "gittoolbox.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QUuid>
#include <algorithm>

namespace {
QString digest(const QByteArray &data){return QString::fromLatin1(QCryptographicHash::hash(data,QCryptographicHash::Sha256).toHex());}
QString fileDigest(const QString &path){QFile file(path);QCryptographicHash hash(QCryptographicHash::Sha256);return file.open(QIODevice::ReadOnly)&&hash.addData(&file)?QString::fromLatin1(hash.result().toHex()):QString();}
bool allowedState(const GitOperationState &state,const QString &id,const QStringList &args){
    if(!state.known||!state.locks.isEmpty()||!state.conflicts.isEmpty())return false;
    if(state.operation.isEmpty())return true;
    return state.operation=="bisect"&&((id.startsWith("bisect.")&&QStringList{"good","bad","skip","reset"}.contains(args.value(1)))||id.startsWith("export.")||id=="worktree.add"||id=="worktree.attach");
}
}
void ToolboxController::snapshot(std::function<void(bool,QString,QString)> callback){
    git_->loadOperationState([=,this](bool ok,const QString &error){
        if(!ok){callback(false,{},error);return;}const auto state=git_->operationState();
        git_->inspectLimited({"for-each-ref","--format=%(refname)%00%(objectname)"},[=,this](bool ok,const QByteArray &refs,const QString &error){
            if(!ok){callback(false,{},error);return;}
            git_->inspectLimited({"config","--null","--list"},[=,this](bool ok,const QByteArray &config,const QString &error){
                if(!ok){callback(false,{},error);return;}
                git_->inspectLimited({"worktree","list","--porcelain","-z"},[=](bool ok,const QByteArray &trees,const QString &error){
                    if(!ok){callback(false,{},error);return;}QByteArray sparse;
                    QFile rules(QDir(state.gitDirectory).filePath("info/sparse-checkout"));
                    if(rules.exists()){if(rules.size()>8*1024*1024||!rules.open(QIODevice::ReadOnly)){callback(false,{},QObject::tr("입력 파일을 읽을 수 없습니다."));return;}sparse=rules.readAll();}
                    callback(true,digest(state.fingerprint.toUtf8()+refs+config+trees+sparse),{});
                });
            });
        });
    });
}
void ToolboxController::review(const QString &id,const QMap<QString,QString> &values,std::function<void(bool,ToolReview,QString)> callback){
    QString error;auto args=arguments(id,values,&error);if(args.isEmpty()){callback(false,{},error);return;}
    auto all=catalog();auto it=std::find_if(all.begin(),all.end(),[&](const auto &tool){return tool.id==id;});if(it==all.end()){callback(false,{},tr("알 수 없는 기능입니다."));return;}
    const auto tool=*it;ToolReview review{git_->repositoryPath(),{},args,tool.description,id,{},{},tool.query,tool.clean};
    if(review.repository.isEmpty()){callback(false,{},tr("저장소를 먼저 여세요."));return;}
    review.standardInput=values.value("stdin").toUtf8();
    if(id=="expert"){
        const auto input=values.value("stdinFile"),output=values.value("stdoutFile");
        if(!input.isEmpty()){
            if(!review.standardInput.isEmpty()){callback(false,{},tr("표준 입력 텍스트와 파일 중 하나만 선택하세요."));return;}
            review.inputFile=QDir(review.repository).absoluteFilePath(input);const QFileInfo info(review.inputFile);QFile file(review.inputFile);
            if(!info.isFile()||info.size()>8*1024*1024||!file.open(QIODevice::ReadOnly)){callback(false,{},tr("8 MiB 이하의 읽을 수 있는 입력 파일을 선택하세요."));return;}
            review.standardInput=file.readAll();review.inputHash=digest(review.standardInput);
        }
        if(!output.isEmpty()){
            review.outputFile=QDir(review.repository).absoluteFilePath(output);const QFileInfo info(review.outputFile);
            if(info.exists()||info.isSymLink()||info.isJunction()||!QDir(info.absolutePath()).exists()){callback(false,{},tr("대상 파일/폴더가 이미 있습니다. 새 경로를 지정하세요."));return;}
        }
    }
    const auto path=values.value("path");
    if(id.startsWith("notes.")&&!values.value("notes").startsWith("refs/notes/")){callback(false,{},tr("Notes 참조는 refs/notes/로 시작해야 합니다."));return;}
    if(id.startsWith("submodule.")&&!path.isEmpty()){
        const auto relative=QDir::fromNativeSeparators(path);
        if(QDir::isAbsolutePath(path)||relative.split('/').contains("..")||relative.split('/').contains(".git",Qt::CaseInsensitive)){callback(false,{},tr("저장소 안의 상대 폴더 경로를 입력하세요."));return;}
    }
    if(!path.isEmpty()){
        const auto absolute=QDir(review.repository).absoluteFilePath(path);const QFileInfo info(absolute);
        if(id.startsWith("export.")||id=="worktree.add"||id=="worktree.attach"||id=="worktree.detached"||id=="submodule.add"){
            if(info.exists()||info.isSymLink()||info.isJunction()){callback(false,{},tr("대상 파일/폴더가 이미 있습니다. 새 경로를 지정하세요."));return;}
            if(!QDir(info.absolutePath()).exists()){callback(false,{},tr("상위 폴더가 존재해야 합니다."));return;}
        }
        if(id=="worktree.remove"||id=="worktree.move"){
            if(info.canonicalFilePath().isEmpty()||info.canonicalFilePath().compare(QFileInfo(review.repository).canonicalFilePath(),Qt::CaseInsensitive)==0||info.isSymLink()||info.isJunction()){callback(false,{},tr("현재 작업 트리 또는 유효하지 않은 경로는 제거·이동할 수 없습니다."));return;}
            if(id=="worktree.move"&&QFileInfo::exists(QDir(review.repository).absoluteFilePath(values.value("destination")))){callback(false,{},tr("이동 대상 폴더가 이미 있습니다."));return;}
        }
        if(id.startsWith("patch.")||id.startsWith("bundle.")){
            if(!info.isFile()||info.size()>128*1024*1024){callback(false,{},tr("128 MiB 이하의 입력 파일을 선택하세요."));return;}
            review.inputFile=absolute;review.inputHash=fileDigest(absolute);if(review.inputHash.isEmpty()){callback(false,{},tr("입력 파일을 읽을 수 없습니다."));return;}
        }
    }
    auto capture=[=,this](ToolReview prepared){snapshot([=,this](bool ok,QString fingerprint,QString error)mutable{
        if(!ok){callback(false,{},error);return;}
        if(!prepared.query&&!allowedState(git_->operationState(),id,prepared.arguments)){callback(false,{},tr("현재 진행 작업·충돌·잠금 상태에서는 이 기능을 실행할 수 없습니다."));return;}
        prepared.fingerprint=fingerprint;
        if(!prepared.clean){callback(true,prepared,{});return;}
        git_->loadStatus([=](bool ok,QList<GitStatusEntry> entries,QString error){callback(ok&&entries.isEmpty(),prepared,ok&&!entries.isEmpty()?QObject::tr("변경 파일을 먼저 커밋하거나 Stash에 보관하세요."):error);});
    });};
    if(id=="tag.publish"||id=="tag.remote-delete"){
        const auto remote=values.value("remote"),tag="refs/tags/"+values.value("name");
        git_->loadOperationState([=,this](bool ok,const QString &error){
            if(!ok||!allowedState(git_->operationState(),id,args)){callback(false,{},ok?tr("진행 작업을 먼저 종료하세요."):error);return;}
            QStringList fetch{"fetch","--no-tags",remote};if(id=="tag.remote-delete")fetch.append(tag);
            git_->inspect(fetch,[=,this](bool ok,const QByteArray &,const QString &error){
                if(!ok){callback(false,{},error);return;}
                git_->inspectLimited({"ls-remote","--refs",remote,tag},[=,this](bool ok,const QByteArray &out,const QString &error)mutable{
                    if(!ok){callback(false,{},error);return;}const auto oid=QString::fromLatin1(out.split('\t').value(0)).trimmed();
                    if((id=="tag.publish"&&!out.trimmed().isEmpty())||(id=="tag.remote-delete"&&out.trimmed().isEmpty())){callback(false,{},tr("게시할 이름이 이미 존재하거나 삭제할 태그가 없습니다. 덮어쓰지 않습니다."));return;}
                    review.arguments.insert(1,"--force-with-lease="+tag+":"+(id=="tag.publish"?QString():oid));
                    if(id=="tag.remote-delete"){review.inputHash=oid;review.description+=tr("\n삭제할 원격 태그 객체: ")+oid;}
                    capture(review);
                });
            });
        });return;
    }
    if(id=="expert"){
        git_->inspectLimited({"--list-cmds=main"},[=](bool ok,const QByteArray &out,const QString &error){
            const auto commands=QString::fromUtf8(out).split('\n',Qt::SkipEmptyParts);
            if(!ok||(!commands.contains(args.first())&&args.first()!="lfs")){callback(false,{},ok?QObject::tr("설치된 Git 내장 명령 또는 lfs만 실행합니다. 별칭·다른 확장 명령은 외부 터미널을 사용하세요."):error);return;}capture(review);
        });return;
    }
    if(values.value("scope")=="worktree"){
        git_->inspect({"config","--bool","--get","extensions.worktreeConfig"},[=](bool ok,const QByteArray &out,const QString &){if(!ok||out.trimmed()!="true"){callback(false,{},QObject::tr("Worktree 범위를 쓰려면 extensions.worktreeConfig를 먼저 활성화해야 합니다. Local로 대신 저장하지 않습니다."));return;}capture(review);});return;
    }
    capture(review);
}
void ToolboxController::execute(const ToolReview &review,GitClient::CommandCallback callback){
    if(review.repository!=git_->repositoryPath()||review.arguments.isEmpty()){callback(false,{},tr("저장소가 바뀌었습니다. 다시 미리보기하세요."));return;}
    if(!review.inputFile.isEmpty()&&fileDigest(review.inputFile)!=review.inputHash){callback(false,{},tr("미리보기 이후 입력 파일이 바뀌었습니다."));return;}
    if(!review.outputFile.isEmpty()&&(QFileInfo::exists(review.outputFile)||QFileInfo(review.outputFile).isSymLink())){callback(false,{},tr("출력 대상이 이미 있습니다. 새 경로를 지정하세요."));return;}
    snapshot([=,this](bool ok,QString fingerprint,QString error){
        if(!ok||fingerprint!=review.fingerprint){callback(false,{},ok?tr("HEAD·설정·참조·작업 트리 상태가 바뀌었습니다. 다시 미리보기하세요."):error);return;}
        if(review.query){git_->inspectLimited(review.arguments,callback);return;}
        if(!allowedState(git_->operationState(),review.id,review.arguments)){callback(false,{},tr("진행 작업·충돌·잠금을 먼저 해결하세요."));return;}
        auto perform=[=,this]{
            const auto args=review.arguments;const auto id=QUuid::createUuid().toString(QUuid::WithoutBraces);
            auto run=[=,this](const QString &backup){git_->executeReviewed(args,[=](bool ok,const QByteArray &out,const QString &error){
                if(ok&&!review.outputFile.isEmpty()){
                    QFile file(review.outputFile);if(!file.open(QIODevice::WriteOnly|QIODevice::NewOnly)){callback(false,{},QObject::tr("명령은 완료됐지만 출력 파일을 만들지 못했습니다. 작업을 다시 실행하기 전에 상태를 확인하세요.")+"\n"+file.errorString());return;}
                    if(file.write(out)!=out.size()||!file.flush()){callback(false,{},QObject::tr("출력 파일 저장이 완료되지 않았습니다. 일부 파일이 남아 있습니다.")+"\n"+file.errorString());return;}
                    callback(true,review.outputFile.toUtf8()+"\nSHA256: "+digest(out).toUtf8()+"\nBackup: "+backup.toUtf8(),{});return;
                }
                callback(ok,out+(backup.isEmpty()?QByteArray():"\nBackup: "+backup.toUtf8()),error+(ok||backup.isEmpty()?QString():"\nBackup: "+backup));
            },review.standardInput);};
            QString source="HEAD";
            if(review.id.startsWith("notes.")&&!review.query)source=args.value(1).mid(6);
            if(review.id=="tag.delete")source="refs/tags/"+args.last();
            if(review.id=="tag.remote-delete")source=review.inputHash;
            git_->inspect({"rev-parse","--verify",source},[=,this](bool ok,const QByteArray &out,const QString &error){
                if(!ok){if(source=="HEAD"||review.id=="notes.add"||review.id=="notes.append"||review.id=="notes.copy"){run({});return;}callback(false,{},error);return;}
                const auto oid=QString::fromLatin1(out.trimmed()),backup="refs/gitcanvas/tool-backups/"+id;
                git_->inspect({"update-ref",backup,oid},[=,this](bool ok,const QByteArray &,const QString &error){
                    if(!ok){callback(false,{},error);return;}
                    if(review.id=="tag.delete"){git_->executeReviewed({"update-ref","-d",source,oid},[=](bool ok,const QByteArray &out,const QString &error){callback(ok,out+"\nBackup: "+backup.toUtf8(),error);});return;}
                    run(backup);
                });
            });
        };
        auto checked=[=,this]{
            if(review.id=="submodule.deinit"){
                const auto relative=review.arguments.last();const auto directory=QDir(review.repository).absoluteFilePath(relative);
                const QFileInfo info(directory);
                if(info.isSymLink()||info.isJunction()||!QFileInfo::exists(directory+"/.git")){callback(false,{},QObject::tr("초기화된 Submodule 경로를 선택하세요."));return;}
                git_->inspectLimited({"-C",directory,"status","--porcelain=v1","--untracked-files=all","--ignored"},[=](bool ok,const QByteArray &out,const QString &error){if(!ok||!out.trimmed().isEmpty()){callback(false,{},ok?QObject::tr("대상 작업 트리에 변경·미추적·무시 파일이 있습니다. 먼저 보관하거나 옮기세요."):error);return;}perform();});return;
            }
            if(review.id=="replace.delete"){
                git_->inspect({"rev-parse","--verify",review.arguments.last()},[=,this](bool ok,const QByteArray &oid,const QString &error){if(!ok){callback(false,{},error);return;}const auto source="refs/replace/"+QString::fromLatin1(oid.trimmed());git_->inspect({"rev-parse","--verify",source},[=,this](bool ok,const QByteArray &target,const QString &error){if(!ok){callback(false,{},error);return;}const auto backup="refs/gitcanvas/tool-backups/"+QUuid::createUuid().toString(QUuid::Id128);git_->executeReviewed({"update-ref",backup,QString::fromLatin1(target.trimmed())},[=,this](bool ok,const QByteArray &,const QString &error){if(!ok){callback(false,{},error);return;}git_->executeReviewed({"update-ref","-d",source,QString::fromLatin1(target.trimmed())},[=](bool ok,const QByteArray &out,const QString &error){callback(ok,out+"\nBackup: "+backup.toUtf8(),error);});});});});return;
            }
            if(review.id=="worktree.remove"){
                git_->inspectLimited({"-C",review.arguments.last(),"status","--porcelain=v1","--untracked-files=all","--ignored"},[=](bool ok,const QByteArray &out,const QString &error){if(!ok||!out.trimmed().isEmpty()){callback(false,{},ok?QObject::tr("대상 작업 트리에 변경·미추적·무시 파일이 있습니다. 먼저 보관하거나 옮기세요."):error);return;}perform();});return;
            }
            if(review.id=="patch.apply"){
                git_->inspectLimited({"apply","--check","--index","--",review.inputFile},[=](bool ok,const QByteArray &,const QString &error){if(!ok){callback(false,{},error);return;}perform();});return;
            }
            perform();
        };
        // Recheck output destinations, not only Git metadata, before creating files.
        if(review.id.startsWith("export.")||review.id=="worktree.add"||review.id=="worktree.attach"||review.id=="worktree.detached"||review.id=="submodule.add"){
            QString path;const auto args=review.arguments;
            if(review.id=="export.archive")path=args.value(2).mid(QString("--output=").size());
            else if(review.id=="export.patch")path=args.value(1).mid(QString("--output-directory=").size());
            else if(review.id=="export.bundle")path=args.value(2);
            else if(review.id=="submodule.add")path=args.last();
            else path=args.value(review.id=="worktree.add"?4:review.id=="worktree.detached"?3:2);
            const QFileInfo target(QDir(review.repository).absoluteFilePath(path));if(target.exists()||target.isSymLink()||target.isJunction()){callback(false,{},tr("출력 대상이 이미 있습니다. 새 경로를 지정하세요."));return;}
        }
        if(review.clean){git_->loadStatus([=](bool ok,QList<GitStatusEntry> entries,QString error){if(!ok||!entries.isEmpty()){callback(false,{},ok?QObject::tr("변경 파일을 먼저 커밋하거나 보관하세요."):error);return;}checked();});}else checked();
    });
}
