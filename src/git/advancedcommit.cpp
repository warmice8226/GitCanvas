#include "gitclient.h"
#include <QUuid>

void GitClient::commitWithOptions(const QString &message,bool amend,bool signOff,const QString &expectedHead,CommandCallback callback) {
    if(message.trimmed().isEmpty()){callback(false,{},tr("커밋 제목을 입력하세요."));return;}
    QStringList args{"commit","-m",message};if(signOff)args.append("--signoff");
    if(!amend){run(args,callback);return;}
    args.append("--amend");
    run({"rev-parse","--verify","HEAD"},[=,this](bool ok,const QByteArray &out,const QString &error){
        if(!ok||expectedHead.isEmpty()||QString::fromUtf8(out).trimmed()!=expectedHead){callback(false,{},tr("마지막 커밋이 바뀌었거나 없습니다. Amend를 다시 선택하세요.\n")+error);return;}
        const auto backup="refs/gitcanvas/amend-backups/"+QUuid::createUuid().toString(QUuid::WithoutBraces);
        run({"update-ref",backup,expectedHead},[=,this](bool ok,const QByteArray &,const QString &error){
            if(!ok){callback(false,{},error);return;}
            loadOperationState([=,this](bool ok,const QString &error){
                if(!ok){callback(false,{},error);return;}
                const bool editing=state_.known&&state_.operation=="rebase"&&state_.editStop&&state_.conflicts.isEmpty()&&state_.locks.isEmpty();
                run(args,[=](bool ok,const QByteArray &out,const QString &error){callback(ok,out+"\n"+backup.toUtf8(),ok?QString():error+QObject::tr("\n원래 커밋: ")+backup);},{},0,true,editing);
            });
        });
    });
}
