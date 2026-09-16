#pragma once
#include <QWidget>
#include <QByteArray>
#include <QStringList>
#include <QSet>
class QListWidget;
class GitClient;
class QPlainTextEdit;
class QComboBox;
class QPushButton;
class QLabel;
class DiffWidget final : public QWidget {
    Q_OBJECT
public:
    explicit DiffWidget(GitClient *git,QWidget *parent=nullptr);
    QPlainTextEdit *unified() const { return unified_; }
    void clear();
    void showPatch(const QByteArray &patch);
    void setPatchActionsVisible(bool visible);
    void loadWorking(const QString &path,bool staged);
    void loadRevisions(const QString &base,const QString &head,const QString &path);
    static QList<QByteArray> splitHunks(const QByteArray &patch);
    static QByteArray selectedLinesPatch(const QByteArray &patch,const QSet<int> &rows,bool reverse);
signals:
    void indexChanged();
private:
    void display(const QByteArray &bytes,bool patch);
    void applySelected();
    void applyLines();
    void workingPatch(const QByteArray &out,bool staged);
    void applyReviewed(const QByteArray &patch,bool reverse);
    GitClient *git_;
    QPlainTextEdit *unified_,*left_,*right_;
    QComboBox *encoding_,*hunks_;
    QPushButton *apply_;
    QPushButton *applyLines_;
    QListWidget *lines_;
    QLabel *info_;
    QByteArray bytes_;
    QList<QByteArray> patches_;
    QStringList arguments_;
    bool isPatch_=false,editable_=false,reverse_=false;
};
