#pragma once
#include <QObject>
#include <QJsonObject>
#include <QJsonArray>
#include <functional>
class GithubClient;
class GitClient;
class QJsonDocument;

class PullRequests final : public QObject {
    Q_OBJECT
public:
    PullRequests(GithubClient *client,GitClient *git,QString host,QString login,QString repository,QObject *parent=nullptr);
    bool busy() const { return busy_; }
    QString repository() const { return repository_; }
    void list(const QString &state,int page);
    void detail(int number);
    void branches(const QString &repository);
    void preview(const QString &source,const QString &head,const QString &base);
    void loadTemplate(const QString &file);
    void create(const QString &source,const QString &head,const QString &base,const QString &title,const QString &body,bool draft);
    void act(const QString &action,const QJsonObject &expected,const QString &text={});
    void checkout(const QJsonObject &expected);
    void cancel();
    static bool validRepository(const QString &repository);
    static bool canMerge(const QJsonObject &detail);
signals:
    void busyChanged(bool busy);
    void message(const QString &text);
    void listed(const QJsonArray &items,int page);
    void loaded(const QJsonObject &detail);
    void branchesLoaded(const QString &repository,const QJsonArray &branches);
    void previewReady(const QJsonObject &comparison);
    void templateReady(const QString &text);
    void completed(bool success);
    void checkedOut(const QString &path);
private:
    using Reply=std::function<void(bool,const QJsonDocument &)>;
    using ArrayReply=std::function<void(bool,const QJsonArray &)>;
    bool begin();
    void end(bool ok,const QString &message={});
    void request(const QString &endpoint,const QString &method,const QJsonObject &body,Reply reply);
    void array(const QString &endpoint,const QString &key,ArrayReply reply,int page=1,QJsonArray rows={});
    void readDetail(int number,std::function<void(bool,QJsonObject)> reply);
    void verify(const QJsonObject &expected,std::function<void(QJsonObject)> reply);
    QString path(int number=0) const;
    GithubClient *client_;
    GitClient *git_;
    QString host_,login_,repository_;
    bool busy_=false,cancelled_=false;
};
