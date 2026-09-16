#include "gitclient.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QDateTime>
#include <QUuid>

void GitClient::loadOperationState(std::function<void(bool, const QString &)> callback) {
    const auto repository = repositoryPath_;
    run({"rev-parse", "--absolute-git-dir"}, [this, repository, callback](bool ok, const QByteArray &out, const QString &error) {
        if (!ok) { state_ = {}; state_.repository = repository; emit operationStateChanged(); callback(false, error); return; }
        const auto directory = QString::fromUtf8(out).trimmed();
        run({"status", "--porcelain=v1", "-z", "--untracked-files=all"}, [this, repository, directory, callback](bool ok, const QByteArray &out, const QString &error) {
            GitOperationState state; state.repository = repository; state.gitDirectory = directory;
            if (!ok) { state_ = state; emit operationStateChanged(); callback(false, error); return; }
            const QDir dir(directory);
            state.editStop=QFileInfo::exists(dir.filePath("rebase-merge/amend"));
            auto exists = [&](const char *name) { return QFileInfo::exists(dir.filePath(name)); };
            if (exists("rebase-merge")) state.operation = "rebase";
            else if (exists("rebase-apply")) state.operation = exists("rebase-apply/applying") ? "am" : "rebase";
            else if (exists("MERGE_HEAD")) state.operation = "merge";
            else if (exists("CHERRY_PICK_HEAD")) state.operation = "cherry-pick";
            else if (exists("REVERT_HEAD")) state.operation = "revert";
            else if (exists("sequencer")) {
                QFile todo(dir.filePath("sequencer/todo")); const auto first = todo.open(QIODevice::ReadOnly) ? todo.readLine() : QByteArray();
                state.operation = first.startsWith("revert ") ? "revert" : first.startsWith("pick ") ? "cherry-pick" : "sequencer";
            } else if (exists("BISECT_LOG")) state.operation = "bisect";
            const auto records = out.split('\0');
            for (qsizetype i=0; i<records.size(); ++i) {
                const auto record = records[i]; if (record.size()<4) continue;
                const auto xy = record.left(2);
                if (xy.contains('U') || xy=="AA" || xy=="DD") state.conflicts.append(QString::fromUtf8(record.mid(3)));
                if (xy[0]=='R' || xy[0]=='C') ++i;
            }
            QStringList lockDirs{directory};
            QFile common(dir.filePath("commondir"));
            if (common.open(QIODevice::ReadOnly)) lockDirs.append(QDir::cleanPath(dir.filePath(QString::fromUtf8(common.readAll()).trimmed())));
            lockDirs.removeDuplicates();
            for (const auto &lockDir : lockDirs) for (const auto *name : {"index.lock","HEAD.lock","config.lock","packed-refs.lock","shallow.lock"}) {
                const auto path=QDir(lockDir).filePath(name); if(QFileInfo::exists(path)) state.locks.append(path);
            }
            QCryptographicHash hash(QCryptographicHash::Sha256); hash.addData(repository.toUtf8()); hash.addData(out);
            hash.addData(state.operation.toUtf8()); hash.addData(state.locks.join('\n').toUtf8());
            for (const auto *name : {"HEAD","ORIG_HEAD","MERGE_HEAD","CHERRY_PICK_HEAD","REVERT_HEAD","index","BISECT_LOG","BISECT_START","BISECT_HEAD",
                    "rebase-merge/amend","rebase-merge/head-name","rebase-merge/onto","rebase-merge/orig-head","rebase-merge/stopped-sha",
                    "rebase-merge/git-rebase-todo","rebase-merge/done","rebase-apply/next","rebase-apply/last","sequencer/todo"}) {
                QFile file(dir.filePath(name));
                if (!file.exists()) continue;
                if (!file.open(QIODevice::ReadOnly) || !hash.addData(&file)) {
                    state_ = state; emit operationStateChanged(); callback(false,tr("작업 상태 파일을 읽을 수 없습니다.")); return;
                }
            }
            state.fingerprint = QString::fromLatin1(hash.result().toHex());
            run({"rev-parse","--verify","HEAD"},[this,state,callback](bool ok,const QByteArray &head,const QString &) mutable {
                state.fingerprint=QString::fromLatin1(QCryptographicHash::hash(state.fingerprint.toUtf8()+(ok?head:QByteArray("unborn")),QCryptographicHash::Sha256).toHex());
                state.known=true;state_=state;emit operationStateChanged();callback(true,{});
            });
        });
    });
}

void GitClient::recoverOperation(const QString &expectedFingerprint, bool abort, CommandCallback callback) {
    loadOperationState([this, expectedFingerprint, abort, callback](bool ok, const QString &error) {
        if (!ok) { callback(false, {}, error); return; }
        if (state_.fingerprint != expectedFingerprint || state_.operation.isEmpty()) {
            callback(false, {}, tr("확인 이후 Git 작업 상태가 바뀌었습니다. 새 상태를 확인하고 다시 실행하세요.")); return;
        }
        if (!state_.locks.isEmpty() || (!abort && !state_.conflicts.isEmpty())) {
            callback(false, {}, tr("잠금 파일 또는 해결하지 않은 충돌이 있습니다. 파일을 해결하고 Stage한 뒤 다시 확인하세요.")); return;
        }
        const auto operation = state_.operation;
        if (!QStringList{"merge","rebase","cherry-pick","revert","am"}.contains(operation)) {
            callback(false, {}, tr("이 작업의 계속/취소는 외부 Git 도구에서 진행하세요.")); return;
        }
        const auto backup = "refs/gitcanvas/recovery/" + QUuid::createUuid().toString(QUuid::WithoutBraces);
        run({"update-ref", backup, "HEAD"}, [this, operation, abort, backup, callback](bool ok, const QByteArray &, const QString &error) {
            if (!ok) { callback(false, {}, tr("복구용 HEAD 참조를 저장하지 못했습니다.\n") + error); return; }
            run({"-c", "core.editor=true", operation, abort ? "--abort" : "--continue"},
                [this, backup, callback](bool ok, const QByteArray &out, const QString &error) {
                    const auto result = out;
                    const auto message = error;
                    loadOperationState([this, ok, result, message, backup, callback](bool checked, const QString &stateError) {
                        diagnostic_ = (ok ? tr("복구 작업 명령이 완료되었습니다. 현재 상태를 확인하세요.") : message) +
                            tr("\n실행 전 HEAD: %1\n이 참조는 미커밋 파일의 백업이 아닙니다.").arg(backup);
                        if (!checked) diagnostic_ += "\n" + stateError;
                        emit diagnosticChanged(); emit recoveryRequired();
                        callback(ok && checked, result, ok && checked ? QString() : diagnostic_);
                    });
                }, {}, 0, true, true);
        });
    });
}
