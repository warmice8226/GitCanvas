#pragma once
#include "githubclient.h"
#include <QWidget>
class GitClient;
class QLineEdit;
class QComboBox;
class QLabel;
class QPushButton;
class QTableWidget;
class QDialog;
class QCheckBox;
class GithubPanel final : public QWidget {
    Q_OBJECT
public:
    explicit GithubPanel(GitClient *git,QWidget *parent=nullptr);
    bool busy() const { return client_.busy()||prBusy_; }
signals:
    void busyChanged(bool busy);
    void repositoryCreated(const QString &path);
    void repositoryCloned(const QString &path);
    void repositoryChanged();
    void commandStarted(const QString &command);
private:
    void showEvent(QShowEvent *event) override;
    void update();
    void clearRepositories();
    void refresh();
    void filter();
    QString host() const;
    QString activeLogin() const;
    GitClient *git_;
    GithubClient client_;
    QDialog *advanced_;
    QCheckBox *autoSetup_;
    QLabel *accountSummary_;
    QLineEdit *host_,*path_,*search_;
    QComboBox *accounts_,*owner_,*visibility_;
    QLabel *status_,*scope_,*device_;
    QTableWidget *repos_;
    QList<QPushButton*> controls_;
    QPushButton *more_,*list_,*switch_,*logout_,*clone_,*browser_,*setup_,*cancel_,*pullRequests_;
    int page_=0;
    bool moreAvailable_=false;
    bool restored_=false, setupAfterLogin_=false;
    bool prBusy_=false;
};
