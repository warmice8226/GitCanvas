#pragma once

#include "gitstatusentry.h"

#include <QByteArray>
#include <QObject>
#include <QProcess>
#include <QString>
#include <QStringList>

#include <functional>
#include <QElapsedTimer>

struct GitOperationState {
    QString repository, gitDirectory, operation, fingerprint;
    QStringList conflicts, locks;
    bool known = false;
    bool editStop = false;
};

class GitClient final : public QObject
{
    Q_OBJECT

public:
    using CommandCallback = std::function<void(bool, const QByteArray &, const QString &)>;

    explicit GitClient(QObject *parent = nullptr);

    void setRepositoryPath(QString path);
    [[nodiscard]] const QString &repositoryPath() const;
    [[nodiscard]] bool isBusy() const;
    bool canCancel() const { return activeProcess_ && cancellable_ && !stopping_ && bool(terminateTree_); }
    QString activityText() const { return activity_; }
    const GitOperationState &operationState() const { return state_; }
    QString lastDiagnostic() const { return diagnostic_; }
    void cancelActive();
    void setTimeoutMilliseconds(int milliseconds);
    void loadOperationState(std::function<void(bool, const QString &)> callback);
    void recoverOperation(const QString &expectedFingerprint, bool abort, CommandCallback callback);

    void checkRepository(CommandCallback callback);
    void loadStatus(std::function<void(bool, QList<GitStatusEntry>, QString)> callback);
    void stage(const QStringList &paths, CommandCallback callback);
    void unstage(const QStringList &paths, CommandCallback callback);
    void commit(const QString &message, CommandCallback callback);
    void commitWithOptions(const QString &message, bool amend, bool signOff,
                           const QString &expectedHead, CommandCallback callback);
    void inspect(const QStringList &arguments, CommandCallback callback);
    // Environment/configuration queries also work before a repository is opened.
    void inspectEnvironment(const QStringList &arguments, CommandCallback callback);
    void inspectLimited(const QStringList &arguments, CommandCallback callback, qint64 limit = 8 * 1024 * 1024);
    // Reviewed toolbox writes are always treated as writes, even when options
    // resemble a read-only command (e.g. log --output or config --get + --set).
    void executeReviewed(const QStringList &arguments, CommandCallback callback, const QByteArray &input = {});
    void applyPatch(const QByteArray &patch, bool reverse, CommandCallback callback);
    void switchBranch(const QString &name, bool create, CommandCallback callback);
    void renameBranch(const QString &oldName, const QString &newName, CommandCallback callback);
    void deleteBranch(const QString &name, CommandCallback callback);
    void deleteCurrentBranch(const QString &expectedBranch, const QString &destination, bool force,
                             const QString &remote, const QString &remoteRef, CommandCallback callback);
    void sync(const QString &action, CommandCallback callback);
    void saveRemote(const QString &name, const QString &url, bool create, CommandCallback callback);
    void removeRemote(const QString &name, CommandCallback callback);
    void setUpstream(const QString &branch, const QString &remote, const QString &target, CommandCallback callback);
    void createRepository(const QString &path, const QString &branch, CommandCallback callback);
    void cloneRepository(const QString &url, const QString &path, const QString &mode, int depth,
                         const QString &branch, CommandCallback callback);

signals:
    void commandFinished(const QString &command,bool success,int exitCode,qint64 elapsedMs,const QString &error,bool mayWrite);
    void outputProgress(qint64 outputBytes,qint64 errorBytes);
    void busyChanged(bool busy);
    void commandStarted(const QString &displayCommand);
    void cloneProgress(const QString &text);
    void activityChanged();
    void operationStateChanged();
    void recoveryRequired();
    void diagnosticChanged();

private:
    void run(QStringList arguments, CommandCallback callback, QByteArray input = {}, qint64 limit = 0,
             bool requiresRepository = true, bool checked = false, bool conservative = false);
    void stopActive(bool timeout);
    static QList<GitStatusEntry> parsePorcelainStatus(const QByteArray &output);

    QString repositoryPath_;
    QProcess *activeProcess_ = nullptr;
    GitOperationState state_;
    QString activity_, diagnostic_, stopReason_;
    bool cancellable_ = false, stopping_ = false;
    int timeoutMs_ = 120000;
    std::function<void()> terminateTree_;
};

