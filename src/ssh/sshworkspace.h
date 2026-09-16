#pragma once
#include <QDialog>
#include <QJsonObject>
#include <QProcess>
#include <QTimer>
#include <functional>
class QComboBox;
class QLineEdit;
class QSpinBox;
class QLabel;
class QListWidget;
class QPlainTextEdit;
class QPushButton;
class DiffWidget;
class HistoryTable;

struct SshProfile {
    QString id, alias, host, user, identity, root;
    int port=22;
    QJsonObject json() const;
    static SshProfile fromJson(const QJsonObject &value);
    bool valid() const;
};
class SshExecutor final : public QObject {
    Q_OBJECT
public:
    using Callback=std::function<void(bool,const QJsonObject &,const QString &)>;
    explicit SshExecutor(QObject *parent=nullptr,const QString &storage={});
    bool busy() const {return process_.state()!=QProcess::NotRunning||bool(callback_);}
    bool writing() const {return writing_;}
    void scan(const SshProfile &,Callback);
    bool trust(const SshProfile &,const QString &key,QString *error);
    bool trusted(const SshProfile &) const;
    void request(const SshProfile &,QJsonObject,Callback);
    void disconnectTransport();
    static QString fingerprint(const QString &key);
    static QStringList arguments(const SshProfile &,const QString &knownHosts);
signals:
    void activity(const QString &);
    void busyChanged();
private:
    void start(const QString &,const QStringList &,const QByteArray &,Callback,bool scan,bool write);
    void consume();
    void finish(bool,const QString &error={});
    QString knownFile(const SshProfile &) const;
    QProcess process_;
    QTimer timer_;
    QByteArray output_,errors_;
    QJsonObject result_;
    Callback callback_;
    bool scanning_=false,writing_=false,received_=false;
    QString directory_,failure_;
};
class SshWorkspace final : public QDialog {
    Q_OBJECT
public:
    explicit SshWorkspace(QWidget *parent=nullptr,const QString &storage={});
    void reject() override;
protected:
    void closeEvent(QCloseEvent *) override;
private:
    SshProfile profile() const;
    void loadProfiles();
    void saveProfile();
    void connectHost();
    void browse();
    void refresh();
    void send(QJsonObject,SshExecutor::Callback);
    void mutate(const QString &,QJsonObject={});
    void showFile(bool edit);
    void repositories();
    void remember(bool favorite=false);
    void update();
    bool discardDraft();
    SshExecutor executor_;
    QComboBox *profiles_,*branches_;
    QLineEdit *alias_,*host_,*user_,*key_,*root_,*path_,*title_;
    QSpinBox *port_;
    QLabel *scope_,*status_;
    QListWidget *folders_,*saved_,*files_;
    QPlainTextEdit *editor_,*description_,*log_;
    DiffWidget *diff_;
    HistoryTable *history_;
    QList<QWidget*> controls_;
    QList<QPushButton*> writes_;
    QPushButton *connect_,*refresh_,*saveFile_,*cancel_;
    QJsonObject state_;
    QString connectedId_,editPath_,editHash_;
    QString editRepository_,editProfile_;
    QString fetchId_;
    bool editCrlf_=false,editBom_=false;
    bool online_=false;
};
