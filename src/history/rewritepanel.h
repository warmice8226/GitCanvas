#pragma once
#include "rewritecontroller.h"
#include <QWidget>
class QLineEdit;
class QCheckBox;
class QPlainTextEdit;
class QTableWidget;
class QPushButton;
class FlatSections;
class QSpinBox;
class QLabel;
class RewritePanel final:public QWidget {
    Q_OBJECT
public:
    explicit RewritePanel(GitClient *git,QWidget *parent=nullptr);
    void openBase(const QString &base);
    bool confirmLeave();
signals:
    void repositoryChanged();
    void conflictEditorRequested();
    void editCommitRequested();
private:
    void update();
    void prepare();
    void renderPlan();
    void refreshRecovery();
    void recover(const QString &action);
    void continueOperation(bool abort);
    GitClient *git_;
    RewriteController controller_;
    RebasePlan plan_;
    QList<RecoveryEntry> entries_;
    QString repository_,recoveryFingerprint_,recoveryRepository_,preparedBase_;
    bool preparedRoot_=false;
    QLineEdit *base_,*branch_,*filter_;
    QCheckBox *root_;
    QSpinBox *limit_;
    QPlainTextEdit *message_,*details_,*status_;
    QTableWidget *planTable_,*recoveryTable_;
    FlatSections *tabs_;
    QPushButton *prepare_,*start_,*up_,*down_,*refresh_,*continue_,*abort_,*edit_;
    QList<QPushButton*> recoveryButtons_;
    QLabel *state_;
    bool loading_=false,pending_=false,dirty_=false;
};
