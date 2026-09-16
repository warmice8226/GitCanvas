#pragma once
#include "mergecontroller.h"
#include <QWidget>
class QComboBox;
class QListWidget;
class QPlainTextEdit;
class QLabel;
class QPushButton;
class FlatSections;
class GuardedDialog;
class MergePanel final:public QWidget {
    Q_OBJECT
public:
    explicit MergePanel(GitClient *git,QWidget *parent=nullptr);
    void openTarget(const QString &ref);
    void openConflicts();
    bool confirmLeave();
signals:
    void repositoryChanged();
private:
    void update();
    void compare();
    void load();
    void save(const QString &choice);
    void recover(bool abort);
    GitClient *git_;
    MergeController controller_;
    MergePreview preview_;
    ConflictFile conflict_;
    QComboBox *target_;
    QListWidget *files_;
    QPlainTextEdit *comparison_,*result_,*status_;
    std::array<QPlainTextEdit*,3> sides_;
    QLabel *hint_, *editorHint_;
    GuardedDialog *editorDialog_;
    FlatSections *tabs_;
    QPushButton *compare_,*merge_,*save_,*continue_,*abort_;
    QList<QPushButton*> resolutionButtons_;
    bool loading_=false,pendingCompare_=false;
    bool pendingRefs_=true;
    QString repository_,requestedRef_;
};
