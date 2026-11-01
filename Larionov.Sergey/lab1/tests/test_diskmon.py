#!/usr/bin/env python3
"""Real Linux processes, inotify, signals and /proc; only log capture is interposed."""
import ctypes
import os
from pathlib import Path
import re
import signal
import subprocess
import tempfile
import time
import unittest


LAB = Path(__file__).resolve().parents[1]
BINARY = LAB / "diskmon"


def wait_for(predicate, timeout=5):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if predicate():
            return
        time.sleep(0.02)
    raise AssertionError("Timed out waiting for condition")


def alive(pid):
    try:
        status = Path(f"/proc/{pid}/stat").read_text()
        return status.rsplit(")", 1)[1].split()[0] != "Z"
    except FileNotFoundError:
        return False


class DiskmonTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        # Adopt double-forked daemons so the test runner can reap them itself.
        libc = ctypes.CDLL(None, use_errno=True)
        if libc.prctl(36, 1, 0, 0, 0) != 0:  # PR_SET_CHILD_SUBREAPER
            raise OSError(ctypes.get_errno(), "prctl")

    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="diskmon-test-")
        self.root = Path(self.temporary.name)
        self.first = self.root / "first"
        self.second = self.root / "second with spaces"
        self.first.mkdir()
        self.second.mkdir()
        self.configuration = self.root / "monitor.conf"
        self.configuration.write_text("# relative path\nfirst\n")
        self.pid_file = self.root / "daemon.pid"
        self.log = self.root / "capture.txt"
        self.environment = os.environ.copy()
        self.environment["LD_PRELOAD"] = os.environ["DISKMON_CAPTURE"]
        self.environment["DISKMON_TEST_LOG"] = str(self.log)
        self.pids = []

    def tearDown(self):
        for pid in self.pids:
            if alive(pid):
                os.kill(pid, signal.SIGTERM)
        for pid in self.pids:
            try:
                wait_for(lambda: not alive(pid))
            except AssertionError:
                os.kill(pid, signal.SIGKILL)
            try:
                os.waitpid(pid, 0)
            except ChildProcessError:
                pass
        # Reap failed startup descendants as well.
        while True:
            try:
                if os.waitpid(-1, os.WNOHANG)[0] == 0:
                    break
            except ChildProcessError:
                break
        self.temporary.cleanup()

    def lines(self):
        return self.log.read_text() if self.log.exists() else ""

    def message(self, text):
        wait_for(lambda: text in self.lines())

    def launch(self, configuration=None, pid_file=None):
        return subprocess.run(
            [str(BINARY), str(configuration or self.configuration), str(pid_file or self.pid_file)],
            cwd=self.root, env=self.environment, text=True, capture_output=True, timeout=10,
        )

    def start(self):
        result = self.launch()
        self.assertEqual(result.returncode, 0, result.stderr)
        pid = int(re.search(r"OK pid=(\d+)", result.stdout)[1])
        self.pids.append(pid)
        self.assertEqual(int(self.pid_file.read_text()), pid)
        self.assertTrue(alive(pid))
        return pid

    def event(self, name, path):
        self.message(f'event={name} path="{path}"')

    def reload(self, pid, text, success=True):
        before = self.lines().count("RELOAD_OK" if success else "RELOAD_FAILED")
        self.configuration.write_text(text)
        os.kill(pid, signal.SIGHUP)
        marker = "RELOAD_OK" if success else "RELOAD_FAILED"
        wait_for(lambda: self.lines().count(marker) > before)

    def test_daemon_context_and_term(self):
        pid = self.start()
        self.assertEqual(os.readlink(f"/proc/{pid}/cwd"), "/")
        self.assertNotEqual(os.getsid(pid), os.getsid(0))
        self.assertNotEqual(os.getsid(pid), pid)  # double fork
        for descriptor in range(3):
            self.assertEqual(os.readlink(f"/proc/{pid}/fd/{descriptor}"), "/dev/null")
        os.kill(pid, signal.SIGTERM)
        wait_for(lambda: not alive(pid))
        self.message(f"pid={pid} STOP signal=15")
        self.assertEqual(self.pid_file.read_text(), "")

    def test_file_operations_and_local0(self):
        self.start()
        path = self.first / "example.txt"
        path.write_text("one")
        self.event("CREATE", path)
        self.event("OPEN", path)
        self.event("MODIFY", path)
        self.event("CLOSE_WRITE", path)
        self.assertEqual(path.read_text(), "one")
        self.event("ACCESS", path)
        self.event("CLOSE_NOWRITE", path)
        path.chmod(0o600)
        self.event("ATTRIB", path)
        renamed = self.first / "renamed.txt"
        path.rename(renamed)
        self.event("MOVED_FROM", path)
        self.event("MOVED_TO", renamed)
        cookies = [re.search(r"cookie=(\d+)", line)[1] for line in self.lines().splitlines()
                   if 'event=MOVED_' in line]
        self.assertEqual(len(cookies), 2)
        self.assertEqual(cookies[0], cookies[1])
        self.assertNotEqual(cookies[0], "0")
        renamed.unlink()
        self.event("DELETE", renamed)
        self.assertTrue(all(line.startswith("facility=16 ") for line in self.lines().splitlines()))

    def test_existing_recursive_tree(self):
        nested = self.first / "existing" / "deeper"
        nested.mkdir(parents=True)
        self.start()
        path = nested / "deep.txt"
        path.write_text("recursive")
        self.event("CREATE", path)

    def test_new_directory_and_moved_in_tree(self):
        self.start()
        nested = self.first / "new"
        nested.mkdir()
        self.message(f'WATCH_ADDED path="{nested}"')
        (nested / "file.txt").write_text("new")
        self.event("CREATE", nested / "file.txt")
        outside = self.root / "incoming"
        (outside / "deep").mkdir(parents=True)
        destination = self.first / "incoming"
        outside.rename(destination)
        self.message(f'WATCH_ADDED path="{destination}"')
        (destination / "deep" / "inside.txt").write_text("moved")
        self.event("CREATE", destination / "deep" / "inside.txt")

    def test_renamed_directory_has_current_paths(self):
        nested = self.first / "old" / "deep"
        nested.mkdir(parents=True)
        self.start()
        renamed = self.first / "new"
        nested.parent.rename(renamed)
        path = renamed / "deep" / "file.txt"
        path.write_text("renamed")
        self.event("CREATE", path)
        self.assertNotIn(f'event=CREATE path="{nested}/file.txt"', self.lines())

    def test_moved_out_directory_no_longer_monitored(self):
        nested = self.first / "nested"
        nested.mkdir()
        self.start()
        outside = self.root / "outside"
        nested.rename(outside)
        self.event("MOVED_FROM", nested)
        path = outside / "not-monitored.txt"
        path.write_text("outside")
        time.sleep(0.2)
        self.assertNotIn(str(path), self.lines())

    def test_multiple_roots_spaces_and_duplicate_elimination(self):
        self.configuration.write_text("first\nsecond with spaces\nfirst\nfirst\n")
        self.start()
        path = self.second / "файл с пробелами.txt"
        path.write_text("unicode")
        self.event("CREATE", path)
        self.assertEqual(self.lines().count(f'event=CREATE path="{path}"'), 1)

    def test_symlink_directory_is_not_followed(self):
        outside = self.root / "outside"
        outside.mkdir()
        (self.first / "link").symlink_to(outside, target_is_directory=True)
        (self.first / "loop").symlink_to(self.first, target_is_directory=True)
        self.start()
        (outside / "secret.txt").write_text("outside")
        time.sleep(0.2)
        self.assertNotIn("secret.txt", self.lines())

    def test_sighup_uses_original_absolute_config_path(self):
        # Run with a relative configuration path; daemon later chdir('/')s.
        result = subprocess.run([str(BINARY), "monitor.conf", "daemon.pid"],
                                cwd=self.root, env=self.environment, text=True,
                                capture_output=True, timeout=10)
        self.assertEqual(result.returncode, 0, result.stderr)
        pid = int(self.pid_file.read_text())
        self.pids.append(pid)
        self.reload(pid, "second with spaces\n")
        path = self.second / "after-reload.txt"
        path.write_text("new root")
        self.event("CREATE", path)
        (self.first / "old-root.txt").write_text("old root")
        time.sleep(0.2)
        self.assertNotIn("old-root.txt", self.lines())

    def test_invalid_reload_keeps_old_monitor(self):
        pid = self.start()
        self.reload(pid, "missing-directory\n", success=False)
        path = self.first / "still-watched.txt"
        path.write_text("old configuration")
        self.event("CREATE", path)
        self.assertTrue(alive(pid))

    def test_repeated_reload_does_not_leak_descriptors(self):
        pid = self.start()
        count = len(list(Path(f"/proc/{pid}/fd").iterdir()))
        for _ in range(12):
            self.reload(pid, "first\n")
        self.assertEqual(len(list(Path(f"/proc/{pid}/fd").iterdir())), count)

    def test_repeated_start_replaces_old_process(self):
        old = self.start()
        new = self.start()
        self.assertNotEqual(new, old)
        wait_for(lambda: not alive(old))
        self.message(f"pid={old} STOP signal=15")
        self.message(f"REPLACE previous_pid={old}")
        self.assertEqual(int(self.pid_file.read_text()), new)

    def test_simultaneous_launches_leave_one_instance(self):
        launches = [subprocess.Popen(
            [str(BINARY), str(self.configuration), str(self.pid_file)],
            cwd=self.root, env=self.environment, text=True,
            stdout=subprocess.PIPE, stderr=subprocess.PIPE) for _ in range(4)]
        results = []
        for launch in launches:
            stdout, stderr = launch.communicate(timeout=10)
            results.append((launch.returncode, stderr))
            if launch.returncode == 0:
                self.pids.append(int(re.search(r"OK pid=(\d+)", stdout)[1]))
        for code, stderr in results:
            self.assertEqual(code, 0, stderr)
        current = int(self.pid_file.read_text())
        wait_for(lambda: sum(alive(pid) for pid in self.pids) == 1)
        self.assertTrue(alive(current))

    def test_rebuild_and_failed_build_cleanup(self):
        old = self.start()
        result = subprocess.run([str(LAB / "build.sh")], text=True,
                                capture_output=True, timeout=40)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertTrue(alive(old))
        new = self.start()
        wait_for(lambda: not alive(old))
        self.assertTrue(alive(new))
        environment = os.environ.copy()
        environment["CXX"] = "false"
        before = BINARY.read_bytes()
        result = subprocess.run([str(LAB / "build.sh")], env=environment,
                                text=True, capture_output=True, timeout=5)
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(BINARY.read_bytes(), before)
        self.assertEqual(list(LAB.glob(".build.*")), [])

    def test_root_rename_outside_and_recovery_by_hup(self):
        pid = self.start()
        outside = self.root / "renamed-root"
        self.first.rename(outside)
        self.message("ROOT_MOVED")
        (outside / "outside.txt").write_text("outside")
        time.sleep(0.15)
        self.assertNotIn("outside.txt", self.lines())
        self.reload(pid, "renamed-root\n")
        path = outside / "after-hup.txt"
        path.write_text("inside again")
        self.event("CREATE", path)

    def test_invalid_start_does_not_stop_running_instance(self):
        pid = self.start()
        invalid = self.root / "invalid.conf"
        invalid.write_text("missing\n")
        result = self.launch(configuration=invalid)
        self.assertNotEqual(result.returncode, 0)
        self.assertTrue(alive(pid))
        self.assertEqual(int(self.pid_file.read_text()), pid)

    def test_stale_pid_after_sigkill(self):
        old = self.start()
        os.kill(old, signal.SIGKILL)
        os.waitpid(old, 0)
        new = self.start()
        self.assertNotEqual(new, old)

    def test_unrelated_pid_is_never_signalled(self):
        sleeper = subprocess.Popen(["sleep", "30"])
        try:
            self.pid_file.write_text(f"{sleeper.pid}\n")
            result = self.launch()
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("unrelated process", result.stderr)
            self.assertIsNone(sleeper.poll())
        finally:
            sleeper.terminate()
            sleeper.wait()

    def test_invalid_empty_and_missing_configurations(self):
        for text in ("", "# comment only\n", "does-not-exist\n"):
            with self.subTest(text=text):
                self.configuration.write_text(text)
                result = self.launch()
                self.assertNotEqual(result.returncode, 0)
                self.assertFalse(self.pid_file.exists())
        self.configuration.unlink()
        self.assertNotEqual(self.launch().returncode, 0)

    def test_invalid_pid_and_symlink_pid_file(self):
        for text in ("0\n", "1\n", "-1\n", "9999999999999999\n", "12 extra\n", "abc\n"):
            with self.subTest(text=text):
                self.pid_file.write_text(text)
                self.assertNotEqual(self.launch().returncode, 0)
        target = self.root / "must-not-change"
        target.write_text("original")
        self.pid_file.unlink()
        self.pid_file.symlink_to(target)
        self.assertNotEqual(self.launch().returncode, 0)
        self.assertEqual(target.read_text(), "original")

    def test_control_characters_cannot_forge_log_lines(self):
        self.start()
        path = self.first / 'bad\nFAKE "percent%s".txt'
        path.write_text("escaped")
        self.message(r'bad\x0aFAKE \"percent%s\".txt')
        self.assertTrue(all(line.startswith("facility=16 ") for line in self.lines().splitlines()))

    def test_check_mode_and_overlapping_roots(self):
        nested = self.first / "nested"
        nested.mkdir()
        self.configuration.write_text("first/nested\nfirst\nfirst/nested\n")
        result = subprocess.run([str(BINARY), "--check", str(self.configuration)],
                                text=True, capture_output=True, timeout=5)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("1 root(s)", result.stdout)
        self.assertFalse(self.pid_file.exists())

    def test_root_deletion_is_reported(self):
        self.start()
        self.first.rmdir()
        self.event("DELETE_SELF", self.first)


if __name__ == "__main__":
    unittest.main(verbosity=2)
