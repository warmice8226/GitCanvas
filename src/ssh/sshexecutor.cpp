#include "sshworkspace.h"
#include "settings/diagnostics.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QProcessEnvironment>
#include <QUuid>

QJsonObject SshProfile::json()const{return {{"id",id},{"alias",alias},{"host",host},{"user",user},{"port",port},{"identity",identity},{"root",root}};}
SshProfile SshProfile::fromJson(const QJsonObject &v){return {v.value("id").toString(),v.value("alias").toString(),v.value("host").toString(),v.value("user").toString(),v.value("identity").toString(),v.value("root").toString(),v.value("port").toInt(22)};}
bool SshProfile::valid()const{
    return QRegularExpression("^[a-f0-9]{32}$").match(id).hasMatch()&&
        QRegularExpression("^[A-Za-z0-9][A-Za-z0-9.:-]{0,252}$").match(host).hasMatch()&&
        QRegularExpression("^[A-Za-z0-9_][A-Za-z0-9_.-]{0,63}$").match(user).hasMatch()&&
        port>0&&port<65536&&root.startsWith('/')&&!root.contains(QChar::Null)&&
        !identity.contains('\n')&&!identity.contains('\r')&&!identity.contains(QChar::Null)&&
        (identity.isEmpty()||QFileInfo(identity).isAbsolute());
}
SshExecutor::SshExecutor(QObject *parent,const QString &storage):QObject(parent){
    directory_=storage.isEmpty()?QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)+"/ssh":storage;
    timer_.setSingleShot(true);timer_.setInterval(180000);
    connect(&timer_,&QTimer::timeout,this,[this]{failure_=tr("SSH 시간이 초과되었습니다. 쓰기는 자동 재실행하지 않습니다. 재연결 후 상태를 확인하세요.");process_.kill();});
    connect(&process_,&QProcess::readyReadStandardOutput,this,&SshExecutor::consume);
    connect(&process_,&QProcess::readyReadStandardError,this,[this]{errors_+=process_.readAllStandardError();if(errors_.size()>262144)errors_=errors_.right(262144);});
    connect(&process_,&QProcess::errorOccurred,this,[this](QProcess::ProcessError e){if(e==QProcess::FailedToStart)finish(false,tr("OpenSSH 실행 파일을 찾거나 실행할 수 없습니다. ssh와 ssh-keyscan 설치를 확인하세요."));});
    connect(&process_,&QProcess::finished,this,[this](int code,QProcess::ExitStatus exit){consume();errors_+=process_.readAllStandardError();errors_=errors_.right(262144);finish(code==0&&exit==QProcess::NormalExit);});
}
QString SshExecutor::knownFile(const SshProfile &p)const{return directory_+"/"+p.id+".known_hosts";}
QString SshExecutor::fingerprint(const QString &key){const auto fields=key.simplified().split(' ');if(fields.size()!=3||fields[1]!="ssh-ed25519")return {};const auto blob=QByteArray::fromBase64(fields[2].toLatin1(),QByteArray::AbortOnBase64DecodingErrors);if(blob.size()!=51||!blob.startsWith(QByteArray::fromHex("0000000b7373682d6564323535313900000020")))return {};return "SHA256:"+QString::fromLatin1(QCryptographicHash::hash(blob,QCryptographicHash::Sha256).toBase64(QByteArray::OmitTrailingEquals));}
bool SshExecutor::trusted(const SshProfile &p)const{QFile file(knownFile(p));return p.valid()&&file.exists();}
bool SshExecutor::trust(const SshProfile &p,const QString &key,QString *error){
    if(!p.valid()||fingerprint(key).isEmpty()){if(error)*error=tr("유효하지 않은 SSH 호스트 키입니다.");return false;}
    if(trusted(p)){if(error)*error=tr("이미 등록된 호스트 키는 덮어쓰지 않습니다. 서버 변경은 새 프로필로 확인하세요.");return false;}
    QDir().mkpath(directory_);QSaveFile file(knownFile(p));if(!file.open(QIODevice::WriteOnly)){if(error)*error=file.errorString();return false;}
    const auto fields=key.simplified().split(' ');const auto name=p.port==22?p.host:"["+p.host+"]:"+QString::number(p.port);
    const auto bytes=(name+" "+fields[1]+" "+fields[2]+"\n").toUtf8();if(file.write(bytes)!=bytes.size()||!file.commit()){if(error)*error=file.errorString();return false;}return true;
}
QStringList SshExecutor::arguments(const SshProfile &p,const QString &known){
    QStringList args{"-F","none","-T","-p",QString::number(p.port),"-l",p.user,
        "-o","BatchMode=yes","-o","StrictHostKeyChecking=yes","-o","UpdateHostKeys=no",
        "-o","GlobalKnownHostsFile=none","-o","UserKnownHostsFile=\""+QDir::fromNativeSeparators(known)+"\"",
        "-o","HostKeyAlgorithms=ssh-ed25519","-o","ForwardAgent=no","-o","ClearAllForwardings=yes",
        "-o","ConnectTimeout=15","-o","ServerAliveInterval=15","-o","ServerAliveCountMax=3","-o","RequestTTY=no"};
    if(!p.identity.isEmpty())args.append({"-i",p.identity,"-o","IdentitiesOnly=yes"});args.append(p.host);return args;
}
void SshExecutor::scan(const SshProfile &p,Callback callback){if(!p.valid()){callback(false,{},tr("호스트·사용자·포트·절대 시작 경로를 확인하세요."));return;}start("ssh-keyscan",{"-T","10","-p",QString::number(p.port),"-t","ed25519",p.host},{},callback,true,false);}
void SshExecutor::request(const SshProfile &p,QJsonObject request,Callback callback){
    if(!p.valid()||!trusted(p)){callback(false,{},tr("먼저 SSH 프로필과 호스트 키를 확인하세요."));return;}
    QFile helper(":/ssh/remote_helper.py");if(!helper.open(QIODevice::ReadOnly)){callback(false,{},tr("원격 helper 리소스를 읽지 못했습니다."));return;}
    // Only fixed code and base64 alphabet enter the shell command. User data is stdin JSON.
    const auto encoded=qCompress(helper.readAll(),9).mid(4).toBase64();auto args=arguments(p,knownFile(p));
    args.append("python3 -c 'import base64,zlib;exec(zlib.decompress(base64.b64decode(\""+QString::fromLatin1(encoded)+"\")))'");
    request.insert("version",1);request.insert("root",p.root);
    const auto action=request.value("action").toString();const bool write=!QStringList{"probe","browse","status","history","diff","read","sync-info"}.contains(action);
    if(write)request.insert("id",QUuid::createUuid().toString(QUuid::Id128));
    start("ssh",args,QJsonDocument(request).toJson(QJsonDocument::Compact),callback,false,write);
}
void SshExecutor::start(const QString &program,const QStringList &args,const QByteArray &input,Callback callback,bool scan,bool write){
    if(busy()){callback(false,{},tr("다른 SSH 작업이 진행 중입니다."));return;}
    output_.clear();errors_.clear();result_={};failure_.clear();received_=false;scanning_=scan;writing_=write;callback_=std::move(callback);
    auto env=QProcessEnvironment::systemEnvironment();env.insert("SSH_ASKPASS_REQUIRE","never");
    for(const auto &key:env.keys())if(key=="GH_TOKEN"||key=="GITHUB_TOKEN"||key=="GH_ENTERPRISE_TOKEN"||key=="GITHUB_ENTERPRISE_TOKEN"||key.startsWith("GIT_CONFIG_"))env.remove(key);
    process_.setProcessEnvironment(env);process_.start(program,args);process_.write(input);process_.closeWriteChannel();timer_.start();emit busyChanged();
}
void SshExecutor::consume(){
    output_+=process_.readAllStandardOutput();if(output_.size()>16*1024*1024){failure_=tr("SSH 응답 제한을 초과했습니다. 상태를 다시 확인하세요.");process_.kill();return;}
    if(scanning_)return;
    int newline;
    while((newline=output_.indexOf('\n'))>=0){const auto line=output_.left(newline);output_.remove(0,newline+1);QJsonParseError error;const auto frame=QJsonDocument::fromJson(line,&error).object();
        if(error.error!=QJsonParseError::NoError){failure_=tr("SSH helper 응답 형식이 올바르지 않습니다.");process_.kill();return;}
        if(frame.value("type")=="progress")emit activity(tr("SSH · %1 · %2초").arg(frame.value("command").toString()).arg(frame.value("seconds").toInt()));
        else if(frame.value("type")=="result"&&frame.value("version").toInt()==1&&!received_){result_=frame;received_=true;}
        else {failure_=tr("지원하지 않는 SSH helper 응답입니다.");process_.kill();return;}
    }
}
void SshExecutor::finish(bool ok,const QString &error){
    if(!callback_)return;timer_.stop();auto callback=std::move(callback_);callback_={};
    QString message=failure_.isEmpty()?error:failure_;QJsonObject value;
    if(scanning_){QString key;for(const auto &line:QString::fromUtf8(output_).split('\n'))if(!fingerprint(line).isEmpty()){key=line;break;}ok=ok&&!key.isEmpty();value={{"key",key},{"fingerprint",fingerprint(key)}};}
    else {const bool transport=ok&&received_&&output_.trimmed().isEmpty();ok=transport&&result_.value("ok").toBool();value=result_.value("result").toObject();value.insert("transport_failed",!transport||!failure_.isEmpty());if(message.isEmpty())message=result_.value("error").toString();}
    if(!message.isEmpty())ok=false;
    if(!ok&&message.isEmpty())message=QString::fromUtf8(errors_).trimmed();
    if(!ok&&message.isEmpty())message=tr("SSH 연결 또는 요청에 실패했습니다. 키·서버·Python 3·Git 설치를 확인하세요.");
    if(!ok&&writing_)message+=tr("\n원격 쓰기가 이미 반영되었거나 계속 실행 중일 수 있습니다. 자동 재실행하지 말고 재연결 후 상태부터 확인하세요.");
    writing_=false;emit busyChanged();callback(ok,value,Diagnostics::redact(message));
}
void SshExecutor::disconnectTransport(){if(!busy())return;failure_=tr("SSH 전송을 중단했습니다. 원격 작업의 취소나 되돌리기를 보장하지 않습니다.");process_.kill();}
