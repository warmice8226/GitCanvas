#pragma once
#include "pullrequests.h"
#include <QDialog>
#include <QMap>
class QComboBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QTableWidget;
class DiffWidget;
class PullRequestDialog final : public QDialog {
    Q_OBJECT
public:
    PullRequestDialog(GithubClient *client,GitClient *git,const QString &host,const QString &login,const QString &repository,QWidget *parent=nullptr);
    void reject() override;
signals:
    void busyChanged(bool busy);
    void checkedOut(const QString &path);
private:
    void update();
    void display(const QJsonObject &pr);
    void create();
    void submit(const QString &action,const QString &text={});
    PullRequests requests_;
    QString host_,login_;
    QJsonObject detail_;
    QMap<int,QString> reviewDrafts_;
    int reviewNumber_=0;
    QTableWidget *list_,*files_;
    QComboBox *state_,*action_,*method_;
    QLineEdit *search_;
    QPlainTextEdit *description_,*input_;
    DiffWidget *diff_;
    QLabel *status_,*summary_;
    QPushButton *more_,*cancel_,*send_,*merge_,*closePr_,*reopen_,*checkout_,*reload_,*web_;
    QList<QWidget*> controls_;
    int page_=1;
    bool moreAvailable_=false,valid_=false,mutating_=false;
};
