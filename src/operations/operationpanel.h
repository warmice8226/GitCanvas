#pragma once
#include <QWidget>
class GitClient;
class QLabel;
class QPushButton;
class QListWidget;
class QPlainTextEdit;
class QProgressBar;

class ProcessControls : public QWidget {
    Q_OBJECT
public:
    explicit ProcessControls(GitClient *git,QWidget *parent=nullptr);
};
class OperationPanel : public QWidget {
    Q_OBJECT
public:
    explicit OperationPanel(GitClient *git,QWidget *parent=nullptr);
signals:
    void refreshRequested();
private:
    void update();
    void recover(bool abort);
    GitClient *git_;
    QLabel *state_;
    QListWidget *conflicts_, *locks_;
    QPushButton *continue_, *abort_, *refresh_;
    QPlainTextEdit *diagnostic_;
};
