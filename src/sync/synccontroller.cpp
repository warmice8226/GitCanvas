#include "synccontroller.h"
#include <QCryptographicHash>
SyncController::SyncController(GitClient *git,QObject *parent):QObject(parent),git_(git){invalidate();}
QString SyncController::label() const {
    if(working_)return tr("원격 확인 중…");
    switch(action_){
    case Action::Pull:return tr("↓ Pull · %1개 받기").arg(behind_);
    case Action::Push:return tr("↑ Push · %1개 올리기").arg(ahead_);
    case Action::Publish:return tr("↑ 최초 Push · 브랜치 게시");
    case Action::Diverged:return tr("↓ Pull 우선 · 이력 통합 필요");
    case Action::Unavailable:return tr("Fetch · 연결 설정 확인");
    default:return fetched_?tr("↻ Fetch · 최신 상태"):tr("↻ Fetch · 원격 확인");
    }
}
void SyncController::invalidate(){fetched_=false;action_=Action::Fetch;fingerprint_.clear();explanation_=tr("먼저 원격의 최신 커밋 정보를 내려받습니다. Fetch가 성공하기 전에는 Pull과 Push를 실행하지 않습니다.");emit changed();}
void SyncController::finish(bool ok,const QString &error){working_=false;if(!ok)invalidate();emit changed();if(!ok)emit failed(error);emit completed();}
void SyncController::compare(std::function<void(bool)> done){
    remote_.clear(); trackingRef_.clear();
    git_->inspect({"status","--porcelain=v2","--branch","-z"},[this,done](bool ok,const QByteArray &out,const QString &error){
        if(!ok){explanation_=error;done(false);return;}
        QString head,oid,upstream;int ahead=0,behind=0;bool dirty=false;
        for(const auto &record:out.split('\0')){
            if(record.startsWith("# branch.head "))head=QString::fromUtf8(record.mid(14));
            else if(record.startsWith("# branch.oid "))oid=QString::fromUtf8(record.mid(13));
            else if(record.startsWith("# branch.upstream "))upstream=QString::fromUtf8(record.mid(18));
            else if(record.startsWith("# branch.ab ")){const auto counts=record.mid(12).split(' ');if(counts.size()==2){ahead=counts[0].toInt();behind=-counts[1].toInt();}}
            else if(!record.isEmpty()&&!record.startsWith('#'))dirty=true;
        }
        ahead_=ahead;behind_=behind;
        if(head=="(detached)"||oid=="(initial)"){
            fetched_=false;action_=Action::Unavailable;explanation_=tr("현재 브랜치의 연결된 원격 브랜치(upstream)가 필요합니다. 첫 커밋과 브랜치 연결 설정을 확인한 뒤 Fetch를 다시 실행하세요.");done(true);return;
        }
        git_->inspect({"for-each-ref","--format=%(refname)%00%(upstream:remotename)%00%(upstream:remoteref)%00%(upstream)","refs/heads/"+head},[this,done,out,head,upstream,dirty](bool ok,const QByteArray &refs,const QString &error){
            if(!ok){explanation_=error;done(false);return;}
            QString remote,ref;
            for(const auto &line:refs.split('\n')){auto fields=line.split('\0');if(fields.size()==4 && QString::fromUtf8(fields[0])=="refs/heads/"+head){remote=QString::fromUtf8(fields[1]);ref=QString::fromUtf8(fields[2]);trackingRef_=QString::fromUtf8(fields[3]);}}
            if(remote.isEmpty()||remote=="."||!ref.startsWith("refs/heads/")||!trackingRef_.startsWith("refs/remotes/")){fetched_=false;action_=Action::Unavailable;explanation_=tr("외부 원격 브랜치 연결 설정을 확인하세요. 로컬 브랜치를 upstream으로 사용하는 동기화는 지원하지 않습니다.");done(true);return;}
            if(trackingRef_ != "refs/remotes/" + remote + "/" + ref.mid(11)){fetched_=false;action_=Action::Unavailable;explanation_=tr("원격 브랜치의 기본 fetch 매핑이 필요합니다. 원격 설정을 확인하세요.");done(true);return;}
            git_->inspect({"for-each-ref","--format=%(refname)%00%(objectname)",trackingRef_},[this,done,out,head,upstream,dirty,remote,ref](bool ok,const QByteArray &refs,const QString &error){
            if(!ok){explanation_=error;done(false);return;}
            QByteArray targetOid;
            for(const auto &line:refs.split('\n')){const auto fields=line.split('\0');if(fields.size()==2&&QString::fromUtf8(fields[0])==trackingRef_)targetOid=fields[1];}
            git_->inspect({"remote","get-url",remote},[this,done,out,head,upstream,dirty,remote,ref,targetOid](bool ok,const QByteArray &url,const QString &error){
                if(!ok){explanation_=error;done(false);return;}
                git_->inspect({"remote","get-url","--push","--all",remote},[this,done,out,head,upstream,dirty,remote,ref,url,targetOid](bool ok,const QByteArray &pushUrl,const QString &error){
                    if(!ok){explanation_=error;done(false);return;}
                    if(url.trimmed()!=pushUrl.trimmed()){fetched_=false;action_=Action::Unavailable;explanation_=tr("가져오는 주소와 올리는 주소가 다르거나 Push 대상이 여러 개입니다. 같은 원격을 비교하고 올릴 수 있도록 설정을 확인하세요.");done(true);return;}
                    const auto fingerprint=QString::fromLatin1(QCryptographicHash::hash(out+remote.toUtf8()+ref.toUtf8()+url+pushUrl+targetOid,QCryptographicHash::Sha256).toHex());
                    const auto scope=remote.toUtf8()+ref.toUtf8()+url+pushUrl;
                    if(repository_!=git_->repositoryPath() || (!branch_.isEmpty()&&(branch_!=head||remoteScope_!=scope)))fetched_=false;
                    remoteScope_=scope;
                    repository_=git_->repositoryPath();branch_=head;upstream_=upstream;remote_=remote;remoteRef_=ref;
                    // A refreshed local snapshot may select a new action only after a successful Fetch in this scope.
                    fingerprint_=fingerprint;
                    if(!fetched_){action_=Action::Fetch;explanation_=tr("이 저장소와 브랜치에서 먼저 Fetch를 실행하세요.");}
                    else if(targetOid.isEmpty()){action_=Action::Publish;explanation_=tr("Fetch로 확인한 원격에 대상 브랜치 %1이 없습니다. 현재 커밋을 올려 새 원격 브랜치를 만들고 upstream을 연결합니다. 그 사이 같은 이름이 생기면 덮어쓰지 않고 중단합니다.").arg(remote+"/"+ref.mid(11));}
                    else if(behind_>0&&ahead_>0){action_=Action::Diverged;explanation_=tr("원격에 %1개, 내 브랜치에 %2개의 서로 다른 커밋이 있습니다. 누르면 Fetch로 확인한 원격과 비교·병합하는 화면을 엽니다. 충돌 해결을 마친 뒤 다시 Fetch하여 Push하세요.").arg(behind_).arg(ahead_);}
                    else if(behind_>0){action_=Action::Pull;explanation_=dirty?tr("원격 변경을 받기 전에 작업 중인 변경을 커밋하거나 임시 보관하세요. 수정 중인 파일이 있어 Pull을 차단합니다."):tr("원격의 새 커밋 %1개를 현재 브랜치와 파일에 반영합니다. 이력이 갈라지면 중단하며 자동 병합이나 강제 덮어쓰기는 하지 않습니다.").arg(behind_);}
                    else if(ahead_>0){action_=Action::Push;explanation_=tr("Fetch로 확인한 원격 브랜치 %1에 내 커밋 %2개를 올립니다. 커밋하지 않은 파일은 올라가지 않으며 강제 Push는 사용하지 않습니다.").arg(upstream_).arg(ahead_);}
                    else {action_=Action::Fetch;explanation_=tr("마지막 Fetch 기준으로 원격과 내 커밋이 같습니다. 다시 누르면 최신 원격 정보를 확인합니다.");}
                    if(dirty&&(action_==Action::Pull||action_==Action::Diverged)){action_=Action::Unavailable;explanation_=tr("원격 변경을 받기 전에 작업 변경을 커밋하거나 Stash에 보관하세요.");}
                    done(true);
                });
            });
            });
        });
    });
}
void SyncController::review(std::function<void()> done){
    if(!fetched_){done();return;}
    compare([this,done](bool ok){if(!ok)invalidate();emit changed();done();});
}
void SyncController::execute(){
    if(working_||git_->isBusy())return;
    if(action_==Action::Diverged){
        const auto snapshot=fingerprint_;working_=true;emit changed();
        compare([this,snapshot](bool ok){working_=false;emit changed();if(!ok||!fetched_||action_!=Action::Diverged||fingerprint_!=snapshot){finish(false,tr("상태가 바뀌었습니다. Fetch를 다시 실행하세요."));return;}emit mergeRequested(trackingRef_);});return;
    }
    if(action_==Action::Fetch||action_==Action::Unavailable||!fetched_){
        working_=true;invalidate();emit changed();
        remote_.clear();
        compare([this](bool ok){
            if(!ok){finish(false,explanation_);return;}
            const QStringList fetch=remote_.isEmpty()?QStringList{"fetch","--all"}:QStringList{"fetch","--no-tags","--prune",remote_,"+refs/heads/*:refs/remotes/"+remote_+"/*"};
            git_->inspect(fetch,[this](bool ok,const QByteArray &,const QString &error){
                if(!ok){finish(false,error);return;}
                fetched_=true;repository_=git_->repositoryPath();branch_.clear();
                compare([this](bool ok){finish(ok,explanation_);});
            });
        });return;
    }
    const auto expected=action_;const auto snapshot=fingerprint_;
    working_=true;emit changed();
    compare([this,expected,snapshot](bool ok){
        if(!ok){finish(false,explanation_);return;}
        if(!fetched_||action_!=expected||fingerprint_!=snapshot){finish(false,tr("확인 이후 저장소 상태가 바뀌었습니다. Fetch를 다시 실행하세요."));return;}
        // Explicit upstream target avoids push.default / remote.push selecting a different branch.
        QStringList args=expected==Action::Pull?QStringList{"pull","--ff-only","--no-rebase","--no-autostash","--no-recurse-submodules",remote_,remoteRef_}:QStringList{"-c","push.followTags=false","-c","remote."+remote_+".mirror=false","push","--recurse-submodules=no",remote_,"HEAD:"+remoteRef_};
        if(expected==Action::Publish) args={"-c","push.followTags=false","-c","remote."+remote_+".mirror=false","push","--recurse-submodules=no","--set-upstream","--force-with-lease="+remoteRef_+":",remote_,"HEAD:"+remoteRef_};
        git_->inspect(args,[this](bool ok,const QByteArray &,const QString &error){
            if(!ok){finish(false,error);return;}
            compare([this](bool ok){finish(ok,explanation_);});
        });
    });
}
