#!/usr/bin/env python3
"""Bounded, isolated correctness diagnostic for PR 2156. No performance verdict."""
import argparse
import json
import hashlib
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
    [
        "case00",
        "#define F(x) x\nF\n(11)\n",
        [
            "11"
        ]
    ],
    [
        "case01",
        "#define F(x) x\nF\n(__LINE__)\n",
        [
            "3"
        ]
    ],
    [
        "case02",
        "#define F(x) x\n#define A F\nA\n(__LINE__)\n",
        [
            "4"
        ]
    ],
    [
        "case03",
        "#define F(x) x\n#define ID(x) x\nID(F\n(__LINE__))\n",
        [
            "4"
        ]
    ],
    [
        "case04",
        "#define F(x) x\n#line 70 \"rescan-lines.c\"\nF\n(__LINE__)\n",
        [
            "71"
        ]
    ],
    [
        "case05",
        "#define F(x) x\n#define A F\n#define B A\nB\n\n(12)\n",
        [
            "12"
        ]
    ],
    [
        "case06",
        "#define F(x) x\nF /* comment\ncontinued */\n/* between */ (13)\n",
        [
            "13"
        ]
    ],
    [
        "case07",
        "#define F(x) x\r\nF\r\n\r\n(14)\r\n",
        [
            "14"
        ]
    ],
    [
        "case08",
        "#define F(x) x\n#define ID(x) x\n#define A F\nID(A)\n(ID(F\n(15)))\n",
        [
            "15"
        ]
    ],
    [
        "case09",
        "#define F(x) x\n#define TAIL(x) x F\nTAIL(16)\n(17)\n",
        [
            "16",
            "17"
        ]
    ],
    [
        "case10",
        "#define F(x) x\n#define A F\nF\nname A\n+ 18\n",
        [
            "F",
            "name",
            "F",
            "+",
            "18"
        ]
    ],
    [
        "case11",
        "#define F(x) x\n#define A F\nA\n#undef A\n#define A 19\nA\n",
        [
            "F",
            "19"
        ]
    ],
    [
        "case12",
        "#define X 1\n#pragma push_macro(\"X\")\n#undef X\n#define X 2\n_Pragma(\"pop_macro(\\\"X\\\")\") X\n",
        [
            "1"
        ]
    ],
    [
        "case13",
        "#define X 1\n#pragma push_macro(\"X\")\n#undef X\n#define X 2\n_Pragma(\"pop_macro(\\\"X\\\")\")\nX\n",
        [
            "1"
        ]
    ],
    [
        "case14",
        "#define X 1\n#pragma push_macro(\"X\")\n#undef X\n#define X 2\n#define RESTORE _Pragma(\"pop_macro(\\\"X\\\")\")\n#define ID(x) x\nID(RESTORE X) X\n",
        [
            "1",
            "1"
        ]
    ],
    [
        "case15",
        "#define X 1\n#pragma push_macro(\"X\")\n#undef X\n#define X 2\n#pragma push_macro(\"X\")\n#undef X\n#define X 3\n#define DUP(x) x x\nDUP(_Pragma(\"pop_macro(\\\"X\\\")\") X) X\n",
        [
            "2",
            "2",
            "2"
        ]
    ],
    [
        "case16",
        "#define X 1\n#pragma push_macro(\"X\")\n#undef X\n#define X 2\n_Pragma(\"push_macro(\\\"X\\\")\") _Pragma(\"pop_macro(\\\"X\\\")\") X _Pragma(\"pop_macro(\\\"X\\\")\") X\n",
        [
            "2",
            "1"
        ]
    ],
    [
        "case17",
        "#pragma push_macro(\"MISSING\")\n#define MISSING 9\n_Pragma(\"pop_macro(\\\"MISSING\\\")\") MISSING\n",
        [
            "MISSING"
        ]
    ],
    [
        "case18",
        "#define F(x) x\n#pragma push_macro(\"F\")\n#undef F\n#define F(x,y) x + y\nF(_Pragma(\"pop_macro(\\\"F\\\")\") 1,2) F(3)\n",
        [
            "1",
            "+",
            "2",
            "3"
        ]
    ],
    [
        "case19",
        "#define F(x) x\n#pragma push_macro(\"F\")\n#undef F\n#define F(x,y) x #y\nF(_Pragma(\"pop_macro(\\\"F\\\")\") 4,5) F(6)\n",
        [
            "4",
            "\"",
            "5",
            "\"",
            "6"
        ]
    ],
    [
        "case20",
        "#pragma push_macro(\"X\")\n#define X _Pragma(\"pop_macro(\\\"X\\\")\")\nX\n#define X 1\nX\n",
        [
            "1"
        ]
    ],
    [
        "case21",
        "#define M 7\n#pragma push_macro(\"M\")\n#undef M\n#define M(x,...) x , ##__VA_ARGS__\nM(_Pragma(\"pop_macro(\\\"M\\\")\") 11) M\n",
        [
            "11",
            "7"
        ]
    ],
    [
        "case22",
        "#define M 7\n#pragma push_macro(\"M\")\n#undef M\n#define M(x,...) x , ##__VA_ARGS__\nM(_Pragma(\"pop_macro(\\\"M\\\")\") 11,) M\n",
        [
            "11",
            ",",
            "7"
        ]
    ],
    [
        "case23",
        "#define SAME _Pragma(\"push_macro(\\\"SAME\\\")\") _Pragma(\"pop_macro(\\\"SAME\\\")\") SAME\nSAME\n",
        [
            "SAME"
        ]
    ],
    [
        "case24",
        "#define SELF 7\n#pragma push_macro(\"SELF\")\n#undef SELF\n#define SELF _Pragma(\"pop_macro(\\\"SELF\\\")\") SELF\nSELF SELF\n",
        [
            "7",
            "7"
        ]
    ]
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
                    print("DIAGNOSTIC_CASE", json.dumps({"name": name, "source": source, "sha256": hashlib.sha256(source.encode()).hexdigest()}), flush=True)
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
