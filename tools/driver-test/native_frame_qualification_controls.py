"""Hosted controls for the temporary controller; no compiler/matrix execution."""
import itertools
import json
import os
from pathlib import Path
import runpy
import subprocess
import sys
import tempfile
import time

namespace = runpy.run_path("tools/driver-test/native_frame_qualification.py")
g = namespace["command"].__globals__
original = {name: g[name] for name in ("ROOT", "OUTPUT", "sample", "identities", "command", "retain_build_evidence")}
real_popen = subprocess.Popen
real_run = subprocess.run
real_check = subprocess.check_output
real_open = Path.open
failures = []


def check(value, label):
    if not value:
        raise AssertionError(label)


def restore():
    subprocess.Popen = real_popen
    subprocess.run = real_run
    subprocess.check_output = real_check
    Path.open = real_open
    for name, value in original.items():
        g[name] = value
    g["PRODUCER_DRAIN_UNRESOLVED"] = False
    g.pop("print", None)


def execute(label, test):
    try:
        test()
        print("CONTROL PASS", label, flush=True)
    except Exception as error:
        failures.append((label, repr(error)))
        print("CONTROL FAIL", label, ascii(error), flush=True)
    finally:
        restore()


with tempfile.TemporaryDirectory(prefix="frame-controller-", dir=os.environ["RUNNER_TEMP"]) as temporary:
    root = Path(temporary)
    environment = dict(os.environ)
    payload = b'native negative control --\xff\xfe\nUTF8 \xf0\x9f\x98\x80\n'
    producer = [sys.executable, "-c", "import sys;sys.stdout.buffer.write(" + repr(payload) + ");sys.stdout.buffer.flush()"]

    def raw_capture():
        log = root / "raw.log"
        result = g["command"](producer, root, environment, log)
        check(result["exit_status"] == 0 and result["owned_child_drained"], "raw producer must finish")
        check(log.read_bytes() == payload, "raw malformed UTF8 bytes must be exact")
    execute("malformed UTF8 and cp1252 console", raw_capture)

    def log_open_failure():
        calls = []
        def opened(path, mode="r", *args, **kwargs):
            if path.name == "open-fail.log" and mode == "wb":
                raise OSError("injected capture open failure")
            return real_open(path, mode, *args, **kwargs)
        def spawned(*args, **kwargs):
            calls.append(args)
            return real_popen(*args, **kwargs)
        Path.open = opened
        subprocess.Popen = spawned
        result = g["command"](producer, root, environment, root / "open-fail.log")
        check(result["exit_status"] != 0 and result["capture_errors"], "capture open failure must fail")
        check(not calls and result["owned_child_drained"], "producer must not start without capture")
    execute("capture open failure", log_open_failure)

    def capture_flush_failure():
        finished = root / "flush-child-done"
        code = "import pathlib,sys,time;sys.stdout.buffer.write(b'raw\\xff\\n');sys.stdout.buffer.flush();time.sleep(.2);pathlib.Path(" + repr(str(finished)) + ").write_text('done')"
        class FlushFailure:
            def __init__(self, wrapped):
                self.wrapped = wrapped
            def __enter__(self):
                return self
            def __exit__(self, *args):
                self.wrapped.close()
            def fileno(self):
                return self.wrapped.fileno()
            def flush(self):
                raise OSError("injected capture flush failure")
        def opened(path, mode="r", *args, **kwargs):
            file = real_open(path, mode, *args, **kwargs)
            return FlushFailure(file) if path.name == "flush-fail.log" and mode == "wb" else file
        Path.open = opened
        result = g["command"]([sys.executable, "-c", code], root, environment, root / "flush-fail.log")
        check(result["exit_status"] != 0 and result["capture_errors"], "capture flush failure must fail")
        check(result["process_exit_status"] == 0 and result["owned_child_drained"] and finished.is_file(), "capture failure must follow completed producer")
        check((root / "flush-fail.log").read_bytes() == b"raw\xff\n", "raw bytes remain retained")
    execute("capture write failure after producer drain", capture_flush_failure)

    def wait_interruption():
        finished = root / "wait-child-done"
        code = "import pathlib,time;time.sleep(.2);pathlib.Path(" + repr(str(finished)) + ").write_text('done');print('owned child done')"
        processes = []
        class WaitOnce:
            def __init__(self, process):
                self.process = process
                self.first = True
            def wait(self):
                if self.first:
                    self.first = False
                    raise RuntimeError("injected observer wait exception")
                return self.process.wait()
        def spawned(*args, **kwargs):
            process = real_popen(*args, **kwargs)
            processes.append(process)
            return WaitOnce(process)
        subprocess.Popen = spawned
        result = g["command"]([sys.executable, "-c", code], root, environment, root / "wait-fail.log")
        check(result["exit_status"] != 0 and result["capture_errors"], "observer exception remains failure")
        check(result["owned_child_drained"] and finished.is_file() and processes[0].poll() == 0, "finally must await owned producer")
    execute("observer exception drains child", wait_interruption)

    def unresolved_wait():
        processes = []
        class WaitFailure:
            def wait(self):
                raise RuntimeError("injected persistent wait failure")
        def spawned(*args, **kwargs):
            process = real_popen(*args, **kwargs)
            processes.append(process)
            return WaitFailure()
        subprocess.Popen = spawned
        try:
            result = g["command"]([sys.executable, "-c", "import time;time.sleep(5)"], root, environment, root / "unresolved.log")
            check(result["exit_status"] != 0 and not result["owned_child_drained"] and g["PRODUCER_DRAIN_UNRESOLVED"], "unresolved lifecycle must remain explicit")
        finally:
            for process in processes:
                process.kill()
                process.wait()
    execute("unresolved producer is not declared drained", unresolved_wait)

    def deferred_cleanup():
        g["ROOT"] = root / "still-owned-source"
        g["ROOT"].mkdir()
        (g["ROOT"] / "keep").write_text("owned")
        g["OUTPUT"] = root / "cleanup-output"
        g["OUTPUT"].mkdir()
        calls = []
        def mocked_run(argv, **kwargs):
            calls.append(argv)
            return subprocess.CompletedProcess(argv, 0)
        subprocess.run = mocked_run
        subprocess.check_output = lambda *args, **kwargs: "0" * 40 + "\n"
        g["command"] = lambda *args, **kwargs: {"exit_status": 1, "owned_child_drained": False, "process_exit_status": None}
        g["PRODUCER_DRAIN_UNRESOLVED"] = True
        row = g["sample"]("A1", environment)
        check(not row["success"] and "cleanup_deferred" in row, "unresolved source must fail and defer cleanup")
        check((g["ROOT"] / "keep").is_file() and not any("remove" in argv for argv in calls), "live worktree must not be removed")
    execute("cleanup refuses unresolved producer", deferred_cleanup)

    def broken_console():
        def broken(*args, **kwargs):
            raise BrokenPipeError("injected console presentation failure")
        g["print"] = broken
        log = root / "console-fail.log"
        result = g["command"](producer, root, environment, log)
        check(result["exit_status"] == 0 and result["owned_child_drained"] and log.read_bytes() == payload, "optional presentation must not interrupt primary capture/lifecycle")
    execute("console presentation cannot orphan producer", broken_console)

    def attempted_sample():
        g["OUTPUT"] = root / "accounting"
        g["identities"] = lambda environment: None
        def failed_sample(name, environment):
            raise RuntimeError("injected A1 capture error \ufffd")
        g["sample"] = failed_sample
        g["print"] = lambda *args, **kwargs: (_ for _ in ()).throw(BrokenPipeError("secondary console error"))
        status = g["main"]()
        terminal = json.loads((g["OUTPUT"] / "terminal.json").read_text())
        attempted = json.loads((g["OUTPUT"] / "attempted-samples.json").read_text())
        check(status != 0 and [row["sample"] for row in terminal["samples"]] == ["A1"], "failed A1 must remain attempted")
        check(attempted["attempted"] == ["A1"] and terminal["unexecuted_samples"] == list(g["SAMPLES"][1:]), "only later arms are unexecuted")
    execute("attempted A1 survives error reporting failure", attempted_sample)

print(json.dumps({"controls": 8, "failures": failures}, ensure_ascii=True), flush=True)
sys.exit(bool(failures))
