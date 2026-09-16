"""GitCanvas protocol v1. Executed in memory by SSH; no persistent agent install."""
import base64
import hashlib
import json
import os
from pathlib import Path
import platform
import stat
import subprocess
import sys
import tempfile
import threading
import time

LIMIT = 8 * 1024 * 1024
ENV = dict(os.environ, GIT_TERMINAL_PROMPT="0", GIT_EDITOR="false",
           GIT_SEQUENCE_EDITOR="false", GIT_OPTIONAL_LOCKS="0", LC_ALL="C")
# Inherited remote authentication remains remote-owned. No local env is sent.
for key in tuple(ENV):
    if key.startswith("GIT_") and key not in {"GIT_TERMINAL_PROMPT", "GIT_EDITOR", "GIT_SEQUENCE_EDITOR", "GIT_OPTIONAL_LOCKS", "GIT_SSH_COMMAND", "GIT_SSH"}:
        ENV.pop(key)


def frame(value):
    try:
        print(json.dumps(value, ensure_ascii=True), flush=True)
    except (BrokenPipeError, OSError):
        pass  # A disconnected UI must not interrupt a write in progress.


def run(repo, args, write=False, allow_failure=False):
    command = ["git", "--no-pager", "-C", str(repo), *args]
    process = subprocess.Popen(command, stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
                               stderr=subprocess.PIPE, env=ENV)
    buffers = [bytearray(), bytearray()]
    overflow = [False]

    def drain(pipe, target, maximum):
        while True:
            chunk = pipe.read(65536)
            if not chunk:
                break
            remaining = maximum - len(target)
            target.extend(chunk[:max(0, remaining)])
            if len(chunk) > remaining:
                overflow[0] = True
                if not write:
                    process.kill()
        pipe.close()

    threads = [threading.Thread(target=drain, args=(process.stdout, buffers[0], LIMIT)),
               threading.Thread(target=drain, args=(process.stderr, buffers[1], 256 * 1024))]
    for thread in threads:
        thread.start()
    started = time.monotonic()
    while process.poll() is None:
        try:
            process.wait(timeout=1)
        except subprocess.TimeoutExpired:
            frame({"type": "progress", "command": args[0], "seconds": int(time.monotonic()-started)})
            if not write and time.monotonic() - started > 120:
                process.kill()
    for thread in threads:
        thread.join()
    if overflow[0]:
        raise ValueError("OUTPUT_LIMIT: narrow the query; inspect state after a write")
    if process.returncode and not allow_failure:
        raise ValueError(buffers[1].decode("utf-8", "replace")[:6000] or "Git command failed")
    return bytes(buffers[0]), process.returncode


def text(repo, args, allow_failure=False):
    return run(repo, args, allow_failure=allow_failure)[0].decode("utf-8", "strict").strip()


def within(path, root):
    return path == root or root in path.parents


def file_path(repo, name):
    if not isinstance(name, str) or not name or "\0" in name or Path(name).is_absolute():
        raise ValueError("Invalid relative file path")
    path = repo / name
    if ".." in Path(name).parts or ".git" in Path(name).parts:
        raise ValueError("Unsafe file path")
    if not within(path.resolve(), repo):
        raise ValueError("Path escapes repository")
    # Git may stage symlinks, but this helper deliberately never follows them.
    current = path
    while current != repo:
        if current.is_symlink():
            raise ValueError("Symlink paths require an external tool")
        current = current.parent
    return path


def status_rows(repo):
    raw = run(repo, ["status", "--porcelain=v1", "-z", "--untracked-files=all"])[0]
    fields = raw.split(b"\0")
    rows = []
    index = 0
    while index < len(fields) and fields[index]:
        item = fields[index]; index += 1
        row = {"status": item[:2].decode("ascii"), "path": item[3:].decode("utf-8")}
        if b"R" in item[:2] or b"C" in item[:2]:
            row["original"] = fields[index].decode("utf-8"); index += 1
        rows.append(row)
    return raw, rows


def state(repo):
    raw, rows = status_rows(repo)
    head = text(repo, ["rev-parse", "--verify", "HEAD"], True)
    branch = text(repo, ["symbolic-ref", "--short", "-q", "HEAD"], True)
    gitdir = Path(text(repo, ["rev-parse", "--absolute-git-dir"])).resolve()
    common = Path(text(repo, ["rev-parse", "--path-format=absolute", "--git-common-dir"])).resolve()
    pending = [name for name in ("MERGE_HEAD", "CHERRY_PICK_HEAD", "REVERT_HEAD", "rebase-merge", "rebase-apply", "BISECT_LOG") if (gitdir/name).exists()]
    locks = [str(p.relative_to(base)) for base in {gitdir, common} for p in base.glob("*.lock")]
    digest = hashlib.sha256(head.encode()+b"\0"+branch.encode()+raw)
    digest.update(run(repo, ["config", "--null", "--list", "--show-origin"])[0])
    digest.update(run(repo, ["for-each-ref", "--format=%(refname)%00%(objectname)"])[0])
    digest.update(run(repo, ["diff", "--no-ext-diff", "--no-textconv", "--binary"])[0])
    digest.update(run(repo, ["diff", "--cached", "--no-ext-diff", "--no-textconv", "--binary"])[0])
    index_file = gitdir/"index"
    if index_file.exists():
        if index_file.stat().st_size > LIMIT:
            raise ValueError("Index exceeds 8 MiB safety limit")
        digest.update(index_file.read_bytes())
    # Hash untracked bytes too, so identical status names cannot conceal edits.
    for row in rows:
        if row["status"] == "??":
            path = file_path(repo, row["path"])
            if not path.is_file() or path.stat().st_size > LIMIT:
                raise ValueError("Untracked file is not a regular file of at most 8 MiB")
            digest.update(path.read_bytes())
    active_file = gitdir/"gitcanvas"/"ssh-active.json"
    for directory in (gitdir/"gitcanvas", gitdir/"gitcanvas"/"ssh-results", gitdir/"gitcanvas"/"ssh-file-backups"):
        if directory.is_symlink():
            raise ValueError("Helper metadata cannot be a symlink")
    active = {}
    last = {}
    last_file = gitdir/"gitcanvas"/"ssh-last-result.json"
    if last_file.exists():
        if last_file.is_symlink() or last_file.stat().st_size > 65536:
            raise ValueError("Invalid helper result record")
        last = json.loads(last_file.read_text())
    if active_file.exists():
        if active_file.stat().st_size > 65536:
            raise ValueError("Helper marker is too large")
        active = json.loads(active_file.read_text())
        try:
            os.kill(int(active["pid"]), 0); active["alive"] = True
        except ProcessLookupError:
            active["alive"] = False
        except (OSError, KeyError, ValueError):
            active["alive"] = True  # Cannot establish that the process has exited.
    return {"repository": str(repo), "common_dir": str(common), "git_dir": str(gitdir),
            "fingerprint": digest.hexdigest(), "head": head, "branch": branch,
            "files": rows, "pending": pending, "locks": locks, "active": active, "last_operation": last,
            "branches": text(repo, ["for-each-ref", "--format=%(refname:short)", "refs/heads"]).splitlines(),
            "author": text(repo, ["var", "GIT_AUTHOR_IDENT"], True),
            "remotes": text(repo, ["remote", "-v"])}


def sync_info(repo):
    branch = text(repo, ["symbolic-ref", "--short", "-q", "HEAD"], True)
    if not branch:
        raise ValueError("A local branch with an upstream is required")
    remote = text(repo, ["config", "--get", "branch."+branch+".remote"], True)
    target = text(repo, ["config", "--get", "branch."+branch+".merge"], True)
    if not remote or remote == "." or remote.startswith("-") or not target.startswith("refs/heads/"):
        raise ValueError("Configure a remote upstream on the server first")
    fetch_url = text(repo, ["remote", "get-url", "--all", remote])
    push_url = text(repo, ["remote", "get-url", "--push", "--all", remote])
    if fetch_url != push_url or "\n" in fetch_url:
        raise ValueError("Fetch and Push must use the same single URL")
    upstream = text(repo, ["rev-parse", "--verify", "@{upstream}"])
    ahead, behind = map(int, text(repo, ["rev-list", "--left-right", "--count", "HEAD...@{upstream}"]).split())
    return {"ahead": ahead, "behind": behind, "remote": remote, "target": target, "upstream": upstream, "url": fetch_url}


def atomic_json(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    fd, temporary = tempfile.mkstemp(prefix=".gitcanvas-", dir=path.parent)
    try:
        with os.fdopen(fd, "w", encoding="utf-8") as stream:
            json.dump(value, stream); stream.flush(); os.fsync(stream.fileno())
        os.replace(temporary, path)
    finally:
        if os.path.exists(temporary):
            os.unlink(temporary)


def record_result(path, value):
    atomic_json(path, value)
    atomic_json(path.parent.parent/"ssh-last-result.json", value)


def dispatch(request):
    if request.get("version") != 1:
        raise ValueError("Unsupported helper protocol")
    action = request["action"]
    if action == "probe":
        return {"os": os.name, "platform": platform.system(), "home": str(Path.home()),
                "user": __import__("getpass").getuser(), "python": list(sys.version_info[:3]), "git": text(Path.home(), ["--version"]), "version": 1}
    root_string = request.get("root", "")
    if not isinstance(root_string, str) or not Path(root_string).is_absolute():
        raise ValueError("An absolute allowed root is required")
    root = Path(root_string).resolve(strict=True)
    raw_repo = request.get("repository", "")
    if not isinstance(raw_repo, str) or not Path(raw_repo).is_absolute():
        raise ValueError("An absolute remote path is required")
    repo = Path(raw_repo).resolve(strict=True)
    if not within(repo, root) or not repo.is_dir():
        raise ValueError("Remote path escapes allowed root")
    if action == "browse":
        directories = []
        for child in sorted(repo.iterdir(), key=lambda p: p.name.lower()):
            if child.name == ".git" or child.is_symlink() or not child.is_dir():
                continue
            directories.append({"name": child.name, "path": str(child)})
            if len(directories) == 500:
                break
        top, result = run(repo, ["rev-parse", "--show-toplevel"], allow_failure=True)
        return {"directories": directories, "path": str(repo), "is_repository": result == 0 and Path(top.decode().strip()).resolve() == repo}
    if Path(text(repo, ["rev-parse", "--show-toplevel"])).resolve() != repo:
        raise ValueError("Open the repository root, not a subdirectory")
    if action == "status":
        return state(repo)
    if action == "sync-info":
        return sync_info(repo)
    if action == "history":
        limit = max(1, min(1000, int(request.get("limit", 100))))
        data = run(repo, ["log", "--all", "--date=iso-strict", "--format=%H%x00%P%x00%an%x00%ae%x00%aI%x00%D%x00%s%x00%b%x00%cI", "-z", "-n", str(limit)])[0]
        return {"data": base64.b64encode(data).decode()}
    if action == "diff":
        path = request["path"]; file_path(repo, path)
        args = ["diff", "--no-ext-diff", "--no-textconv"]
        if request.get("staged"):
            args.append("--cached")
        return {"data": base64.b64encode(run(repo, [*args, "--", path])[0]).decode()}
    if action == "read":
        path = file_path(repo, request["path"])
        if not path.is_file() or path.stat().st_size > LIMIT:
            raise ValueError("Only regular files of at most 8 MiB can be edited")
        data = path.read_bytes(); data.decode("utf-8")
        if b"\0" in data:
            raise ValueError("Binary file: editing unavailable")
        return {"data": base64.b64encode(data).decode(), "hash": hashlib.sha256(data).hexdigest()}
    before = state(repo)
    if not within(Path(before["git_dir"]), root) or not within(Path(before["common_dir"]), root):
        raise ValueError("Git metadata lies outside the allowed root; expand the profile root explicitly")
    if request.get("expected") != before["fingerprint"]:
        raise ValueError("STALE: remote files or HEAD changed; refresh before retrying")
    active_path = Path(before["git_dir"])/"gitcanvas"/"ssh-active.json"
    if action == "acknowledge":
        if not before["active"] or before["active"].get("alive", True):
            raise ValueError("Remote helper may still be running")
        active_path.unlink()
        return {"message": "Cleared stale helper marker only. Git locks and pending operations remain."}
    if before["active"] or before["locks"] or before["pending"] or any("U" in row["status"] or row["status"] in ("AA", "DD") for row in before["files"]):
        raise ValueError("BUSY: resolve remote helper, Git operation, conflict, or lock first")
    paths = request.get("paths", [])
    if not isinstance(paths, list) or len(paths) > 5000:
        raise ValueError("Invalid file selection")
    for name in paths:
        file_path(repo, name)
    args = None
    if action in ("stage", "unstage"):
        if not paths:
            raise ValueError("Select files first")
        if action == "unstage":
            for row in before["files"]:
                if row["path"] in paths and row.get("original") and row["original"] not in paths:
                    file_path(repo, row["original"])
                    paths.append(row["original"])
        args = ["add", "--", *paths] if action == "stage" else (["restore", "--staged", "--", *paths] if before["head"] else ["rm", "--cached", "-f", "--", *paths])
    elif action == "commit":
        message = request.get("message", "")
        if not isinstance(message, str) or not message.strip() or len(message.encode()) > 65536:
            raise ValueError("Enter a commit message of at most 64 KiB")
        args = ["commit", "-m", message]
    elif action in ("switch", "branch"):
        name = request.get("branch", "")
        if not isinstance(name, str) or name.startswith("-") or not name or before["files"]:
            raise ValueError("A branch name and clean working tree are required")
        run(repo, ["check-ref-format", "--branch", name])
        if action == "switch" and name not in before["branches"]:
            raise ValueError("Select an existing local branch")
        args = ["switch", *( ["-c"] if action == "branch" else []), name]
    elif action == "fetch":
        args = ["fetch", "--all", "--prune"]
    elif action == "delete-branch":
        destination = next((name for name in ("main", "master") if name in before["branches"] and name != before["branch"]), None)
        if not destination or before["files"] or not before["head"] or before["branch"] in ("main", "master", ""):
            raise ValueError("Delete requires a clean topic branch and an existing main/master")
        args = ["branch", "-d", "--", before["branch"]]
    elif action in ("pull", "push"):
        ticket = Path(before["git_dir"])/"gitcanvas"/"ssh-fetch.json"
        if not ticket.is_file() or ticket.is_symlink():
            raise ValueError("Fetch in this session before Pull/Push")
        fetched = json.loads(ticket.read_text())
        if fetched.get("id") != request.get("fetch_id") or fetched.get("head") != before["head"] or fetched.get("branch") != before["branch"]:
            raise ValueError("Fetch ticket is stale; Fetch again")
        sync = sync_info(repo)
        if sync["upstream"] != request.get("upstream"):
            raise ValueError("Remote tracking data changed; Fetch again")
        if before["files"]:
            raise ValueError("Commit or stash remote working changes first")
        if action == "pull":
            if sync["ahead"] or not sync["behind"]:
                raise ValueError("A fast-forward Pull is required; divergent history needs external merge")
            args = ["merge", "--ff-only", sync["upstream"]]
        else:
            if sync["behind"] or not sync["ahead"]:
                raise ValueError("Receive remote commits before Push")
            run(repo, ["merge-base", "--is-ancestor", sync["upstream"], before["head"]])
            args = ["push", "--force-with-lease="+sync["target"]+":"+sync["upstream"], sync["remote"], "HEAD:"+sync["target"]]
        ticket.unlink()  # Single-use even when connection is lost or a write fails.
    elif action == "save":
        path = file_path(repo, request["path"])
        if not path.is_file() or path.stat().st_size > LIMIT or hashlib.sha256(path.read_bytes()).hexdigest() != request.get("hash"):
            raise ValueError("STALE: remote file changed; read it again")
        data = base64.b64decode(request["data"], validate=True)
        if len(data) > LIMIT or b"\0" in data:
            raise ValueError("File must be UTF-8 text of at most 8 MiB")
        data.decode("utf-8")
    else:
        raise ValueError("Unsupported remote action")
    operation = request.get("id", "")
    if not isinstance(operation, str) or not __import__("re").fullmatch(r"[a-f0-9]{32}", operation):
        raise ValueError("Invalid operation ID")
    directory = Path(before["git_dir"])/"gitcanvas"
    directory.mkdir(parents=True, exist_ok=True)
    if directory.is_symlink():
        raise ValueError("Helper metadata directory cannot be a symlink")
    receipt = directory/"ssh-results"/(operation+".json")
    if receipt.exists():
        raise ValueError("This write ID was already used; inspect the result instead of replaying")
    # Cross-session exclusion. A lost connection never triggers automatic replay.
    with active_path.open("x", encoding="utf-8") as stream:
        json.dump({"id": operation, "pid": os.getpid(), "action": action}, stream)
    record_result(receipt, {"id": operation, "state": "started", "action": action})
    try:
        if action == "save":
            backup = directory/"ssh-file-backups"/(operation+".bin")
            backup.parent.mkdir(exist_ok=True)
            original = path.read_bytes()
            if hashlib.sha256(original).hexdigest() != request["hash"]:
                raise ValueError("STALE: file changed before backup")
            with backup.open("xb") as stream:
                stream.write(original)
            fd, temporary = tempfile.mkstemp(prefix=".gitcanvas-", dir=path.parent)
            try:
                with os.fdopen(fd, "wb") as stream:
                    stream.write(data); stream.flush(); os.fsync(stream.fileno())
                os.chmod(temporary, stat.S_IMODE(path.stat().st_mode))
                if hashlib.sha256(path.read_bytes()).hexdigest() != request["hash"]:
                    raise ValueError("STALE: file changed during save")
                os.replace(temporary, path)
            finally:
                if os.path.exists(temporary):
                    os.unlink(temporary)
            result = {"message": "Saved remote file", "backup": str(backup)}
        else:
            if action == "delete-branch":
                run(repo, ["update-ref", "refs/gitcanvas/ssh-backups/"+operation, before["head"]], write=True)
                run(repo, ["switch", destination], write=True)
            output = run(repo, args, write=True)[0]
            result = {"message": output.decode("utf-8", "replace")[:6000] or "Completed"}
            if action == "fetch":
                atomic_json(directory/"ssh-fetch.json", {"id": operation, "head": before["head"], "branch": before["branch"]})
                result["fetch_id"] = operation
        record_result(receipt, {"id": operation, "state": "completed", "action": action})
        return result
    except Exception:
        record_result(receipt, {"id": operation, "state": "failed_check_repository", "action": action})
        raise
    finally:
        active_path.unlink(missing_ok=True)


def main():
    try:
        raw = sys.stdin.buffer.read(12 * 1024 * 1024 + 1)
        if len(raw) > 12 * 1024 * 1024:
            raise ValueError("Request too large")
        result = dispatch(json.loads(raw))
        frame({"type": "result", "version": 1, "ok": True, "result": result})
    except Exception as error:
        frame({"type": "result", "version": 1, "ok": False, "error": str(error)[:6000]})


if __name__ == "__main__":
    main()
