#!/usr/bin/env python3
"""Reproduce #3158 with the real foreign linker at both revisions in this verification build."""
import hashlib
import json
import os
import re
import socket
import subprocess
import sys
import tempfile
from pathlib import Path

repo = Path(__file__).resolve().parents[2]
compiler = Path(sys.argv[1]).resolve(strict=True)
env = {k: v for k, v in os.environ.items() if not k.startswith("BUSTER_3158_")}
env["LC_ALL"] = "C"


def command(argv, *, cwd=repo, extra=None, timeout=150):
    return subprocess.run(argv, cwd=cwd, env={**env, **(extra or {})}, capture_output=True, timeout=timeout)


def need(value, detail):
    if not value:
        raise RuntimeError(str(detail))


def say(message):
    print("B3158", message, flush=True)


def read(path):
    return path.read_bytes() if path.is_file() else None


def digest(data):
    return hashlib.sha256(data).hexdigest() if data is not None else "-"


def check_host():
    data = command(["lscpu"]).stdout.decode()
    name = re.search(r"^Model name:\s+(.+)", data, re.M)
    need(name and "9700X" not in name[1].upper() and "benchpress" not in socket.gethostname().lower(), "unqualified host")
    if env.get("GITHUB_ACTIONS"):
        event = json.loads(Path(env["GITHUB_EVENT_PATH"]).read_text())
        need(event.get("pull_request", {}).get("draft") is True, "requires a draft")
        need("EPYC" in name[1] or "XEON" in name[1].upper(), name[1])
    c = command(["clang", "-march=native", "-include", "src/buster/compiler/host_isa.h", "-fsyntax-only", "-x", "c", "/dev/null"])
    need(c.returncode == 0, c.stderr)
    n = len(os.sched_getaffinity(0))
    need(n >= 2, f"only {n} allowed CPUs")
    lanes = sorted({1, 2, min(n, 4)})
    say(f"host={name[1]} allowed_cpus={n} lanes={lanes} host_isa.h=pass")
    return lanes


def binutils(args, cwd):
    result = command(args, cwd=cwd)
    need(result.returncode == 0, (args, result.stderr))
    return result.stdout.decode()


def test(directory, lanes):
    binutils(["bash", str(Path(__file__).with_name("objects.sh").resolve()), str(directory)], repo)
    table = binutils(["readelf", "-Ws", "edge-a.o"], directory)
    duplicates = [s.split()[3] for s in table.splitlines() if s.split() and s.split()[-1] == "duplicate"]
    need(duplicates == ["NOTYPE", "OBJECT", "FUNC"], duplicates)
    say("edge-a.o same-name ELF types=" + ",".join(duplicates))

    entry = ["entry.o"]
    empty = ["empty.o"]
    def batch(n, reverse=False):
        a, b = ("function", "object") if reverse else ("object", "function")
        return entry + [f"{a}-{n}.o"] + empty + [f"{b}-{n}.o"]
    abc = entry + ["edge-a.o"] + empty + ["edge-b.o", "edge-c.o"]
    cases = {
        "batch-256": batch(256), "batch-512": batch(512), "batch-reversed": batch(256, True),
        "edges-abc": abc, "edges-cba": entry + ["edge-c.o"] + empty + ["edge-b.o", "edge-a.o"],
        "shared-of": ["shared-object.o"] + empty + ["shared-function.o"],
        "shared-fo": ["shared-function.o"] + empty + ["shared-object.o"],
        "sparse": entry + empty + ["sparse.o"], "no-winner": entry + empty,
        "archive": entry + empty + ["archive-root.o", "later.a"], "undefined": abc + ["bad-undef.o"],
        "bad-ab": abc + ["bad-alpha.o", "bad-beta.o"], "bad-ba": abc + ["bad-beta.o", "bad-alpha.o"],
    }
    first_bad = {"undefined": "missing_alpha", "bad-ab": "bad-alpha.o", "bad-ba": "bad-beta.o"}
    output, flags = directory / "result", directory / "flags.tsv"
    for name, inputs in cases.items():
        reference = None
        counts = {}
        shared = name.startswith("shared")
        opts = ["-shared", "-soname", "libtypes.so"] if shared else ["-static"]
        for nlanes in lanes:
            for mode in ("base", "candidate"):
                for path in [output, flags]:
                    path.unlink(missing_ok=True)
                extra = {"BUSTER_3158_COUNTS": "1", "BUSTER_3158_FLAGS": str(flags)}
                if mode == "base":
                    extra["BUSTER_3158_BASE"] = "1"
                result = command([compiler, "link-foreign", "--lanes", str(nlanes), *opts, "-o", str(output), *inputs], cwd=directory, extra=extra)
                records, diagnostic = [], []
                for line in result.stderr.splitlines(keepends=True):
                    if line.startswith(b"b3158-count "):
                        fields = tuple(map(int, line.split()[1:]))
                        need(len(fields) == 6, line)
                        records.append(fields)
                    else:
                        diagnostic.append(line)
                diagnostic = b"".join(diagnostic)
                snapshot = (result.returncode, result.stdout, diagnostic, read(output), read(flags))
                if reference is None:
                    reference = snapshot
                for field, left, right in zip(("exit", "stdout", "stderr", "ELF", "flags"), reference, snapshot):
                    need(left == right, (name, mode, nlanes, field, repr(left)[:150], repr(right)[:150]))
                status, stdout, stderr, elf, flag_bytes = snapshot
                if name in first_bad:
                    need(status != 0 and elf is None and first_bad[name].encode() in stdout+stderr, (name, status, stdout, stderr))
                else:
                    need(status == 0 and elf and flag_bytes, (name, status, stdout, stderr, bool(elf), bool(flag_bytes)))
                    by_name = {s.split("\t", 2)[2]: int(s.split("\t")[1], 16) for s in flag_bytes.decode().splitlines()}
                    wanted = {}
                    if name.startswith("batch"):
                        size = 512 if name == "batch-512" else 256
                        wanted = {f"ref_{i}": 0x22 if name == "batch-reversed" else 0x21 for i in range(size)}
                    elif name.startswith("edges"):
                        picked = 0x21 if name == "edges-abc" else 0x22
                        wanted = dict(duplicate=picked, other_weak=picked, weak_tls=0x26, plain=0x20, hidden_weak=0xa1, defined_data=0x11, defined_tls=0x16)
                    elif shared:
                        t = "OBJECT" if name == "shared-of" else "FUNC"
                        rows = [s.split() for s in binutils(["readelf", "-W", "--dyn-syms", str(output)], directory).splitlines()]
                        for symbol, kind, binding in (("strong_ref", t, "GLOBAL"), ("weak_api", "FUNC", "WEAK")):
                            r = [x[3:7] for x in rows if len(x) >= 8 and x[-1] == symbol]
                            need(r == [[kind, binding, "DEFAULT", "UND"]], (name, symbol, r))
                        wanted = dict(strong_ref=0x11 if t == "OBJECT" else 0x12, weak_api=0x22)
                    elif name == "sparse":
                        wanted = dict(early=0x21, tail_255=0x11)
                    elif name == "archive":
                        wanted = dict(late_ref=0x22, pull_member=0x11)
                    for symbol, value in wanted.items():
                        need(by_name.get(symbol) == value, (name, symbol, by_name.get(symbol), value))
                    if not shared:
                        run = command([output], cwd=directory)
                        need((run.returncode, run.stdout, run.stderr) == (42, b"", b""), (name, run))
                if nlanes == 1 or name == "undefined":
                    need(not records, (name, mode, nlanes, records))
                else:
                    need(sorted((r[0], r[1]) for r in records) == [(i, nlanes) for i in range(nlanes)], (name, nlanes, records))
                    totals = tuple(map(sum, zip(*(r[2:] for r in records))))
                    counts[f"{mode}/{nlanes}"] = totals
                    if name.startswith("batch"):
                        n = 512 if name == "batch-512" else 256
                        expected = (2*n+1, n, n*(n+1)//2, n) if mode == "base" else (2*n+1, 1, n, n)
                        need(totals == expected, (name, mode, nlanes, totals, expected))
                    if name in ("sparse", "no-winner"):
                        need(totals[1:] == ((1, 1, 1) if name == "sparse" else (0, 0, 0)), (name, mode, totals))
        status, stdout, stderr, elf, flag_bytes = reference
        say(f"{name} lanes={lanes} identical exit={status} ELF={digest(elf)} flags={digest(flag_bytes)}")
        if name in first_bad:
            say(f"{name} diagnostic={(stdout+stderr).decode().strip()}")
        if counts:
            say(f"{name} (mapped extent,object scans,recovery visits,type writes) {json.dumps(counts, sort_keys=True)}")
    say(f"{len(cases)} cases x {len(lanes)} lane counts x 2 implementations passed")


if __name__ == "__main__":
    selected = check_host()
    with tempfile.TemporaryDirectory(prefix="b3158-") as temp:
        test(Path(temp), selected)
    for mode in ("base", "candidate"):
        extra = {"BUSTER_3158_INNER": "1"}
        if mode == "base":
            extra["BUSTER_3158_BASE"] = "1"
        done = command([compiler, "test-foreign", "--cross-check"], extra=extra, timeout=400)
        lines = (done.stdout + done.stderr).decode(errors="replace").splitlines()
        checks = [s for s in lines if re.search(r"\d+ checks, \d+ failures", s)]
        if done.returncode != 0:
            for line in lines[-50:]:
                say(line)
        need(done.returncode == 0 and checks, (mode, done.returncode, lines[-10:]))
        total = sum(int(re.search(r"(\d+) checks,", s)[1]) for s in checks)
        say(f"suite={mode} test-foreign --cross-check exit=0 groups={len(checks)} checks={total} details={checks}")
