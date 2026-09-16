#pragma once
#include "git/gitclient.h"
#include <QObject>

struct WorktreeReview {
    QString repository, fingerprint, gitDirectory;
    QList<GitStatusEntry> files;
    QByteArray stashes;
};

// A review binds a confirmation to repository state AND the actual modified file bytes.
class WorktreeController final : public QObject {
    Q_OBJECT
public:
    explicit WorktreeController(GitClient *git, QObject *parent = nullptr) : QObject(parent), git_(git) {}
    using ReviewCallback = std::function<void(bool, WorktreeReview, QString)>;
    void review(ReviewCallback callback);
    void execute(const WorktreeReview &review, const QString &action, const QStringList &paths,
                 const QString &value, const QString &stashOid, GitClient::CommandCallback callback);
private:
    GitClient *git_;
};
