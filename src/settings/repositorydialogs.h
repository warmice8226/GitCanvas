#pragma once
#include <QDialog>
class GitClient;
class QLabel;
class QLineEdit;
class QComboBox;
class QSpinBox;
class QPushButton;
class QPlainTextEdit;

class RepositoryDialog : public QDialog {
    Q_OBJECT
public:
    RepositoryDialog(GitClient *git, bool clone, QWidget *parent = nullptr);
    QString createdPath() const { return createdPath_; }
    void reject() override;
private:
    void execute();
    void setWorking(bool busy);
    GitClient *git_;
    bool clone_, working_ = false;
    QString createdPath_;
    QLineEdit *path_, *url_, *branch_;
    QComboBox *mode_;
    QSpinBox *depth_;
    QPushButton *run_, *browse_, *close_;
    QPlainTextEdit *progress_;
};

class RemoteDialog : public QDialog {
    Q_OBJECT
public:
    RemoteDialog(GitClient *git, QWidget *parent = nullptr);
    void reject() override;
signals:
    void configured();
private:
    void reload();
    void selectRemote();
    void save();
    void remove();
    void connectBranch();
    void setWorking(bool busy);
    void finish(bool ok, const QString &error, bool reloadList = true);
    GitClient *git_;
    bool working_ = false;
    QString branchName_;
    QComboBox *remotes_, *target_;
    QLineEdit *name_, *url_;
    QLabel *branch_, *status_, *addresses_;
    QPushButton *save_, *remove_, *fetch_, *connect_, *sameUrl_, *close_;
};
