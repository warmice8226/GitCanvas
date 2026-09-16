#include "gitclient.h"

#include <QDir>
#include <QProcessEnvironment>
#include <QFileInfo>
#include <QRegularExpression>

#include <utility>
#include <memory>

GitClient::GitClient(QObject *parent)
    : QObject(parent)
{
}

void GitClient::setRepositoryPath(QString path)
{
    repositoryPath_ = QDir::cleanPath(std::move(path));
    if (state_.repository!=repositoryPath_) {state_={};emit operationStateChanged();}
}

const QString &GitClient::repositoryPath() const
{
    return repositoryPath_;
}

bool GitClient::isBusy() const
{
    return activeProcess_ != nullptr;
}

void GitClient::checkRepository(CommandCallback callback)
{
    run({"rev-parse", "--show-toplevel"}, std::move(callback));
}

void GitClient::loadStatus(std::function<void(bool, QList<GitStatusEntry>, QString)> callback)
{
    run({"status", "--porcelain=v1", "-z", "--untracked-files=all"},
        [callback = std::move(callback)](bool ok, const QByteArray &output, const QString &error) {
            callback(ok, ok ? parsePorcelainStatus(output) : QList<GitStatusEntry>{}, error);
        });
}

void GitClient::stage(const QStringList &paths, CommandCallback callback)
{
    QStringList arguments{"add", "--"};
    arguments.append(paths);
    run(std::move(arguments), std::move(callback));
}

void GitClient::unstage(const QStringList &paths, CommandCallback callback)
{
    run({"rev-parse", "--verify", "HEAD"}, [this, paths, callback](bool hasHead, const QByteArray &, const QString &) {
        QStringList arguments = hasHead ? QStringList{"restore", "--staged", "--"}
                                        : QStringList{"rm", "--cached", "--"};
        arguments.append(paths);
        run(arguments, callback);
    });
}

void GitClient::commit(const QString &message, CommandCallback callback)
{
    run({"commit", "-m", message}, std::move(callback));
}

void GitClient::inspect(const QStringList &arguments, CommandCallback callback)
{
    run(arguments, std::move(callback));
}
void GitClient::inspectEnvironment(const QStringList &arguments, CommandCallback callback)
{
    run(arguments, std::move(callback), {}, 8 * 1024 * 1024, false);
}
void GitClient::inspectLimited(const QStringList &arguments, CommandCallback callback, qint64 limit)
{
    run(arguments,std::move(callback),{},limit);
}
void GitClient::applyPatch(const QByteArray &patch,bool reverse,CommandCallback callback)
{
    QStringList args{"apply","--cached","--whitespace=nowarn"};if(reverse)args.append("--reverse");
    auto check=args;check.append("--check");
    run(check,[this,args,patch,callback](bool ok,const QByteArray &,const QString &error){
        if(!ok){callback(false,{},error);return;}run(args,callback,patch);
    },patch);
}

void GitClient::switchBranch(const QString &name, bool create, CommandCallback callback)
{
    run(create ? QStringList{"switch", "-c", name, "HEAD"} : QStringList{"switch", "--", name}, std::move(callback));
}

void GitClient::renameBranch(const QString &oldName, const QString &newName, CommandCallback callback)
{
    run({"branch", "-m", "--", oldName, newName}, std::move(callback));
}

void GitClient::deleteBranch(const QString &name, CommandCallback callback)
{
    run({"branch", "-d", "--", name}, std::move(callback));
}

void GitClient::deleteCurrentBranch(const QString &expectedBranch, const QString &destination, bool force,
                                  const QString &remote, const QString &remoteRef, CommandCallback callback)
{
    if(!remote.isEmpty()&&!remoteRef.startsWith("refs/heads/")){callback(false,{},tr("원격 삭제 대상은 연결된 브랜치여야 합니다."));return;}
    run({"symbolic-ref","--quiet","--short","HEAD"},[=,this](bool ok,const QByteArray &out,const QString &error){
        if(!ok||QString::fromUtf8(out).trimmed()!=expectedBranch){callback(false,{},error.isEmpty()?tr("현재 브랜치가 바뀌었습니다. 삭제 대상을 다시 확인하세요."):error);return;}
        const QStringList args=destination.isEmpty()?QStringList{"switch","--detach","HEAD"}:QStringList{"switch","--",destination};
        run(args,[=,this](bool ok,const QByteArray &,const QString &error){
            if(!ok){callback(false,{},tr("다른 브랜치로 전환하지 못해 삭제하지 않았습니다.\n")+error);return;}
            run({"branch",force?"-D":"-d","--",expectedBranch},[=,this](bool ok,const QByteArray &out,const QString &error){
                if(!ok){callback(false,{},tr("브랜치는 전환되었지만 삭제는 실패했습니다. 원격은 삭제하지 않았습니다.\n")+error);return;}
                if(remote.isEmpty()){callback(true,out,{});return;}
                run({"-c","remote."+remote+".mirror=false","-c","push.followTags=false","push","--recurse-submodules=no",remote,":"+remoteRef},[callback](bool ok,const QByteArray &out,const QString &error){
                    callback(ok,out,ok?QString():tr("로컬 브랜치는 삭제되었지만 원격 삭제는 실패했습니다. 원격 상태를 확인하세요.\n")+error);
                });
            });
        });
    });
}

void GitClient::sync(const QString &action, CommandCallback callback)
{
    if (action == "fetch") run({"fetch", "--all"}, std::move(callback));
    else if (action == "pull") run({"pull", "--ff-only"}, std::move(callback));
    else if (action == "push") run({"push"}, std::move(callback));
}

namespace {
bool validRemote(const QString &name) {
    return QRegularExpression("^[A-Za-z0-9_][A-Za-z0-9_.-]*$").match(name).hasMatch() && !name.contains("..");
}
bool validSource(const QString &url) {
    return !url.trimmed().isEmpty() && !url.startsWith('-') && !url.contains("::") &&
           !QRegularExpression("[\\x00-\\x1f]").match(url).hasMatch();
}
bool emptyDestination(const QString &path) {
    if (path.isEmpty() || !QFileInfo(path).isAbsolute()) return false;
    const QFileInfo info(path);
    if (info.isSymLink()) return false;
    return !info.exists() || (info.isDir() && !info.isSymLink() && QDir(path).entryList(QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot).isEmpty());
}
QString initDestinationError(const QString &path) {
    const QFileInfo info(path);
    if (path.isEmpty() || !info.isAbsolute() || info.isSymLink() || (info.exists() && !info.isDir()))
        return QObject::tr("새 폴더 또는 기존 프로젝트 폴더의 절대 경로를 입력하세요. 파일이나 심볼릭 링크는 선택할 수 없습니다.");
    const QDir directory(path);
    const QFileInfo marker(directory.filePath(".git"));
    // A .git file also represents a worktree/submodule. Never reinitialize it.
    const bool bare = QFileInfo(directory.filePath("HEAD")).isFile() &&
                      QFileInfo(directory.filePath("objects")).isDir() && QFileInfo(directory.filePath("refs")).isDir();
    if (marker.exists() || marker.isSymLink() || bare)
        return QObject::tr("이미 Git 저장소이거나 Git 관리 정보가 있는 폴더입니다. 기존 저장소는 '저장소 추가'로 여세요.");
    return {};
}
}
void GitClient::saveRemote(const QString &name, const QString &url, bool create, CommandCallback callback) {
    if (!validRemote(name) || !validSource(url)) { callback(false, {}, tr("원격 이름은 영문·숫자·밑줄·점·하이픈으로, 주소는 유효한 URL 또는 경로로 입력하세요.")); return; }
    run({"remote", create ? "add" : "set-url", name, url}, std::move(callback));
}
void GitClient::removeRemote(const QString &name, CommandCallback callback) {
    if (name.isEmpty() || name.startsWith('-')) { callback(false, {}, tr("원격 이름을 확인하세요.")); return; }
    run({"remote", "remove", name}, std::move(callback));
}
void GitClient::setUpstream(const QString &branch, const QString &remote, const QString &target, CommandCallback callback) {
    if (!validRemote(remote) || target.isEmpty()) { callback(false, {}, tr("원격과 대상 브랜치를 선택하세요.")); return; }
    run({"check-ref-format", "refs/heads/" + target}, [this, branch, remote, target, callback](bool ok, const QByteArray &, const QString &error) {
        if (!ok) { callback(false, {}, error.isEmpty() ? tr("원격 브랜치 이름이 올바르지 않습니다.") : error); return; }
        run({"symbolic-ref", "--quiet", "--short", "HEAD"}, [this, branch, remote, target, callback](bool ok, const QByteArray &out, const QString &error) {
            if (!ok || QString::fromUtf8(out).trimmed() != branch) { callback(false, {}, tr("현재 브랜치가 바뀌었습니다. 설정을 다시 여세요.\n") + error); return; }
            run({"remote", "get-url", remote}, [this, branch, remote, target, callback](bool ok, const QByteArray &, const QString &error) {
                if (!ok) { callback(false, {}, error); return; }
                run({"config", "--local", "--replace-all", "branch." + branch + ".remote", remote}, [this, branch, target, callback](bool ok, const QByteArray &, const QString &error) {
                    if (!ok) { callback(false, {}, error); return; }
                    run({"config", "--local", "--replace-all", "branch." + branch + ".merge", "refs/heads/" + target}, [callback](bool ok, const QByteArray &out, const QString &error) {
                        callback(ok, out, ok ? QString() : tr("원격 이름은 저장했지만 대상 브랜치 저장에 실패했습니다. 연결을 다시 설정하세요.\n") + error);
                    });
                });
            });
        });
    });
}
void GitClient::createRepository(const QString &path, const QString &branch, CommandCallback callback) {
    const auto destinationError = initDestinationError(path);
    if (!destinationError.isEmpty()) { callback(false, {}, destinationError); return; }
    inspectEnvironment({"check-ref-format", "refs/heads/" + branch}, [this, path, branch, callback](bool ok, const QByteArray &, const QString &error) {
        if (!ok) { callback(false, {}, error.isEmpty() ? tr("첫 브랜치 이름이 올바르지 않습니다.") : error); return; }
        const auto destinationError = initDestinationError(path);
        if (!destinationError.isEmpty()) { callback(false, {}, destinationError); return; }
        run({"init", "--initial-branch=" + branch, "--", path}, callback, {}, 0, false);
    });
}
void GitClient::cloneRepository(const QString &url, const QString &path, const QString &mode, int depth, const QString &branch, CommandCallback callback) {
    if (!validSource(url) || !emptyDestination(path) || !QStringList{"full", "shallow", "partial"}.contains(mode) || depth < 1) {
        callback(false, {}, tr("주소, Clone 방식과 비어 있는 대상 폴더의 절대 경로를 확인하세요.")); return;
    }
    QStringList args{"clone", "--progress"};
    if (mode == "shallow") args.append({"--no-local", "--depth=" + QString::number(depth)});
    if (mode == "partial") args.append({"--no-local", "--filter=blob:none"});
    if (!branch.isEmpty()) args.append({"--branch", branch});
    args.append({"--", url, path});
    run(args, std::move(callback), {}, 0, false);
}

QList<GitStatusEntry> GitClient::parsePorcelainStatus(const QByteArray &output)
{
    QList<GitStatusEntry> entries;
    const QList<QByteArray> records = output.split('\0');

    for (qsizetype i = 0; i < records.size(); ++i) {
        const QByteArray &record = records.at(i);
        if (record.size() < 4) {
            continue;
        }

        GitStatusEntry entry;
        entry.indexStatus = QChar::fromLatin1(record.at(0));
        entry.workTreeStatus = QChar::fromLatin1(record.at(1));
        entry.path = QString::fromUtf8(record.mid(3));

        const bool isRenameOrCopy = record.at(0) == 'R' || record.at(0) == 'C';
        if (isRenameOrCopy && i + 1 < records.size()) {
            entry.originalPath = QString::fromUtf8(records.at(++i));
        }
        entries.append(std::move(entry));
    }

    return entries;
}

