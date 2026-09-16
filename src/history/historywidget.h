#pragma once
#include <QWidget>
#include <QStringList>
#include <QVector>
#include <QCache>
class GitClient;
class QTableWidget;
class QPlainTextEdit;
class QLabel;
class QListWidget;
class QComboBox;
class QLineEdit;
class QPushButton;
class DiffWidget;
class QDialog;
class HistoryTable;
struct HistoryCommit {
    QString hash, author, email, date, refs, subject, body, committedDate;
    QStringList parents;
};
struct HistoryEdge { int from, to, color; };
struct HistoryGraphRow {
    int lane = 0;
    int color = 0;
    bool incoming = false;
    QVector<HistoryEdge> through, parents;
};
class HistoryWidget final : public QWidget {
    Q_OBJECT
public:
    explicit HistoryWidget(GitClient *git, QWidget *parent = nullptr);
    void reload();
    void runHistoryAction(QStringList arguments);
signals:
    void repositoryChanged();
    void rebaseRequested(const QString &base);
public:
    static QVector<HistoryCommit> parseLog(const QByteArray &output);
    static QVector<HistoryGraphRow> buildGraph(const QVector<HistoryCommit> &commits);
private:
    void showCommit();
    void loadFiles();
    void contextMenu(const QPoint &point);
    void reportError(const QString &message);
    GitClient *git_;
    QVector<HistoryCommit> commits_;
    HistoryTable *table_;
    QPlainTextEdit *details_;
    QLabel *summary_, *filesLabel_;
    QListWidget *files_;
    QLineEdit *search_, *author_, *branchFilter_, *pathFilter_, *since_, *until_;
    QPushButton *searchButton_,*more_,*compare_,*clearCompare_;
    QPushButton *detailButton_;
    QComboBox *parent_;
    DiffWidget *diff_;
    QDialog *detailDialog_;
    QString base_,head_,compareBase_,repository_;
    int limit_=100;
    bool hasMore_=false;
    bool pendingSelection_=false;
    using CachedHistory=QPair<QVector<HistoryCommit>,QVector<HistoryGraphRow>>;
    QCache<QByteArray,CachedHistory> cache_{16*1024*1024};
};
