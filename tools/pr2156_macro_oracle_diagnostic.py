#!/usr/bin/env python3
"""Bounded, isolated correctness diagnostic for PR 2156. No performance verdict."""
import argparse
import json
import os
from pathlib import Path
import re
import resource
import shutil
import signal
import subprocess
import tempfile
import time

HEAD = "f149ef3ac3d3c2f9397a0e0e5c280c6bfd544d13"
PREFIX = '#define M 7\n#pragma push_macro("M")\n#undef M\n#define M(x,...) x , ##__VA_ARGS__\n'
CASES = [
    ("omitted", PREFIX + 'M(_Pragma("pop_macro(\\\"M\\\")") 11) M\n', ["11", "7"]),
    ("empty", PREFIX + 'M(_Pragma("pop_macro(\\\"M\\\")") 11,) M\n', ["11", ",", "7"]),
]

def limits(address_limit):
    resource.setrlimit(resource.RLIMIT_FSIZE, (1024 * 1024, 1024 * 1024))
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    if address_limit:
        resource.setrlimit(resource.RLIMIT_AS, (address_limit, address_limit))

def rss(pid):
    try:
        for line in Path(f"/proc/{pid}/status").read_text().splitlines():
            if line.startswith("VmRSS:"):
                return int(line.split()[1]) * 1024
    except (FileNotFoundError, ProcessLookupError):
        pass
    return 0

def execute(command, label, directory, deadline, rss_limit, address_limit=0, environment=None):
    stdout_path = directory / (label + ".stdout")
    stderr_path = directory / (label + ".stderr")
    print("DIAGNOSTIC_START", json.dumps({"label": label, "command": command,
          "deadline_seconds": deadline, "rss_limit_bytes": rss_limit,
          "address_limit_bytes": address_limit}), flush=True)
    started = time.monotonic()
    peak = 0
    cause = None
    with stdout_path.open("wb") as out, stderr_path.open("wb") as err:
        process = subprocess.Popen(command, stdout=out, stderr=err,
            start_new_session=True, preexec_fn=lambda: limits(address_limit),
            env=environment)
        while process.poll() is None:
            peak = max(peak, rss(process.pid))
            elapsed = time.monotonic() - started
            if elapsed > deadline or peak > rss_limit:
                cause = "deadline" if elapsed > deadline else "rss-limit"
                os.killpg(process.pid, signal.SIGKILL)
                break
            time.sleep(0.02)
        return_code = process.wait(timeout=5)
    receipt = {"label": label, "return_code": return_code,
        "termination": cause, "elapsed_seconds": time.monotonic() - started,
        "peak_observed_rss_bytes": peak, "stdout": stdout_path.name,
        "stderr": stderr_path.name, "diagnostic_head": HEAD}
    (directory / (label + ".json")).write_text(json.dumps(receipt, indent=2) + "\n")
    print("DIAGNOSTIC_RESULT", json.dumps(receipt), flush=True)
    print("DIAGNOSTIC_STDOUT", label, stdout_path.read_text(errors="replace")[:8192], flush=True)
    print("DIAGNOSTIC_STDERR", label, stderr_path.read_text(errors="replace")[:8192], flush=True)
    return receipt, stdout_path.read_text(errors="replace"), stderr_path.read_text(errors="replace")

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--references", action="store_true")
    parser.add_argument("--buster")
    parser.add_argument("--module", action="store_true")
    parser.add_argument("--log-dir", required=True)
    options = parser.parse_args()
    directory = Path(options.log_dir).resolve()
    directory.mkdir(parents=True, exist_ok=True)
    tools = [("clang", shutil.which("clang")), ("gcc", shutil.which("gcc"))] if options.references else [("buster", str(Path(options.buster).resolve()))]
    failures = 0
    with tempfile.TemporaryDirectory(prefix="pr2156-oracle-") as temporary:
        fixtures = Path(temporary)
        for name, source, expected in CASES:
            (fixtures / (name + ".c")).write_text(source)
        for tool_name, executable in tools:
            if not executable:
                print("DIAGNOSTIC_MISSING_TOOL", tool_name, flush=True)
                failures += 1
                continue
            if options.references:
                version = subprocess.run([executable, "--version"], capture_output=True, text=True, timeout=5)
                print("DIAGNOSTIC_TOOL", tool_name, version.stdout.splitlines()[0], flush=True)
            for dialect in ["gnu17", "c17"]:
                for name, source, expected in CASES:
                    command = [executable] + (["-E", "-P"] if options.references else ["cc", "-E"])
                    command += ["-std=" + dialect, str(fixtures / (name + ".c"))]
                    receipt, stdout, stderr = execute(command, tool_name + "-" + dialect + "-" + name,
                        directory, 5, 512 * 1024 * 1024,
                        768 * 1024 * 1024 if options.references else 0)
                    tokens = re.findall(r"[A-Za-z_][A-Za-z_0-9]*|[0-9]+|[^\s]", stdout)
                    passed = receipt["return_code"] == 0 and not receipt["termination"] and not stderr and tokens == expected
                    print("DIAGNOSTIC_TOKENS", json.dumps({"tool": tool_name, "dialect": dialect,
                        "case": name, "expected": expected, "actual": tokens[:100], "passed": passed}), flush=True)
                    failures += not passed
        if options.module and failures == 0:
            environment = dict(os.environ, BUSTER_TEST_JOBS="1")
            receipt, stdout, stderr = execute([tools[0][1], "test", "--module=c_frontend_tests", "--ci=1", "--verbose=1"],
                "buster-frontend-module", directory, 90, 1536 * 1024 * 1024, environment=environment)
            passed = receipt["return_code"] == 0 and not receipt["termination"] and not stderr
            failures += not passed
            progress = [line for line in stdout.splitlines() if line.startswith("TEST_FIXTURE_START_V1") or line.startswith("TEST_MODULE_TIMING")]
            print("DIAGNOSTIC_MODULE", json.dumps({"passed": passed, "last_progress": progress[-8:]}), flush=True)
    print("DIAGNOSTIC_SUMMARY", json.dumps({"failures": failures, "source_head": HEAD}), flush=True)
    return int(failures != 0)

if __name__ == "__main__":
    raise SystemExit(main())
