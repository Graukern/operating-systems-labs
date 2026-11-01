#!/usr/bin/env python3
"""End-to-end demonstration through libc syslog, journald and rsyslog.

Install the supplied rsyslog rule first. This script requires permission to
read /var/log/diskmon.log, and never uses the test logging interposer.
"""
import ctypes
import json
import os
from pathlib import Path
import signal
import subprocess
import tempfile
import time


LAB = Path(__file__).resolve().parents[1]
LOG = Path("/var/log/diskmon.log")
ctypes.CDLL(None).prctl(36, 1, 0, 0, 0)
environment = os.environ.copy()
environment.pop("LD_PRELOAD", None)
environment.pop("DISKMON_TEST_LOG", None)


def wait_for(predicate):
    deadline = time.monotonic() + 8
    while time.monotonic() < deadline:
        if predicate():
            return
        time.sleep(0.025)
    raise AssertionError("Timed out waiting for real syslog output")


with tempfile.TemporaryDirectory(prefix="diskmon-demo-") as temporary:
    root = Path(temporary)
    first, second = root / "first", root / "second"
    first.mkdir()
    second.mkdir()
    config, pid_file = root / "monitor.conf", root / "diskmon.pid"
    config.write_text("first\n")
    processes = []

    def log_text():
        return LOG.read_text() if LOG.exists() else ""

    def expect(marker):
        wait_for(lambda: marker in log_text())

    def start():
        run = subprocess.run([str(LAB / "diskmon"), "monitor.conf", "diskmon.pid"],
                             cwd=root, env=environment, text=True, capture_output=True, timeout=10)
        if run.returncode != 0:
            raise AssertionError(run.stderr)
        pid = int(pid_file.read_text())
        processes.append(pid)
        print(f"$ ./diskmon monitor.conf diskmon.pid\n{run.stdout.strip()}", flush=True)
        return pid

    try:
        pid = start()
        path = first / "proof.txt"
        path.write_text("hello from diskmon\n")
        assert path.read_text() == "hello from diskmon\n"
        path.chmod(0o600)
        renamed = first / "renamed.txt"
        path.rename(renamed)
        renamed.unlink()
        for event in ("CREATE", "MODIFY", "ACCESS", "ATTRIB", "MOVED_FROM", "MOVED_TO", "DELETE"):
            name = renamed if event in ("MOVED_TO", "DELETE") else path
            expect(f'event={event} path="{name}"')
        print("PASS: create, write, read, chmod, rename and delete reached /var/log/diskmon.log", flush=True)
        config.write_text("second\n")
        os.kill(pid, signal.SIGHUP)
        expect(f'diskmon[{pid}]: RELOAD_OK config="{config}"')
        (second / "after-reload.txt").write_text("reload works\n")
        expect(f'event=CREATE path="{second}/after-reload.txt"')
        print("PASS: SIGHUP switched to the second directory", flush=True)
        config.write_text("missing-directory\n")
        os.kill(pid, signal.SIGHUP)
        expect(f"diskmon[{pid}]: RELOAD_FAILED")
        (second / "after-invalid-reload.txt").write_text("old settings survived\n")
        expect(f'event=CREATE path="{second}/after-invalid-reload.txt"')
        print("PASS: invalid reload kept the previous watches", flush=True)
        config.write_text("second\n")
        new_pid = start()
        expect(f"diskmon[{pid}]: STOP signal=15")
        os.waitpid(pid, 0)
        print(f"PASS: repeated launch stopped PID {pid} and started PID {new_pid}", flush=True)
        os.kill(new_pid, signal.SIGTERM)
        os.waitpid(new_pid, 0)
        expect(f"diskmon[{new_pid}]: STOP signal=15")
        assert pid_file.read_text() == ""
        print("PASS: SIGTERM logged shutdown and cleared the PID file", flush=True)
        print("\nActual lines from /var/log/diskmon.log:", flush=True)
        selected = [line for line in log_text().splitlines()
                    if any(f"diskmon[{process}]:" in line for process in processes)]
        print("\n".join(selected), flush=True)
        result = subprocess.run(["journalctl", "-t", "diskmon", "--no-pager", "-o", "json",
                                 "-n", "2000"], text=True, capture_output=True, check=True)
        records = [json.loads(line) for line in result.stdout.splitlines() if line.strip()]
        records = [record for record in records
                   if record.get("SYSLOG_PID") in {str(process) for process in processes}]
        assert records, "No journal records for this demonstration"
        assert all(record.get("SYSLOG_FACILITY") == "16" for record in records)
        print(f"\nPASS: journal confirmed SYSLOG_FACILITY=16 (LOG_LOCAL0) in {len(records)} records", flush=True)
        print("END-TO-END DEMO PASSED", flush=True)
    finally:
        for process in processes:
            try:
                os.kill(process, signal.SIGTERM)
                os.waitpid(process, 0)
            except (ProcessLookupError, ChildProcessError):
                pass
