#include "githubclient.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>
#include <QDir>
#include <QUrl>
#include <QCoreApplication>
#include <QFileInfo>

GithubClient::GithubClient(QObject *parent):QObject(parent){
    executable_=QSettings().value("github/ghExecutable").toString();timer_.setSingleShot(true);
    connect(&process_,&QProcess::readyReadStandardOutput,this,&GithubClient::read);
    connect(&process_,&QProcess::readyReadStandardError,this,&GithubClient::read);
    connect(&process_,&QProcess::finished,this,[this](int code,QProcess::ExitStatus status){read();finish(code==0&&status==QProcess::NormalExit);});
    connect(&process_,&QProcess::errorOccurred,this,[this](QProcess::ProcessError error){if(error==QProcess::FailedToStart){failure_=tr("gh를 실행할 수 없습니다. GitHub CLI 설치 또는 실행 파일 경로를 확인하세요.");finish(false);}});
    connect(&timer_,&QTimer::timeout,this,[this]{failure_=tr("GitHub 요청 시간이 초과됐습니다. 인증 상태를 다시 확인하세요.");process_.kill();});
}
GithubClient::~GithubClient(){process_.disconnect(this);if(process_.state()!=QProcess::NotRunning){process_.kill();process_.waitForFinished(3000);}}
QString GithubClient::executable()const{
    if(!executable_.isEmpty())return executable_;
    const auto bundled=QCoreApplication::applicationDirPath()+
#ifdef Q_OS_WIN
        "/gh.exe";
#else
        "/gh";
#endif
    if(QFileInfo(bundled).isFile())return bundled;
    const auto found=QStandardPaths::findExecutable("gh");
    if(!found.isEmpty())return found;
    for(const auto *variable:{"ProgramFiles","LOCALAPPDATA"}){
        const auto root=qEnvironmentVariable(variable);
        if(root.isEmpty())continue;
        const auto candidate=root+"/GitHub CLI/gh.exe";
        if(QFileInfo(candidate).isFile())return candidate;
    }
    return {};
}
void GithubClient::setExecutable(const QString &path){if(busy())return;executable_=path;QSettings().setValue("github/ghExecutable",path);}
bool GithubClient::validHost(const QString &host){return host.size()<=253&&QRegularExpression("^(?=.{1,253}$)[A-Za-z0-9](?:[A-Za-z0-9-]*[A-Za-z0-9])?(?:\\.[A-Za-z0-9](?:[A-Za-z0-9-]*[A-Za-z0-9])?)+$").match(host).hasMatch();}
bool GithubClient::context(const QString &host,const QString &login){
    if(busy())return false;
    if(!validHost(host)||(!login.isEmpty()&&!QRegularExpression("^[A-Za-z0-9_][A-Za-z0-9_-]{0,99}$").match(login).hasMatch())){emit message(tr("올바른 호스트와 계정을 선택하세요. 호스트에는 https://나 경로를 넣지 않습니다."));return false;}return true;
}
void GithubClient::run(const QStringList &args,Done done,int timeout,bool login,const QByteArray &input){
    if(busy())return;
    if(executable().isEmpty()){emit message(tr("GitHub CLI(gh)가 없습니다. 설치 안내에서 운영체제에 맞는 버전을 준비한 뒤 실행 파일을 선택하세요."));done(false,{});return;}
    output_.clear();error_.clear();failure_.clear();lastUrl_.clear();done_=std::move(done);running_=true;login_=login;
    auto env=QProcessEnvironment::systemEnvironment();
    // Account selection refers to gh's stored accounts, never an ambient token.
    for(const auto *key:{"GH_TOKEN","GITHUB_TOKEN","GH_ENTERPRISE_TOKEN","GITHUB_ENTERPRISE_TOKEN","GH_DEBUG","GH_REPO","GH_HOST"})env.remove(key);
    env.insert("GH_PROMPT_DISABLED","1");env.insert("GH_NO_UPDATE_NOTIFIER","1");env.insert("NO_COLOR","1");env.insert("GH_PAGER","cat");
    process_.setProcessEnvironment(env);process_.setWorkingDirectory(QDir::tempPath());process_.setProgram(executable());process_.setArguments(args);
    emit busyChanged(true);emit commandStarted("gh "+args.join(' '));timer_.start(timeout);process_.start();
    if(login)process_.write("\n");else if(!input.isEmpty())process_.write(input);process_.closeWriteChannel();
}
void GithubClient::read(){
    output_+=process_.readAllStandardOutput();error_+=process_.readAllStandardError();
    if(output_.size()+error_.size()>4*1024*1024){failure_=tr("GitHub 응답이 4 MiB를 넘었습니다. 조회 범위를 줄이세요.");process_.kill();return;}
    if(login_){const auto match=QRegularExpression("(?i)(?:one-time code|code:)\\s*:?\\s*([A-Z0-9]{4}-[A-Z0-9]{4})").match(QString::fromUtf8(error_));if(match.hasMatch())emit deviceCode(match.captured(1));}
    if(login_){const auto match=QRegularExpression("Open this URL to continue in your web browser: (https://[^\\s]+)").match(QString::fromUtf8(error_));if(match.hasMatch()){const QUrl url(match.captured(1));if(url.host()==loginHost_&&url.userInfo().isEmpty()&&url.path().startsWith("/login/")&&lastUrl_!=url.toString()){lastUrl_=url.toString();emit authorizationUrl(lastUrl_);}}}
}
void GithubClient::finish(bool success){
    if(!running_)return;timer_.stop();running_=false;auto done=std::move(done_);const auto out=output_;const auto diagnostic=error_.toLower();output_.clear();error_.clear();
    if(!success||!failure_.isEmpty()){
        if(failure_.isEmpty()){
            if(diagnostic.contains("sso"))failure_=tr("조직 SSO 승인이 필요합니다. GitHub 조직의 인증 정책을 확인하세요.");
            else if(diagnostic.contains("rate limit"))failure_=tr("GitHub API 호출 한도를 초과했습니다. 잠시 후 다시 조회하세요.");
            else if(diagnostic.contains("401")||diagnostic.contains("authentication")||diagnostic.contains("not logged"))failure_=tr("인증이 없거나 만료됐습니다. 다시 로그인하세요.");
            else if(diagnostic.contains("403"))failure_=tr("접근 권한이 부족하거나 조직 정책이 요청을 차단했습니다.");
            else if(diagnostic.contains("404"))failure_=tr("저장소를 찾을 수 없거나 이 계정에 접근 권한이 없습니다.");
            else if(diagnostic.contains("409")||diagnostic.contains("405"))failure_=tr("서버 상태가 바뀌었거나 병합 조건을 만족하지 않습니다. PR과 검사 상태를 다시 읽으세요.");
            else if(diagnostic.contains("422"))failure_=tr("입력이나 작업 조건이 올바르지 않습니다. 중복 PR, 브랜치, 리뷰 대상과 현재 PR 상태를 확인하세요.");
            else failure_=tr("GitHub 요청에 실패했습니다. gh 버전, 네트워크, 로그인 상태와 권한을 확인하세요.");
        }emit requestFailed();emit message(failure_);success=false;
    }
    emit busyChanged(false);if(done)done(success,out);
}
void GithubClient::cancel(){if(!busy())return;failure_=tr("GitHub 요청을 중단했습니다. 로그인·계정 변경은 이미 반영됐을 수 있으므로 인증 상태를 다시 확인하세요.");process_.kill();}
void GithubClient::api(const QString &host,const QString &login,const QString &endpoint,const QString &method,const QJsonObject &body,ApiCallback callback){
    if(busy()){callback(false,{});return;}
    if(!context(host,login)||login.isEmpty()||(!endpoint.startsWith("repos/")&&!(endpoint=="graphql"&&method=="POST"&&body.value("query").toString().startsWith("mutation Ready(")))||endpoint.section('?',0,0).split('/').contains("..")||endpoint.contains('\n')||
       !QStringList{"GET","POST","PUT","PATCH","DELETE"}.contains(method)){emit message(tr("GitHub 요청 대상이 올바르지 않습니다."));callback(false,{});return;}
    run({"api","--hostname",host,"user"},[this,host,login,endpoint,method,body,callback](bool ok,const QByteArray &out){
        if(!ok){callback(false,{});return;}
        if(QJsonDocument::fromJson(out).object().value("login").toString()!=login){emit message(tr("활성 계정이 바뀌었습니다. GitHub 계정을 다시 확인하세요."));callback(false,{});return;}
        QStringList args{"api","--hostname",host,"--method",method,"--header","Accept: application/vnd.github+json","--header","X-GitHub-Api-Version: 2022-11-28"};
        if(method!="GET")args<<"--input"<<"-";
        args<<endpoint;
        run(args,[this,host,login,method,callback](bool ok,const QByteArray &out){
            if(!ok||method!="GET"){callback(ok,out);return;}
            run({"api","--hostname",host,"user"},[this,login,out,callback](bool ok,const QByteArray &user){
                const bool same=ok&&QJsonDocument::fromJson(user).object().value("login").toString()==login;
                if(ok&&!same)emit message(tr("조회 도중 계정이 바뀌어 응답을 버렸습니다."));callback(same,same?out:QByteArray());
            });
        },45000,false,method=="GET"?QByteArray():QJsonDocument(body).toJson(QJsonDocument::Compact));
    });
}
void GithubClient::check(){run({"--version"},[this](bool ok,const QByteArray &out){if(ok){auto first=QString::fromUtf8(out).section('\n',0,0);emit message(first.startsWith("gh version ")?first:tr("gh 실행 파일 응답을 확인하세요."));}});}
QJsonArray GithubClient::parseAccounts(const QByteArray &data,const QString &host){
    QJsonArray result;const auto hosts=QJsonDocument::fromJson(data).object().value("hosts").toObject();
    for(const auto &value:hosts.value(host).toArray()){const auto source=value.toObject();QJsonObject account;for(const auto *key:{"login","host","active","state","scopes","tokenSource","gitProtocol"})account[key]=source.value(key);result.append(account);}return result;
}
void GithubClient::accounts(const QString &host){if(!context(host))return;run({"auth","status","--hostname",host,"--json","hosts"},[this,host](bool ok,const QByteArray &out){if(!ok){emit accountsReady({});return;}const auto doc=QJsonDocument::fromJson(out);if(!doc.isObject()||!doc.object().value("hosts").isObject()){emit accountsReady({});emit message(tr("gh의 인증 JSON 형식을 지원하지 않습니다. 최신 GitHub CLI로 갱신하세요."));return;}const auto values=parseAccounts(out,host);emit accountsReady(values);if(values.isEmpty())emit message(tr("저장된 계정이 없습니다. 브라우저로 로그인하세요."));});}
void GithubClient::login(const QString &host){if(!context(host))return;loginHost_=host;run({"auth","login","--hostname",host,"--web","--git-protocol","https","--skip-ssh-key"},[this](bool ok,const QByteArray &){if(ok){emit message(tr("로그인을 완료했습니다. 인증 상태에서 계정과 자격 증명 저장 위치를 확인하세요."));emit authChanged();}},300000,true);}
void GithubClient::switchAccount(const QString &host,const QString &login){if(!context(host,login)||login.isEmpty())return;run({"auth","switch","--hostname",host,"--user",login},[this](bool ok,const QByteArray &){if(ok)emit authChanged();});}
void GithubClient::logout(const QString &host,const QString &login){if(!context(host,login)||login.isEmpty())return;run({"auth","logout","--hostname",host,"--user",login},[this](bool ok,const QByteArray &){if(ok)emit authChanged();});}
void GithubClient::setupGit(const QString &host){
    if(!context(host))return;
    for(const auto *key:{"GH_TOKEN","GITHUB_TOKEN","GH_ENTERPRISE_TOKEN","GITHUB_ENTERPRISE_TOKEN"})if(qEnvironmentVariableIsSet(key)){emit message(tr("환경 변수 토큰이 Git 인증을 덮어쓸 수 있습니다. 해당 변수를 제거하고 앱을 다시 실행한 뒤 연결하세요."));emit gitSetupFinished(false);return;}
    run({"auth","setup-git","--hostname",host},[this](bool ok,const QByteArray &){if(ok)emit message(tr("이 호스트의 Git HTTPS credential helper를 gh로 연결했습니다. Git 작성자와 원격 주소는 유지됩니다."));emit gitSetupFinished(ok);});
}
void GithubClient::repositories(const QString &host,const QString &login,int page){
    if(!context(host,login)||login.isEmpty()||page<1||page>10000)return;
    run({"api","--hostname",host,"user"},[this,host,login,page](bool ok,const QByteArray &out){
        if(!ok)return;const auto user=QJsonDocument::fromJson(out).object();
        if(user.value("login").toString()!=login){emit repositoriesReady({},1);emit requestFailed();emit message(tr("활성 GitHub 계정이 바뀌었습니다. 인증 상태를 다시 읽으세요."));return;}
        run({"api","--hostname",host,"--method","GET",QString("user/repos?per_page=100&page=%1&sort=updated&affiliation=owner,collaborator,organization_member").arg(page)},[this,host,login,page](bool ok,const QByteArray &out){
            if(!ok)return;const auto doc=QJsonDocument::fromJson(out);if(!doc.isArray()){emit message(tr("저장소 응답 형식이 올바르지 않습니다."));return;}const auto repos=doc.array();
            run({"api","--hostname",host,"user"},[this,login,page,repos](bool ok,const QByteArray &out){if(!ok)return;if(QJsonDocument::fromJson(out).object().value("login").toString()!=login){emit repositoriesReady({},1);emit requestFailed();emit message(tr("조회 도중 활성 계정이 변경되어 결과를 버렸습니다."));return;}emit repositoriesReady(repos,page);});
        });
    });
}
