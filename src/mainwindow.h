#pragma once
#include "git/gitclient.h"
#include <QMainWindow>
#include <QHash>
class QLabel;
class QListWidget;
class QPushButton;
class QPlainTextEdit;
class QComboBox;
class QTabWidget;
class QLineEdit;
class QCheckBox;
class HistoryWidget;
class SyncController;
class DiffWidget;
class WorktreePanel;
class Diagnostics;
class QTimer;
class MainWindow final : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    void openRepository(const QString &path);
private:
    void closeEvent(QCloseEvent *event) override;
    void buildUi();
    void showAppSettings();
    void appendLog(const QString &text);
    Diagnostics *diagnostics_;
    void refreshStatus();
    void repositoryChanged();
    QTimer *refreshTimer_;
    QTimer *creationOpenTimer_;
    QString pendingCreatedRepository_, creationNoticePath_;
    QWidget *creationNoticePanel_;
    QLabel *creationNotice_;
    void addCreatedRepository(const QString &path);
    void showCreationNotice(const QString &message);
    void refreshMetadata();
    void loadDiff(bool staged);
    void stageSelected(bool unstage);
    void stageAll(bool unstage);
    void deleteCurrentBranch();
    void commitChanges();
    void showGitSettings();
    void createRepository(bool clone);
    void repositoryMenu(const QPoint &point);
    void relocateRepository(int row, const QString &path);
    void saveDrafts();
    void showError(const QString &message);
    void updateActions();
    void loadRepositories();
    void saveRepositories();
    void renderRepositories(const QString &selected = {});
    void moveRepository(int direction);
    struct SavedRepository { QString path; bool favorite = false; QString displayName; };
    QList<SavedRepository> repositories_;
    QHash<QString, QStringList> commitDrafts_;
    QListWidget *repositoryList_;
    QPushButton *favoriteButton_, *moveUpButton_, *moveDownButton_;
    QStringList selectedPaths(QListWidget *list) const;
    GitClient git_;
    QString validRepository_;
    QLabel *repositoryLabel_, *branchLabel_, *pathLabel_, *countsLabel_, *diffTitle_;
    QListWidget *unstagedList_, *stagedList_;
    QPlainTextEdit *commitMessage_, *diff_, *log_;
    HistoryWidget *history_;
    DiffWidget *diffPanel_;
    WorktreePanel *worktreePanel_;
    QLineEdit *commitTitle_;
    QCheckBox *unstagedAll_, *stagedAll_;
    QCheckBox *amend_, *signOff_;
    QString amendHead_;
    QComboBox *branches_;
    QTabWidget *tabs_;
    QPushButton *openButton_, *refreshButton_, *stageButton_, *unstageButton_, *commitButton_, *newBranchButton_, *renameBranchButton_, *deleteBranchButton_;
    SyncController *sync_;
    QPushButton *syncButton_;
    QPushButton *stageAllButton_, *unstageAllButton_;
    QPushButton *gitSettingsButton_;
    QPushButton *initButton_, *cloneButton_, *remoteButton_;
};
