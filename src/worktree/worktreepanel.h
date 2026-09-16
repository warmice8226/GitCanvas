#pragma once
#include "worktreecontroller.h"
#include <QWidget>
#include <QHash>
class QListWidget;
class QPlainTextEdit;
class QLineEdit;
class QCheckBox;
class QLabel;
class QPushButton;
class WorktreePanel final : public QWidget {
    Q_OBJECT
public:
    explicit WorktreePanel(GitClient *git,QWidget *parent=nullptr);
    void invalidate();
signals:
    void repositoryChanged();
private:
    void reload();
    void details();
    void act(const QString &action);
    void updateActions();
    GitClient *git_;
    WorktreeController controller_;
    WorktreeReview review_;
    WorktreeReview ignoreReview_;
    QHash<QString,QPair<QString,WorktreeReview>> ignoreDrafts_;
    QListWidget *stashes_, *files_;
    QPlainTextEdit *detail_, *ignore_;
    QLineEdit *message_, *destination_;
    QCheckBox *untracked_, *index_;
    QPlainTextEdit *result_;
    QList<QPushButton*> buttons_;
    bool loading_=false, pending_=true, ignoreLoaded_=false;
};
