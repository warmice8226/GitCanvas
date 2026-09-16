#pragma once
#include <QObject>
#include <QString>
#include "git/gitclient.h"
class SyncController final : public QObject {
    Q_OBJECT
public:
    enum class Action { Fetch, Pull, Push, Publish, Diverged, Unavailable };
    explicit SyncController(GitClient *git, QObject *parent=nullptr);
    Action action() const { return action_; }
    QString label() const;
    QString explanation() const { return explanation_; }
    bool working() const { return working_; }
    void invalidate();
    void execute();
    void review(std::function<void()> done);
signals:
    void changed();
    void completed();
    void failed(const QString &message);
    void mergeRequested(const QString &trackingRef);
private:
    void compare(std::function<void(bool)> done);
    void finish(bool ok,const QString &error={});
    GitClient *git_;
    Action action_=Action::Fetch;
    bool fetched_=false,working_=false;
    QString repository_,branch_,upstream_,remote_,remoteRef_,fingerprint_,explanation_;
    QByteArray remoteScope_;
    QString trackingRef_;
    int ahead_=0,behind_=0;
};
