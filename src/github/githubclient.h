#pragma once
#include <QObject>
#include <QProcess>
#include <QTimer>
#include <QJsonArray>
#include <functional>

class GithubClient final : public QObject {
    Q_OBJECT
public:
    using ApiCallback=std::function<void(bool,const QByteArray &)>;
    void api(const QString &host,const QString &login,const QString &endpoint,const QString &method,
             const QJsonObject &body,ApiCallback callback);
    explicit GithubClient(QObject *parent=nullptr);
    ~GithubClient() override;
    bool busy() const { return running_; }
    QString executable() const;
    void setExecutable(const QString &path);
    void check();
    void accounts(const QString &host);
    void login(const QString &host);
    void switchAccount(const QString &host,const QString &login);
    void logout(const QString &host,const QString &login);
    void setupGit(const QString &host);
    void repositories(const QString &host,const QString &login,int page);
    void cancel();
    static bool validHost(const QString &host);
    static QJsonArray parseAccounts(const QByteArray &data,const QString &host);
signals:
    void busyChanged(bool busy);
    void commandStarted(const QString &command);
    void message(const QString &message);
    void deviceCode(const QString &code);
    void authorizationUrl(const QString &url);
    void requestFailed();
    void accountsReady(const QJsonArray &accounts);
    void repositoriesReady(const QJsonArray &repositories,int page);
    void authChanged();
    void gitSetupFinished(bool success);
private:
    using Done=std::function<void(bool,const QByteArray &)>;
    void run(const QStringList &args,Done done,int timeout=45000,bool login=false,const QByteArray &input={});
    void finish(bool success);
    void read();
    bool context(const QString &host,const QString &login={});
    QProcess process_;
    QTimer timer_;
    QByteArray output_,error_;
    Done done_;
    QString executable_,failure_,loginHost_,lastUrl_;
    bool running_=false,login_=false;
};
