#include "pullrequests.h"
#include "githubclient.h"
#include "git/gitclient.h"
#include <QJsonDocument>
#include <QRegularExpression>
#include <QUrl>
#include <memory>

namespace {
QString encoded(const QString &text){return QString::fromLatin1(QUrl::toPercentEncoding(text));}
QString headSha(const QJsonObject &pr){return pr.value("head").toObject().value("sha").toString();}
bool oid(const QString &sha){return QRegularExpression("^[a-fA-F0-9]{40,64}$").match(sha).hasMatch();}
bool samePr(const QJsonObject &a,const QJsonObject &b){
    return a.value("number")==b.value("number")&&headSha(a)==headSha(b)&&a.value("base").toObject().value("sha")==b.value("base").toObject().value("sha")&&a.value("state")==b.value("state")&&a.value("merged")==b.value("merged")&&a.value("updated_at")==b.value("updated_at");
}
}
PullRequests::PullRequests(GithubClient *client,GitClient *git,QString host,QString login,QString repository,QObject *parent)
    :QObject(parent),client_(client),git_(git),host_(std::move(host)),login_(std::move(login)),repository_(std::move(repository)){
    connect(client_,&GithubClient::message,this,&PullRequests::message);
}
bool PullRequests::validRepository(const QString &repository){return QRegularExpression("^[A-Za-z0-9][A-Za-z0-9_-]*/[A-Za-z0-9_.-]+$").match(repository).hasMatch()&&!repository.contains("..");}
QString PullRequests::path(int number)const{return "repos/"+repository_+"/pulls"+(number>0?"/"+QString::number(number):QString());}
bool PullRequests::begin(){if(busy_||client_->busy()||git_->isBusy())return false;if(!validRepository(repository_)){emit message(tr("저장소 이름이 올바르지 않습니다."));return false;}cancelled_=false;busy_=true;emit busyChanged(true);return true;}
void PullRequests::end(bool ok,const QString &text){busy_=false;if(!text.isEmpty())emit message(text);emit busyChanged(false);emit completed(ok);}
void PullRequests::cancel(){if(!busy_)return;cancelled_=true;if(client_->busy())client_->cancel();if(git_->isBusy())git_->cancelActive();}
void PullRequests::request(const QString &endpoint,const QString &method,const QJsonObject &body,Reply reply){
    if(cancelled_){reply(false,{});return;}
    client_->api(host_,login_,endpoint,method,body,[this,reply](bool ok,const QByteArray &out){
        const auto doc=QJsonDocument::fromJson(out);if(ok&&doc.isNull()){emit message(tr("GitHub 응답 형식을 확인할 수 없습니다."));ok=false;}
        reply(ok&&!cancelled_,doc);
    });
}
void PullRequests::array(const QString &endpoint,const QString &key,ArrayReply reply,int page,QJsonArray rows){
    request(endpoint+(endpoint.contains('?')?"&":"?")+QString("per_page=100&page=%1").arg(page),"GET",{},[this,endpoint,key,reply,page,rows](bool ok,const QJsonDocument &doc)mutable{
        if(!ok||(!key.isEmpty()&&!doc.object().value(key).isArray())||(key.isEmpty()&&!doc.isArray())){reply(false,{});return;}
        const auto current=key.isEmpty()?doc.array():doc.object().value(key).toArray();for(const auto &row:current)rows.append(row);
        if(QJsonDocument(rows).toJson(QJsonDocument::Compact).size()>16*1024*1024){emit message(tr("상세 응답이 16 MiB를 초과했습니다. GitHub 웹에서 확인하세요."));reply(false,{});return;}
        if(current.size()==100){if(page>=30){emit message(tr("조회 범위가 3,000개를 초과해 전체 상태를 확인할 수 없습니다. GitHub 웹에서 확인하세요."));reply(false,{});return;}array(endpoint,key,reply,page+1,rows);return;}
        reply(true,rows);
    });
}
void PullRequests::list(const QString &state,int page){
    if(!QStringList{"open","closed","all"}.contains(state)||page<1||!begin())return;
    request(path()+QString("?state=%1&sort=updated&direction=desc&per_page=100&page=%2").arg(state).arg(page),"GET",{},[this,page](bool ok,const QJsonDocument &doc){if(ok&&doc.isArray())emit listed(doc.array(),page);end(ok&&doc.isArray());});
}
void PullRequests::branches(const QString &repository){
    if(!validRepository(repository)){emit message(tr("브랜치 저장소는 소유자/저장소 형식으로 입력하세요."));return;}if(!begin())return;
    array("repos/"+repository+"/branches",{},[this,repository](bool ok,const QJsonArray &rows){if(ok)emit branchesLoaded(repository,rows);end(ok);});
}
void PullRequests::readDetail(int number,std::function<void(bool,QJsonObject)> reply){
    request(path(number),"GET",{},[this,number,reply](bool ok,const QJsonDocument &doc){
        if(!ok||doc.object().value("number").toInt()!=number||!oid(headSha(doc.object()))){reply(false,{});return;}
        auto data=std::make_shared<QJsonObject>(doc.object());
        // Sequential bounded pages: all CI and review data belongs to this exact head.
        const QString root="repos/"+repository_;
        auto endpoints=std::make_shared<QList<QPair<QString,QString>>>(QList<QPair<QString,QString>>{
            {path(number)+"/files","files"},{root+QString("/issues/%1/comments").arg(number),"comments"},
            {path(number)+"/reviews","reviews"},{path(number)+"/comments","review_comments"},{path(number)+"/commits","commits"},
            {root+"/commits/"+headSha(*data)+"/check-runs?filter=latest","check_runs"}});
        auto next=std::make_shared<std::function<void(int)>>();std::weak_ptr<std::function<void(int)>> weak=next;
        *next=[this,number,root,data,endpoints,reply,weak](int index){
            auto keep=weak.lock();
            if(index<endpoints->size()){
                const auto item=endpoints->at(index);array(item.first,item.second=="check_runs"?item.second:QString(),[data,item,keep,index,reply](bool ok,const QJsonArray &rows){if(!ok){reply(false,{});return;}data->insert(item.second,rows);(*keep)(index+1);});return;
            }
            request(root+"/commits/"+headSha(*data)+"/status","GET",{},[this,number,data,reply](bool ok,const QJsonDocument &status){
                if(!ok){reply(false,{});return;}data->insert("combined_status",status.object());
                request(path(number),"GET",{},[this,data,reply](bool ok,const QJsonDocument &latest){
                    if(!ok||!samePr(*data,latest.object())){emit message(tr("조회 도중 PR이 변경되었습니다. 다시 읽으세요."));reply(false,{});return;}
                    data->insert("mergeable",latest.object().value("mergeable"));data->insert("mergeable_state",latest.object().value("mergeable_state"));
                    request("repos/"+repository_,"GET",{},[data,reply](bool ok,const QJsonDocument &repo){if(ok)data->insert("repository_settings",repo.object());reply(ok,ok?*data:QJsonObject());});
                });
            });
        };(*next)(0);
    });
}
void PullRequests::preview(const QString &source,const QString &head,const QString &base){
    if(!validRepository(source)||head.isEmpty()||base.isEmpty()||!begin())return;
    request("repos/"+repository_+"/compare/"+encoded(base)+"..."+encoded(source.section('/',0,0)+":"+head)+"?per_page=100","GET",{},[this](bool ok,const QJsonDocument &doc){if(ok)emit previewReady(doc.object());end(ok);});
}
void PullRequests::loadTemplate(const QString &file){
    if(!QStringList{".github/pull_request_template.md",".github/PULL_REQUEST_TEMPLATE.md","pull_request_template.md","PULL_REQUEST_TEMPLATE.md","docs/pull_request_template.md"}.contains(file)||!begin())return;
    request("repos/"+repository_+"/contents/"+file,"GET",{},[this](bool ok,const QJsonDocument &doc){
        const auto data=doc.object();if(ok&&data.value("encoding")=="base64"){emit templateReady(QString::fromUtf8(QByteArray::fromBase64(data.value("content").toString().toLatin1())));end(true);return;}
        end(false,tr("템플릿을 읽을 수 없습니다. 경로를 확인하거나 설명을 직접 입력하세요."));
    });
}
void PullRequests::detail(int number){if(number<1||!begin())return;readDetail(number,[this](bool ok,QJsonObject data){if(ok)emit loaded(data);end(ok);});}
bool PullRequests::canMerge(const QJsonObject &pr){
    const auto permissions=pr.value("repository_settings").toObject().value("permissions").toObject();
    if(!permissions.value("push").toBool()&&!permissions.value("maintain").toBool()&&!permissions.value("admin").toBool())return false;
    if(pr.value("state")!="open"||pr.value("draft").toBool()||pr.value("merged").toBool()||!pr.value("mergeable").toBool()||pr.value("mergeable_state")!="clean"||!pr.contains("check_runs")||!pr.contains("combined_status"))return false;
    for(const auto &value:pr.value("check_runs").toArray()){const auto c=value.toObject();if(c.value("status")!="completed"||!QStringList{"success","neutral","skipped"}.contains(c.value("conclusion").toString()))return false;}
    const auto status=pr.value("combined_status").toObject();return status.contains("total_count")&&(status.value("total_count").toInt()==0||status.value("state")=="success");
}
void PullRequests::verify(const QJsonObject &expected,std::function<void(QJsonObject)> reply){
    const int number=expected.value("number").toInt();
    if(number<1||!oid(headSha(expected))||expected.value("base").toObject().value("repo").toObject().value("full_name").toString().compare(repository_,Qt::CaseInsensitive)!=0){end(false,tr("유효한 PR 상세를 먼저 읽으세요."));return;}
    readDetail(number,[this,expected,reply](bool ok,QJsonObject latest){if(!ok){end(false);return;}if(!samePr(expected,latest)){end(false,tr("확인한 뒤 PR의 커밋이나 상태가 변경되었습니다. 상세를 다시 읽으세요."));return;}reply(latest);});
}
void PullRequests::create(const QString &source,const QString &head,const QString &base,const QString &title,const QString &body,bool draft){
    if(!validRepository(source)||head.isEmpty()||base.isEmpty()||title.trimmed().isEmpty()||title.size()>256||body.size()>65000||(source==repository_&&head==base)){emit message(tr("서로 다른 브랜치와 제목을 입력하세요. 제목은 256자, 설명은 65,000자까지입니다."));return;}if(!begin())return;
    request("repos/"+source+"/branches/"+encoded(head),"GET",{},[this,source,head,base,title,body,draft](bool ok,const QJsonDocument &doc){
        if(!ok||!oid(doc.object().value("commit").toObject().value("sha").toString())){end(false);return;}
        request("repos/"+repository_+"/branches/"+encoded(base),"GET",{},[this,source,head,base,title,body,draft](bool ok,const QJsonDocument &){
            if(!ok){end(false);return;}QJsonObject payload{{"title",title.trimmed()},{"body",body},{"base",base},{"head",source.section('/',0,0)+":"+head},{"draft",draft}};
            if(source!=repository_)payload.insert("head_repo",source.section('/',1,1));
            request(path()+"?state=open&head="+encoded(source.section('/',0,0)+":"+head)+"&base="+encoded(base),"GET",{},[this,payload](bool ok,const QJsonDocument &existing){
                if(!ok||!existing.isArray()){end(false);return;}
                if(!existing.array().isEmpty()){emit loaded(existing.array().first().toObject());end(false,tr("동일한 head·base의 열린 PR이 있습니다. 새로 생성하지 않았습니다. 창을 닫고 기존 PR을 확인하세요."));return;}
                request(path(),"POST",payload,[this](bool ok,const QJsonDocument &doc){if(ok)emit loaded(doc.object());end(ok,ok?tr("PR #%1을 생성했습니다. 상세를 다시 읽어 검사와 변경 파일을 확인하세요.").arg(doc.object().value("number").toInt()):tr("생성 결과를 확인하지 못했습니다. 다시 보내기 전에 PR 목록을 확인하세요."));});
            });
        });
    });
}
void PullRequests::act(const QString &action,const QJsonObject &expected,const QString &text){
    if(!QStringList{"comment","approve","request_changes","reviewers","remove_reviewers","update_branch","ready","close","reopen","merge","squash","rebase"}.contains(action))return;
    if((action=="comment"||action=="request_changes"||action=="reviewers"||action=="remove_reviewers")&&text.trimmed().isEmpty()){emit message(tr("내용 또는 리뷰어를 입력하세요."));return;}
    if(text.size()>65000){emit message(tr("내용은 65,000자까지 입력하세요."));return;}if(!begin())return;
    verify(expected,[this,action,text](QJsonObject latest){
        const int number=latest.value("number").toInt();QString endpoint=path(number),method="POST";QJsonObject body;
        const bool merge=QStringList{"merge","squash","rebase"}.contains(action);
        if(merge){if(!canMerge(latest)||!latest.value("repository_settings").toObject().value(action=="merge"?"allow_merge_commit":"allow_"+action+"_merge").toBool()){end(false,tr("병합 권한·방식, 초안·충돌·리뷰·검사 조건을 확인하세요. 현재 상태에서는 병합하지 않았습니다."));return;}endpoint+="/merge";method="PUT";body={{"sha",headSha(latest)},{"merge_method",action}};}
        else if(action=="close"||action=="reopen"){
            if(latest.value("merged").toBool()||(action=="close"&&latest.value("state")!="open")||(action=="reopen"&&latest.value("state")!="closed")){end(false,tr("현재 상태에서 닫기/재열기를 수행할 수 없습니다."));return;}
            method="PATCH";body={{"state",action=="close"?"closed":"open"}};
        }else if(action=="comment"){endpoint="repos/"+repository_+QString("/issues/%1/comments").arg(number);body={{"body",text}};}
        else {
            if(latest.value("state")!="open"){end(false,tr("열린 PR에서만 리뷰를 요청하거나 제출할 수 있습니다."));return;}
            if(action=="update_branch"){endpoint+="/update-branch";method="PUT";body={{"expected_head_sha",headSha(latest)}};}
            else if(action=="ready"){
                if(!latest.value("draft").toBool()||latest.value("node_id").toString().isEmpty()){end(false,tr("초안 PR에서만 리뷰 준비로 변경할 수 있습니다."));return;}
                endpoint="graphql";body={{"query","mutation Ready($id: ID!) { markPullRequestReadyForReview(input: {pullRequestId: $id}) { pullRequest { isDraft } } }"},{"variables",QJsonObject{{"id",latest.value("node_id")}}}};
            }
            else if(action=="reviewers"||action=="remove_reviewers"){
                QJsonArray users,teams;for(const auto &name:text.split(QRegularExpression("[,\\s]+"),Qt::SkipEmptyParts)){
                    const bool team=name.startsWith("team:");const auto value=team?name.mid(5):name;
                    if(!QRegularExpression("^[A-Za-z0-9][A-Za-z0-9_-]*$").match(value).hasMatch()){end(false,tr("리뷰어는 로그인 이름, 팀은 team:팀이름 형식으로 입력하세요."));return;}
                    (team?teams:users).append(value);
                }endpoint+="/requested_reviewers";if(action=="remove_reviewers")method="DELETE";body={{"reviewers",users},{"team_reviewers",teams}};
            }else{
                if(latest.value("user").toObject().value("login")==login_){end(false,tr("내 PR에는 승인/변경 요청 리뷰를 제출할 수 없습니다."));return;}
                endpoint+="/reviews";body={{"event",action=="approve"?"APPROVE":"REQUEST_CHANGES"},{"body",text},{"commit_id",headSha(latest)}};
            }
        }
        request(endpoint,method,body,[this,merge,action](bool ok,const QJsonDocument &doc){
            if(action=="ready"&&ok){const auto result=doc.object().value("data").toObject().value("markPullRequestReadyForReview").toObject().value("pullRequest").toObject();ok=doc.object().value("errors").toArray().isEmpty()&&result.contains("isDraft")&&!result.value("isDraft").toBool();}
            if(merge&&ok&&!doc.object().value("merged").toBool()){end(false,tr("GitHub가 병합을 완료하지 않았습니다. 상세와 검사 상태를 다시 읽으세요."));return;}end(ok,ok?tr("GitHub에 반영했습니다. 상세를 다시 읽어 결과를 확인하세요."):tr("요청을 완료하지 못했습니다. 쓰기가 이미 반영됐을 수 있으므로 다시 보내기 전에 상세를 확인하세요."));
        });
    });
}
void PullRequests::checkout(const QJsonObject &expected){
    if(git_->repositoryPath().isEmpty()){emit message(tr("먼저 이 GitHub 저장소의 로컬 폴더를 여세요."));return;}if(!begin())return;
    verify(expected,[this](QJsonObject pr){
        const auto folder=git_->repositoryPath();const auto sha=headSha(pr);const int number=pr.value("number").toInt();
        git_->inspect({"config","--get-regexp","^remote\\..*\\.url$"},[this,folder,sha,number](bool ok,const QByteArray &out,const QString &error){
            bool matches=false;
            for(const auto &line:QString::fromUtf8(out).split('\n')){
                const auto urlText=line.section(' ',1).trimmed();QUrl url(urlText);QString host=url.host(),repo=url.path();
                const auto ssh=QRegularExpression("^(?:[^@]+@)?([^:]+):(.+)$").match(urlText);
                if(host.isEmpty()&&ssh.hasMatch()){host=ssh.captured(1);repo=ssh.captured(2);}
                while(repo.startsWith('/'))repo.remove(0,1);if(repo.endsWith(".git"))repo.chop(4);
                if(host.compare(host_,Qt::CaseInsensitive)==0&&repo.compare(repository_,Qt::CaseInsensitive)==0)matches=true;
            }
            if(!ok||!matches||folder!=git_->repositoryPath()){end(false,tr("현재 폴더의 원격 주소가 선택한 GitHub 저장소와 일치하지 않습니다. 올바른 폴더를 여세요. ")+error);return;}
            git_->loadStatus([this,folder,sha,number](bool ok,QList<GitStatusEntry> entries,QString error){
                if(!ok||!entries.isEmpty()||cancelled_){end(false,tr("로컬 변경 사항을 먼저 커밋하거나 Stash에 보관하세요. ")+error);return;}
                const auto branch=QString("pr/%1-%2").arg(number).arg(sha.left(12));
                const auto ref="refs/heads/"+branch;
                git_->inspectLimited({"fetch","--no-tags","--","https://"+host_+"/"+repository_+".git",QString("refs/pull/%1/head:%2").arg(number).arg(ref)},[this,folder,sha,branch,ref](bool ok,const QByteArray &,const QString &error){
                    if(!ok||cancelled_){end(false,error);return;}
                    git_->inspect({"rev-parse","--verify",ref},[this,folder,sha,branch](bool ok,const QByteArray &out,const QString &error){
                        if(!ok||QString::fromUtf8(out).trimmed()!=sha||folder!=git_->repositoryPath()||cancelled_){end(false,tr("가져온 PR 커밋이 확인한 커밋과 다릅니다. 전환하지 않았습니다. ")+error);return;}
                        git_->loadStatus([this,folder,branch](bool ok,QList<GitStatusEntry> entries,QString error){
                            if(!ok||!entries.isEmpty()||cancelled_){end(false,tr("가져오는 동안 로컬 파일이 변경되어 전환하지 않았습니다. ")+error);return;}
                            git_->inspect({"switch","--no-overwrite-ignore","--",branch},[this,folder](bool ok,const QByteArray &,const QString &error){end(ok,ok?tr("PR 브랜치로 전환했습니다."):error);if(ok)emit checkedOut(folder);});
                        });
                    });
                });
            });
        });
    });
}
