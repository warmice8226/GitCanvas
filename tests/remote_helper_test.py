"""Real temporary Git repositories; no SSH server or credentials required."""
import base64
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
import uuid

HELPER = Path(__file__).resolve().parents[1]/"src/ssh/remote_helper.py"
spec = importlib.util.spec_from_file_location("remote_helper", HELPER)
helper = importlib.util.module_from_spec(spec)
spec.loader.exec_module(helper)


class RemoteHelperTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.repo = self.root/"repo ' spaces \ud55c\uae00"
        self.repo.mkdir()
        self.git("init", "-b", "main")
        self.git("config", "user.name", "Remote Author")
        self.git("config", "user.email", "remote@example.invalid")
        self.request = {"version": 1, "root": str(self.root), "repository": str(self.repo)}

    def git(self, *args):
        return subprocess.run(["git", "-C", str(self.repo), *args], check=True, capture_output=True).stdout

    def call(self, action, **data):
        return helper.dispatch(dict(self.request, action=action, **data))

    def write(self, action, **data):
        return self.call(action, expected=self.call("status")["fingerprint"], id=uuid.uuid4().hex, **data)

    def commit(self, contents="base\n"):
        (self.repo/"file.txt").write_text(contents, encoding="utf-8")
        self.write("stage", paths=["file.txt"])
        self.write("commit", message="base\n\nRemote description")

    def test_stage_unstage_commit_and_history(self):
        name="safe;$(echo injected).txt"
        (self.repo/name).write_text("one\n", encoding="utf-8")
        self.write("stage", paths=[name])
        self.assertEqual(self.call("status")["files"][0]["status"], "A ")
        self.write("unstage", paths=[name])
        self.assertTrue((self.repo/name).exists())
        self.write("stage", paths=[name]); self.write("commit", message="first")
        self.assertIn(b"first", base64.b64decode(self.call("history")["data"]))
        self.assertFalse((self.repo/"injected").exists())
        self.write("branch", branch="topic")
        self.assertEqual(self.call("status")["branch"], "topic")
        self.write("switch", branch="main")
        self.write("switch", branch="topic")
        self.write("delete-branch")
        self.assertEqual(self.call("status")["branch"], "main")
        self.assertNotIn("topic", self.call("status")["branches"])
        self.assertIn(b"refs/gitcanvas/ssh-backups/", self.git("for-each-ref", "--format=%(refname)", "refs/gitcanvas"))

    def test_stale_status_and_file_save_backup(self):
        self.commit()
        stale = self.call("status")["fingerprint"]
        (self.repo/"file.txt").write_text("changed\n")
        with self.assertRaisesRegex(ValueError, "STALE"):
            self.call("stage", paths=["file.txt"], expected=stale, id=uuid.uuid4().hex)
        read=self.call("read", path="file.txt")
        original=(self.repo/"file.txt").read_bytes()
        result=self.write("save", path="file.txt", hash=read["hash"], data=base64.b64encode(b"edited\n").decode())
        self.assertEqual(Path(result["backup"]).read_bytes(), original)
        self.assertEqual((self.repo/"file.txt").read_bytes(), b"edited\n")
        with self.assertRaisesRegex(ValueError, "STALE"):
            self.write("save", path="file.txt", hash=read["hash"], data=base64.b64encode(b"overwrite").decode())
        self.assertEqual((self.repo/"file.txt").read_bytes(), b"edited\n")

    def test_scope_and_metadata_protection(self):
        self.commit()
        for name in ("../outside", ".git/config", str(self.root/"outside")):
            with self.assertRaises(ValueError):
                self.call("read", path=name)
        with self.assertRaises(ValueError):
            helper.dispatch(dict(self.request, root=str(self.repo), repository=str(self.root), action="browse"))
        with self.assertRaises(ValueError):
            self.write("reset", mode="hard")
        (self.repo/".git"/"index.lock").write_text("other process")
        with self.assertRaisesRegex(ValueError, "BUSY"):
            self.write("commit", message="blocked")
        self.assertEqual((self.repo/".git"/"index.lock").read_text(), "other process")

    def test_rename_unstage_and_configuration_race(self):
        self.commit();self.git("mv", "file.txt", "renamed.txt")
        self.write("unstage", paths=["renamed.txt"])
        self.assertEqual(self.git("diff", "--cached"), b"")
        self.assertTrue((self.repo/"renamed.txt").exists())
        before=self.call("status")["fingerprint"]
        self.git("config", "remote.origin.url", "https://example.invalid/other.git")
        with self.assertRaisesRegex(ValueError, "STALE"):
            self.call("stage", paths=["renamed.txt"], expected=before, id=uuid.uuid4().hex)

    def test_untracked_content_and_replay(self):
        (self.repo/"new.txt").write_text("old")
        before=self.call("status")["fingerprint"]
        (self.repo/"new.txt").write_text("new")
        self.assertNotEqual(before, self.call("status")["fingerprint"])
        operation=uuid.uuid4().hex
        self.call("stage", paths=["new.txt"], expected=self.call("status")["fingerprint"], id=operation)
        self.assertEqual(self.call("status")["last_operation"]["state"], "completed")
        with self.assertRaisesRegex(ValueError, "already used"):
            self.call("stage", paths=["new.txt"], expected=self.call("status")["fingerprint"], id=operation)

    def test_fetch_gate_push_pull(self):
        self.commit()
        remote=self.root/"remote.git"
        subprocess.run(["git", "init", "--bare", "-b", "main", str(remote)], check=True, capture_output=True)
        self.git("remote", "add", "origin", str(remote));self.git("push", "-u", "origin", "main")
        (self.repo/"file.txt").write_text("second\n");self.write("stage", paths=["file.txt"]);self.write("commit", message="second")
        info=self.call("sync-info")
        with self.assertRaisesRegex(ValueError, "Fetch"):
            self.write("push", fetch_id="missing", upstream=info["upstream"])
        fetched=self.write("fetch");info=self.call("sync-info")
        self.write("push", fetch_id=fetched["fetch_id"], upstream=info["upstream"])
        self.assertEqual(self.git("rev-parse", "HEAD").strip(), subprocess.check_output(["git", "--git-dir", str(remote), "rev-parse", "HEAD"]).strip())
        self.git("reset", "--hard", "HEAD~1")
        fetched=self.write("fetch");info=self.call("sync-info");self.assertEqual(info["behind"],1)
        self.write("pull", fetch_id=fetched["fetch_id"], upstream=info["upstream"])
        self.assertEqual((self.repo/"file.txt").read_text(),"second\n")

    def test_busy_helper_and_protocol_input_limit(self):
        self.commit();directory=self.repo/".git"/"gitcanvas";directory.mkdir(exist_ok=True)
        (directory/"ssh-active.json").write_text(json.dumps({"id":"test", "pid":os.getpid(), "action":"commit"}))
        self.assertTrue(self.call("status")["active"]["alive"])
        with self.assertRaisesRegex(ValueError,"BUSY"):
            self.write("stage", paths=["file.txt"])
        with self.assertRaisesRegex(ValueError,"still be running"):
            self.write("acknowledge")
        result=subprocess.run([sys.executable,str(HELPER)],input=b'{"version":99,"action":"probe"}',capture_output=True,check=True)
        frame=json.loads(result.stdout);self.assertFalse(frame["ok"]);self.assertEqual(frame["type"],"result")


if __name__ == "__main__":
    unittest.main()
