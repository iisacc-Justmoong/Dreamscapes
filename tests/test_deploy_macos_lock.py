"""Exercise real process locks without invoking native bundle deployment tools."""
import errno
import fcntl
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import time
from types import SimpleNamespace
import unittest
from unittest import mock


ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / "cmake/single-app/deploy_macos.py"
WORKER = r'''
import importlib.util
import json
from pathlib import Path
import sys
import time
from types import SimpleNamespace

script, supplied_app, workspace, token, behavior = sys.argv[1:]
workspace = Path(workspace)
spec = importlib.util.spec_from_file_location("deployment", script)
deployment = importlib.util.module_from_spec(spec)
spec.loader.exec_module(deployment)

def event(kind, app):
    with (workspace / "events.jsonl").open("a") as output:
        output.write(json.dumps({"token": token, "kind": kind, "app": str(app), "time": time.monotonic_ns()}) + "\n")

def body(args, app):
    guard = app / ".deployment-test-active"
    guard.mkdir()
    try:
        event("entered", app)
        (workspace / (token + ".entered")).write_text(str(app))
        if behavior == "error":
            raise RuntimeError("intentional deployment failure")
        sys.stdin.readline()
        event("leaving", app)
    finally:
        guard.rmdir()

deployment._deploy_locked = body
(workspace / (token + ".ready")).write_text("ready")
deployment.deploy(SimpleNamespace(app=Path(supplied_app)))
'''


class DeploymentLockTests(unittest.TestCase):
    def setUp(self):
        (ROOT / "build").mkdir(exist_ok=True)
        self.temporary = tempfile.TemporaryDirectory(prefix="deploy-lock-tests-", dir=ROOT / "build")
        self.workspace = Path(self.temporary.name)
        self.processes = []

    def tearDown(self):
        for process in self.processes:
            if process.poll() is None:
                process.terminate()
            try:
                process.communicate(timeout=5)
            except subprocess.TimeoutExpired:
                process.kill()
                process.communicate(timeout=5)
        self.temporary.cleanup()

    def app(self, name):
        app = self.workspace / (name + ".app")
        app.mkdir()
        return app

    def start(self, app, token, behavior="wait"):
        process = subprocess.Popen(
            [sys.executable, "-B", "-c", WORKER, str(SCRIPT), str(app), str(self.workspace), token, behavior],
            stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        self.processes.append(process)
        self.wait_for(lambda: (self.workspace / (token + ".ready")).exists())
        return process

    def wait_for(self, predicate, timeout=5):
        deadline = time.monotonic() + timeout
        while not predicate():
            self.assertLess(time.monotonic(), deadline, "Timed out waiting for deployment worker")
            time.sleep(0.01)

    def entered(self, token):
        return (self.workspace / (token + ".entered")).exists()

    def finish(self, process, expected=0):
        stdout, stderr = process.communicate(input="release\n", timeout=5)
        self.assertEqual(process.returncode, expected, stdout + stderr)

    def lock_path(self, app):
        app = app.resolve()
        return app.parent / ("." + app.name + ".deploy.lock")

    def test_same_canonical_app_serializes_and_keeps_lock_inode_for_reacquisition(self):
        app = self.app("Shared")
        alias = self.workspace / "Alias.app"
        alias.symlink_to(app, target_is_directory=True)
        first = self.start(app, "first")
        self.wait_for(lambda: self.entered("first"))
        lock = self.lock_path(app)
        inode = lock.stat().st_ino
        second = self.start(alias, "second")
        # The first process is held in the bundle body until explicitly released.
        deadline = time.monotonic() + 0.2
        while time.monotonic() < deadline:
            self.assertFalse(self.entered("second"), "Same bundle mutation bodies overlapped")
            self.assertIsNone(second.poll(), "Second writer failed instead of waiting for the lock")
            time.sleep(0.01)
        self.finish(first)
        self.wait_for(lambda: self.entered("second"))
        self.finish(second)
        events = [json.loads(line) for line in (self.workspace / "events.jsonl").read_text().splitlines()]
        self.assertEqual([(event["token"], event["kind"]) for event in events],
                         [("first", "entered"), ("first", "leaving"), ("second", "entered"), ("second", "leaving")])
        self.assertTrue(all(event["app"] == str(app.resolve()) for event in events))
        self.assertEqual(lock.stat().st_ino, inode)
        self.assertFalse(self.lock_path(alias).is_relative_to(app))
        third = self.start(app, "third")
        self.wait_for(lambda: self.entered("third"))
        self.finish(third)
        self.assertEqual(lock.stat().st_ino, inode)
        self.assertFalse((self.workspace / ".Alias.app.deploy.lock").exists())

    def test_different_apps_can_enter_while_the_first_app_is_still_locked(self):
        first_app, second_app = self.app("First"), self.app("Second")
        first = self.start(first_app, "first")
        self.wait_for(lambda: self.entered("first"))
        second = self.start(second_app, "second")
        self.wait_for(lambda: self.entered("second"))
        self.assertIsNone(first.poll())
        self.assertTrue((first_app / ".deployment-test-active").exists())
        self.assertTrue((second_app / ".deployment-test-active").exists())
        self.finish(second)
        self.finish(first)
        self.assertNotEqual(self.lock_path(first_app), self.lock_path(second_app))

    def test_failed_body_releases_the_persistent_lock_for_the_next_process(self):
        app = self.app("Recovery")
        failed = self.start(app, "failed", "error")
        self.finish(failed, expected=1)
        lock = self.lock_path(app)
        inode = lock.stat().st_ino
        recovered = self.start(app, "recovered")
        self.wait_for(lambda: self.entered("recovered"))
        self.finish(recovered)
        self.assertEqual(lock.stat().st_ino, inode)
        self.assertFalse((app / ".deployment-test-active").exists())

    def test_bundle_preparation_localization_and_signing_remain_inside_the_lock(self):
        spec = importlib.util.spec_from_file_location("deployment", SCRIPT)
        deployment = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(deployment)
        app = self.app("Scope")
        (app / "Contents/MacOS").mkdir(parents=True)
        (app / "Contents/MacOS/Scope").write_text("test executable")
        qt = self.workspace / "Qt/bin/macdeployqt"
        calls = []

        def inside_lock(name, result):
            def call(*args, **kwargs):
                with self.lock_path(app).open("a") as contender:
                    with self.assertRaises(OSError) as blocked:
                        fcntl.flock(contender.fileno(), fcntl.LOCK_EX | fcntl.LOCK_NB)
                    self.assertIn(blocked.exception.errno, (errno.EACCES, errno.EAGAIN))
                calls.append(name)
                return result
            return call

        args = SimpleNamespace(app=app, macdeployqt=qt, search_path=[], library=[], qml_dir=None,
                               log=self.workspace / "deploy.json", identity="-", entitlements=None,
                               compact=False, quick_style=None, embedded_resources_module=[], configuration="Debug")
        with mock.patch.multiple(deployment,
                                 prepare_native=inside_lock("prepare", {}),
                                 qt_plugins=inside_lock("plugins", []),
                                 complete_frameworks=inside_lock("frameworks", None),
                                 binaries=inside_lock("binaries", []),
                                 localize=inside_lock("localize", None),
                                 sign=inside_lock("sign", None)):
            deployment.deploy(args)
        self.assertEqual(calls[0], "prepare")
        self.assertEqual(calls[-1], "sign")
        self.assertTrue((app / "Contents/Resources/qt.conf").is_file())
        with self.lock_path(app).open("a") as contender:
            fcntl.flock(contender.fileno(), fcntl.LOCK_EX | fcntl.LOCK_NB)
            fcntl.flock(contender.fileno(), fcntl.LOCK_UN)


if __name__ == "__main__":
    unittest.main()
