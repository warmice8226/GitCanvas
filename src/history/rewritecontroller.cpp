#include "rewritecontroller.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSet>
#include <QUuid>

namespace {
bool managedDirectory(const QString &directory){const QFileInfo a(QDir(directory).filePath("gitcanvas"));return !a.isSymLink()&&!a.isJunction();}
bool save(const QString &path,const QByteArray &bytes){if(QFileInfo(path).isSymLink())return false;QSaveFile file(path);return file.open(QIODevice::WriteOnly)&&file.write(bytes)==bytes.size()&&file.commit();}
bool overlaps(const QByteArray &others,const QByteArray &paths){
    QStringList targets;for(const auto &raw:paths.split('\0')){auto path=QString::fromUtf8(raw);if(!path.isEmpty())targets.append(path.toCaseFolded());}
    for(const auto &raw:others.split('\0')){auto path=QString::fromUtf8(raw).toCaseFolded();while(path.endsWith('/'))path.chop(1);if(path.isEmpty())continue;for(const auto &target:targets)if(path==target||path.startsWith(target+'/')||target.startsWith(path+'/'))return true;}return false;
}
}
void RewriteController::plan(const QString &base,bool root,std::function<void(bool,RebasePlan,QString)> callback){
    if(!root&&(base.isEmpty()||base.startsWith('-'))){callback(false,{},tr("유지할 기준 커밋/브랜치를 입력하세요."));return;}
    git_->loadOperationState([=,this](bool ok,const QString &error){
        if(!ok){callback(false,{},error);return;}const auto state=git_->operationState();
        if(!state.operation.isEmpty()||!state.conflicts.isEmpty()||!state.locks.isEmpty()){callback(false,{},tr("진행 작업·충돌·잠금을 먼저 해결하세요."));return;}
        git_->loadStatus([=,this](bool ok,QList<GitStatusEntry> files,QString error){
            if(!ok||!files.isEmpty()){callback(false,{},ok?tr("작업 변경과 새 파일을 먼저 커밋하거나 Stash에 보관하세요."):error);return;}
            git_->inspect({"symbolic-ref","--quiet","HEAD"},[=,this](bool attached,const QByteArray &,const QString &error){
                if(!attached){callback(false,{},tr("로컬 브랜치로 먼저 전환하세요.\n")+error);return;}
                git_->inspect({"rev-parse","--verify","HEAD"},[=,this](bool ok,const QByteArray &head,const QString &error){
                    if(!ok){callback(false,{},error);return;}const auto oid=QString::fromUtf8(head.trimmed());
                    auto load=[=,this](const QString &baseOid){const auto range=root?oid:baseOid+".."+oid;
                        git_->inspect({"rev-list","--min-parents=2",range},[=,this](bool ok,const QByteArray &merges,const QString &error){
                            if(!ok||!merges.trimmed().isEmpty()){callback(false,{},ok?tr("병합 커밋이 있는 범위는 지원하지 않습니다. 선형 구간을 선택하세요."):error);return;}
                            git_->inspectLimited({"log","--reverse","--topo-order","--max-count=201","--format=%H%x00%s%x00%B%x00",range},[=](bool ok,const QByteArray &out,const QString &error){
                                if(!ok){callback(false,{},error);return;}RebasePlan plan{state.repository,state.gitDirectory,oid,baseOid,state.fingerprint,root,{}};
                                const auto fields=out.split('\0');for(int i=0;i+2<fields.size();i+=3)plan.entries.append({QString::fromLatin1(fields[i].trimmed()),QString::fromUtf8(fields[i+1]),QString::fromUtf8(fields[i+2]).trimmed(),"pick"});
                                if(plan.entries.isEmpty()||plan.entries.size()>200){callback(false,{},QObject::tr("1~200개 커밋 범위를 선택하세요."));return;}callback(true,plan,{});
                            });
                        });
                    };
                    if(root){load({});return;}
                    git_->inspect({"rev-parse","--verify",base+"^{commit}"},[=,this](bool ok,const QByteArray &out,const QString &error){
                        if(!ok){callback(false,{},error);return;}const auto baseOid=QString::fromUtf8(out.trimmed());
                        git_->inspect({"merge-base","--is-ancestor",baseOid,oid},[=](bool ok,const QByteArray &,const QString &error){if(ok)load(baseOid);else callback(false,{},QObject::tr("기준은 현재 HEAD의 조상이어야 합니다.\n")+error);});
                    });
                });
            });
        });
    });
}
void RewriteController::start(const RebasePlan &expected,GitClient::CommandCallback callback){
    if(expected.repository!=git_->repositoryPath()){callback(false,{},tr("저장소가 바뀌었습니다."));return;}
    plan(expected.base,expected.root,[=,this](bool ok,RebasePlan current,QString error){
        if(!ok){callback(false,{},error);return;}if(current.head!=expected.head||current.fingerprint!=expected.fingerprint){callback(false,{},tr("계획 이후 HEAD·인덱스·상태가 바뀌었습니다. 계획을 다시 읽으세요."));return;}
        QSet<QString> original,seen;for(const auto &entry:current.entries)original.insert(entry.oid);
        bool preceding=false;QJsonArray entries;
        for(const auto &entry:expected.entries){
            if(!original.contains(entry.oid)||seen.contains(entry.oid)||!QStringList{"pick","reword","edit","squash","fixup","drop"}.contains(entry.action)){callback(false,{},tr("커밋 중복/누락 또는 지원하지 않는 동작이 있습니다."));return;}seen.insert(entry.oid);
            if((entry.action=="squash"||entry.action=="fixup")&&!preceding){callback(false,{},tr("Squash/Fixup 앞에는 유지할 커밋이 있어야 합니다."));return;}
            if(entry.action=="reword"&&entry.message.trimmed().isEmpty()){callback(false,{},tr("Reword 메시지를 입력하세요."));return;}
            if(entry.action!="drop")preceding=true;
            entries.append(QJsonObject{{"oid",entry.oid},{"action",entry.action},{"message",entry.message}});
        }
        if(original!=seen||!preceding){callback(false,{},tr("전체 원본 커밋을 포함하고 최소 하나는 유지하세요. 제외할 커밋은 Drop으로 표시합니다."));return;}
        auto execute=[=,this]{
        if(!managedDirectory(current.gitDirectory)){callback(false,{},tr("안전하지 않은 계획 폴더 경로입니다."));return;}
        const auto id=QUuid::createUuid().toString(QUuid::WithoutBraces),backup="refs/gitcanvas/rebase-backups/"+id;
        const auto folder=QDir(current.gitDirectory).filePath("gitcanvas/rebase-plans");const QFileInfo folderInfo(folder);
        if(folderInfo.isSymLink()||folderInfo.isJunction()||!QDir().mkpath(folder)){callback(false,{},tr("계획 폴더를 만들지 못했습니다."));return;}
        const auto bytes=QJsonDocument(QJsonObject{{"version",1},{"repository",current.repository},{"gitDirectory",current.gitDirectory},{"head",current.head},{"base",current.base},{"root",current.root},{"backup",backup},{"entries",entries}}).toJson();
        if(!save(QDir(folder).filePath(id+".json"),bytes)||!save(QDir(current.gitDirectory).filePath("gitcanvas/rebase-active.json"),bytes)){callback(false,{},tr("실행 계획 저장에 실패했습니다. 이력은 변경하지 않았습니다."));return;}
        git_->inspect({"update-ref",backup,current.head},[=,this](bool ok,const QByteArray &,const QString &error){
            if(!ok){callback(false,{},error);return;}
            QStringList args{"-c","rebase.abbreviateCommands=false","-c","rebase.instructionFormat=%s","rebase","--interactive","--no-autosquash","--no-autostash","--no-update-refs","--no-rebase-merges","--reapply-cherry-picks","--keep-empty","--empty=keep"};args.append(current.root?"--root":current.base);
            git_->inspect(args,[=,this](bool ok,const QByteArray &out,const QString &error){git_->loadOperationState([=](bool checked,const QString &stateError){callback(ok&&checked,out+QObject::tr("\n실행 전 이력: ").toUtf8()+backup.toUtf8(),ok&&checked?QString():error+"\n"+stateError+QObject::tr("\n진행 상태를 확인하세요. 원래 이력: ")+backup);});});
        });
        };
        git_->inspectLimited({"ls-files","--others","-z"},[=,this](bool ok,const QByteArray &others,const QString &error){
            if(!ok){callback(false,{},error);return;}
            git_->inspectLimited({"log","--format=","--name-only","-z",current.root?current.head:current.base+".."+current.head},[=,this](bool ok,const QByteArray &paths,const QString &error){
                if(!ok||overlaps(others,paths)){callback(false,{},ok?tr("무시된 파일과 이력 편집 대상 경로가 겹칩니다. 파일을 먼저 옮기세요."):error);return;}
                git_->loadOperationState([=,this](bool ok,const QString &error){if(!ok||git_->operationState().fingerprint!=current.fingerprint){callback(false,{},ok?tr("계획 이후 상태가 바뀌었습니다. 다시 읽으세요."):error);return;}execute();});
            });
        });
    });
}
void RewriteController::recoveryLog(int limit,std::function<void(bool,QList<RecoveryEntry>,QString)> callback){
    git_->inspectLimited({"reflog","show","--date=iso-strict","--max-count="+QString::number(qBound(1,limit,2000)),"--format=%gD%x00%H%x00%gs%x00","HEAD"},[=,this](bool ok,const QByteArray &out,const QString &error){
        // Unborn HEAD has no reflog; backup refs can still be browsed.
        QList<RecoveryEntry> entries;if(ok){const auto fields=out.split('\0');for(int i=0;i+2<fields.size();i+=3){auto ref=QString::fromUtf8(fields[i]).trimmed();entries.append({ref,QString::fromLatin1(fields[i+1]),ref.section("@{",1).chopped(1),QString::fromUtf8(fields[i+2]),false});}}
        git_->inspectLimited({"for-each-ref","--sort=-creatordate","--format=%(refname)%00%(objectname)%00%(creatordate:iso-strict)%00%(subject)%00","refs/gitcanvas"},[=](bool refsOk,const QByteArray &refs,const QString &refsError)mutable{
            if(!refsOk){callback(false,{},refsError);return;}QList<RecoveryEntry> backups;const auto fields=refs.split('\0');for(int i=0;i+3<fields.size();i+=4)backups.append({QString::fromUtf8(fields[i]).trimmed(),QString::fromLatin1(fields[i+1]),QString::fromUtf8(fields[i+2]),QString::fromUtf8(fields[i+3]),true});callback(true,backups+entries,ok?QString():error);
        });
    });
}
void RewriteController::recover(const RecoveryEntry &entry,const QString &action,const QString &branch,const QString &repository,const QString &fingerprint,GitClient::CommandCallback callback){
    if(repository!=git_->repositoryPath()){callback(false,{},tr("저장소가 바뀌었습니다."));return;}
    git_->loadOperationState([=,this](bool ok,const QString &error){
        if(!ok){callback(false,{},error);return;}const auto state=git_->operationState();
        if(state.fingerprint!=fingerprint||!state.operation.isEmpty()||!state.conflicts.isEmpty()||!state.locks.isEmpty()){callback(false,{},tr("작업 상태가 바뀌었거나 진행 작업·잠금이 있습니다. 다시 확인하세요."));return;}
        git_->inspect({"rev-parse","--verify",entry.oid+"^{commit}"},[=,this](bool ok,const QByteArray &out,const QString &error){
            if(!ok||QString::fromLatin1(out.trimmed())!=entry.oid){callback(false,{},tr("복구할 커밋을 읽을 수 없습니다.\n")+error);return;}
            if(action=="delete-ref"){
                if(!entry.backup||!entry.ref.startsWith("refs/gitcanvas/")){callback(false,{},tr("앱의 복구 참조만 삭제할 수 있습니다."));return;}
                git_->inspect({"update-ref","-d",entry.ref,entry.oid},callback);return;
            }
            if(action=="stash"){
                if(!entry.ref.startsWith("refs/gitcanvas/stash-backups/")){callback(false,{},tr("Stash 복구 참조를 선택하세요."));return;}
                git_->inspect({"stash","store","-m",tr("GitCanvas 복구 보관본"),entry.oid},callback);return;
            }
            if((action=="branch"||action=="reset")&&entry.ref.startsWith("refs/gitcanvas/stash-backups/")){callback(false,{},tr("Stash 목록으로 복원을 사용하세요."));return;}
            if(action=="branch"){
                if(branch.isEmpty()||branch.startsWith('-')){callback(false,{},tr("새 브랜치 이름을 입력하세요."));return;}
                git_->inspect({"check-ref-format","--branch",branch},[=,this](bool ok,const QByteArray &,const QString &error){if(!ok){callback(false,{},error);return;}git_->inspect({"branch","--",branch,entry.oid},callback);});return;
            }
            if(action!="reset"){callback(false,{},tr("지원하지 않는 복구 방식입니다."));return;}
            git_->loadStatus([=,this](bool ok,QList<GitStatusEntry> files,QString error){
                if(!ok||!files.isEmpty()){callback(false,{},ok?tr("현재 변경을 먼저 커밋하거나 보관하세요. 파일을 버리는 Hard Reset은 제공하지 않습니다."):error);return;}
                git_->inspect({"symbolic-ref","--quiet","HEAD"},[=,this](bool ok,const QByteArray &,const QString &error){
                    if(!ok){callback(false,{},tr("현재 로컬 브랜치가 필요합니다.\n")+error);return;}
                    git_->inspectLimited({"ls-files","--others","-z"},[=,this](bool ok,const QByteArray &others,const QString &error){
                        if(!ok){callback(false,{},error);return;}
                        git_->inspectLimited({"ls-tree","-r","--name-only","-z",entry.oid},[=,this](bool ok,const QByteArray &tree,const QString &error){
                            if(!ok){callback(false,{},error);return;}
                            if(overlaps(others,tree)){callback(false,{},tr("무시된 파일과 복구 대상 경로가 겹칩니다. 파일을 먼저 옮기세요."));return;}
                            git_->loadOperationState([=,this](bool ok,const QString &error){
                                if(!ok||git_->operationState().fingerprint!=fingerprint){callback(false,{},ok?tr("저장소 상태가 바뀌었습니다. 복구 기록을 다시 읽으세요."):error);return;}

                            const auto backup="refs/gitcanvas/restore-backups/"+QUuid::createUuid().toString(QUuid::WithoutBraces);
                            git_->inspect({"update-ref",backup,"HEAD"},[=,this](bool ok,const QByteArray &,const QString &error){if(!ok){callback(false,{},error);return;}git_->inspect({"reset","--keep",entry.oid},[=](bool ok,const QByteArray &out,const QString &error){callback(ok,out+"\n"+backup.toUtf8(),ok?QString():error+QObject::tr("\n복구 직전 HEAD: ")+backup);});});
                            });
                        });
                    });
                });
            });
        });
    });
}
