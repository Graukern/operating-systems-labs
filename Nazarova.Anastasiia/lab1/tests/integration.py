#!/usr/bin/env python3
import ctypes
import json
import os
from pathlib import Path
import shutil
import signal
import subprocess
import sys
import tempfile
import time
import unittest

ROOT = Path(__file__).resolve().parents[1]
BINARY = ROOT / "disk-monitor"
SYSLOG = os.environ.get("DISK_MONITOR_SYSLOG_LOG")


def wait_for(predicate, message, timeout=6):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        value = predicate()
        if value:
            return value
        time.sleep(0.025)
    raise AssertionError(message)


def alive(pid):
    try:
        stat = Path(f"/proc/{pid}/stat").read_text()
        return stat.rsplit(")", 1)[1].split()[0] != "Z"
    except FileNotFoundError:
        return False


class MonitorTests(unittest.TestCase):
    def setUp(self):
        self.root = Path(tempfile.mkdtemp(prefix="disk-monitor test-"))
        self.first = self.root / "first"
        self.second = self.root / "second"
        self.events = self.root / "events"
        for directory in (self.first, self.second, self.events):
            directory.mkdir()
        self.config = self.root / "monitor.conf"
        self.pidfile = self.root / "service.pid"
        self.pids = []
        self.write_config()

    def write_config(self, watches=None, events=None, extra=""):
        watches = [self.first] if watches is None else watches
        text = "".join(f"watch={p.name}\n" for p in watches)
        text += f"event_log_dir={(events or self.events).name}\npid_file=service.pid\n" + extra
        replacement = self.root / "config.tmp"
        replacement.write_text(text)
        replacement.replace(self.config)

    def launch(self, success=True):
        result = subprocess.run([str(BINARY), "monitor.conf"], cwd=self.root,
                                text=True, capture_output=True, timeout=12)
        if success:
            self.assertEqual(result.returncode, 0, result.stderr)
            pid = int(self.pidfile.read_text().strip())
            self.pids.append(pid)
            self.assertTrue(alive(pid))
            return pid
        self.assertNotEqual(result.returncode, 0, result.stderr)
        return result

    def records(self):
        result = []
        for file in self.events.glob("*.log"):
            try:
                result.append(json.loads(file.read_text()))
            except (json.JSONDecodeError, FileNotFoundError):
                pass  # Файл ещё записывается.
        return result

    def event(self, kind, path):
        return next((e for e in self.records()
                     if kind in e["event"].split("|") and e["path"] == str(path)), None)

    def expect(self, kind, path):
        return wait_for(lambda: self.event(kind, path), f"Missing {kind} for {path}")

    def stop(self, pid):
        os.kill(pid, signal.SIGTERM)
        wait_for(lambda: not alive(pid), "SIGTERM did not stop daemon")
        self.assertEqual(self.pidfile.read_text(), "")

    def syslog(self):
        if SYSLOG is None:
            self.skipTest("Set DISK_MONITOR_SYSLOG_LOG")
        try:
            text = Path(SYSLOG).read_text()
        except PermissionError:
            text = subprocess.check_output(["sudo", "-n", "cat", SYSLOG], text=True)
        except FileNotFoundError:
            return ""
        return "\n".join(line for line in text.splitlines()
                         if any(f"disk-monitor[{pid}]" in line for pid in self.pids))

    def tearDown(self):
        for pid in self.pids:
            if alive(pid):
                os.kill(pid, signal.SIGTERM)
                try:
                    wait_for(lambda: not alive(pid), "cleanup failed")
                except AssertionError:
                    os.kill(pid, signal.SIGKILL)
        while True:
            try:
                if os.waitpid(-1, os.WNOHANG)[0] == 0:
                    break
            except ChildProcessError:
                break
        shutil.rmtree(self.root)

    def test_daemon_context_and_sigterm(self):
        pid = self.launch()
        self.assertEqual(os.readlink(f"/proc/{pid}/cwd"), "/")
        for fd in (0, 1, 2):
            self.assertEqual(os.readlink(f"/proc/{pid}/fd/{fd}"), "/dev/null")
        stat = Path(f"/proc/{pid}/stat").read_text().rsplit(")", 1)[1].split()
        self.assertEqual(int(stat[4]), 0, "daemon has a controlling terminal")
        self.assertNotEqual(os.getsid(pid), os.getsid(0))
        self.stop(pid)

    def test_file_operations_and_one_log_per_event(self):
        self.write_config([self.first, self.second])
        self.launch()
        file = self.first / "пример с пробелами.txt"
        file.write_text("first\n")
        for event in ("CREATE", "OPEN", "MODIFY", "CLOSE_WRITE"):
            self.expect(event, file)
        self.assertEqual(file.read_text(), "first\n")
        self.expect("ACCESS", file)
        self.expect("CLOSE_NOWRITE", file)
        file.chmod(0o640)
        self.expect("ATTRIB", file)
        moved = self.second / "renamed.txt"
        file.rename(moved)
        old_event = self.expect("MOVED_FROM", file)
        new_event = self.expect("MOVED_TO", moved)
        self.assertGreater(old_event["cookie"], 0)
        self.assertEqual(old_event["cookie"], new_event["cookie"])
        moved.unlink()
        self.expect("DELETE", moved)
        for log in self.events.glob("*.log"):
            text = log.read_text()
            self.assertEqual(len(text.splitlines()), 1)
            self.assertTrue(json.loads(text)["event"])
            self.assertEqual(log.stat().st_mode & 0o777, 0o600)

    def test_log_escaping(self):
        self.launch()
        file = self.first / 'quotes"percent%\nnewline\\.txt'
        file.write_text("data")
        self.expect("CREATE", file)
        for record in self.records():
            self.assertNotIn("\n", json.dumps(record))

    def test_nested_directory_requires_explicit_watch(self):
        nested = self.first / "nested"
        nested.mkdir()
        self.launch()
        (nested / "not-watched.txt").write_text("nested")
        barrier = self.first / "barrier"
        barrier.touch()
        self.expect("CREATE", barrier)
        self.assertFalse(self.event("CREATE", nested / "not-watched.txt"))

    def test_reload_absolute_config_path_and_new_log_directory(self):
        pid = self.launch()
        original_events = self.events
        next_events = self.root / "other-events"
        next_events.mkdir()
        self.write_config([self.second], events=next_events)
        os.kill(pid, signal.SIGHUP)
        self.events = next_events
        counter = 0
        def probe():
            nonlocal counter
            counter += 1
            file = self.second / f"reload-probe-{counter}"
            file.touch()
            return any(e["event"] == "CREATE" for e in self.records())
        wait_for(probe, "SIGHUP did not install new watches/log directory")
        old_count = len(list(original_events.glob("*.log")))
        file = self.first / "after-reload"
        file.touch()
        barrier = self.second / "barrier"
        barrier.touch()
        self.expect("CREATE", barrier)
        self.assertEqual(len(list(original_events.glob("*.log"))), old_count)
        self.assertFalse(self.event("CREATE", file))

    def test_invalid_reload_preserves_previous_config(self):
        pid = self.launch()
        self.write_config(extra="unknown=mistake\n")
        os.kill(pid, signal.SIGHUP)
        if SYSLOG:
            wait_for(lambda: "reload rejected" in self.syslog(), "No reload rejection log")
        else:
            time.sleep(0.2)
        file = self.first / "still-watched"
        file.touch()
        self.expect("CREATE", file)
        self.assertTrue(alive(pid))

    def test_pid_file_change_on_reload_is_rejected(self):
        pid = self.launch()
        self.config.write_text(self.config.read_text().replace("service.pid", "different.pid"))
        os.kill(pid, signal.SIGHUP)
        if SYSLOG:
            wait_for(lambda: "pid_file cannot change" in self.syslog(), "PID change was not rejected")
        else:
            time.sleep(0.2)
        self.assertFalse((self.root / "different.pid").exists())
        self.assertEqual(int(self.pidfile.read_text()), pid)
        file = self.first / "still-watched"
        file.touch()
        self.expect("CREATE", file)

    def test_repeat_launch_replaces_previous_process(self):
        first = self.launch()
        second = self.launch()
        self.assertNotEqual(first, second)
        self.assertFalse(alive(first))
        self.assertTrue(alive(second))
        file = self.first / "new-instance"
        file.touch()
        self.assertEqual(self.expect("CREATE", file)["pid"], second)

    def test_invalid_restart_does_not_stop_working_daemon(self):
        pid = self.launch()
        self.write_config(extra="bad=value\n")
        self.launch(success=False)
        self.assertTrue(alive(pid))
        self.assertEqual(int(self.pidfile.read_text()), pid)

    def test_stale_pid_file(self):
        maximum = int(Path("/proc/sys/kernel/pid_max").read_text())
        self.pidfile.write_text(f"{maximum}\n")
        pid = self.launch()
        self.assertNotEqual(pid, maximum)

    def test_unrelated_pid_is_not_signalled(self):
        sleeper = subprocess.Popen(["sleep", "30"])
        try:
            self.pidfile.write_text(f"{sleeper.pid}\n")
            self.launch(success=False)
            self.assertIsNone(sleeper.poll())
        finally:
            sleeper.terminate()
            sleeper.wait(timeout=5)

    def test_bad_pid_file_is_rejected(self):
        self.pidfile.write_text("-1\n")
        self.launch(success=False)
        self.assertEqual(self.pidfile.read_text(), "-1\n")

    def test_logs_inside_watch_are_rejected(self):
        self.write_config(events=self.first)
        self.launch(success=False)
        self.assertFalse(self.pidfile.exists())

    def test_watch_root_rename_is_reported_and_stops(self):
        pid = self.launch()
        self.first.rename(self.root / "moved-away")
        self.expect("MOVE_SELF", self.first)
        wait_for(lambda: not alive(pid), "Daemon kept logging a stale directory path")
        self.assertEqual(self.pidfile.read_text(), "")


    @unittest.skipUnless(SYSLOG, "Set DISK_MONITOR_SYSLOG_LOG")
    def test_syslog(self):
        pid = self.launch()
        file = self.first / "syslog-test"
        file.touch()
        self.expect("CREATE", file)
        self.write_config([self.first, self.second])
        os.kill(pid, signal.SIGHUP)
        wait_for(lambda: "configuration reloaded" in self.syslog(), "No reload syslog record")
        self.stop(pid)
        wait_for(lambda: "stopping:" in self.syslog(), "No shutdown syslog record")
        log = self.syslog()
        self.assertIn("started config=", log)
        self.assertIn("syslog-test", log)
        self.assertIn('"event":"CREATE"', log)


if __name__ == "__main__":
    if sys.platform != "linux":
        sys.exit("Linux is required.")
    # Забираем завершившиеся дочерние процессы.
    PR_SET_CHILD_SUBREAPER = 36
    if ctypes.CDLL(None).prctl(PR_SET_CHILD_SUBREAPER, 1, 0, 0, 0) != 0:
        sys.exit("Cannot enable child subreaper for daemon tests")
    unittest.main(verbosity=2)
