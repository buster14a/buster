#!/usr/bin/env python3
"""Whole-link `ide cc` timing of the x86-64 ELF TLS companion lookup (#1915).

Ownership: tools/, run from the trusted checkout by compiler_compare as the
link-tls-v1 leg of a pull comparison (#2761); the same command reproduces the
leg by hand on any host:

    python3 -I tools/link_tls_bench.py BASELINE_IDE CANDIDATE_IDE WORK OUTPUT
    python3 -I tools/link_tls_bench.py --profile

The frozen profile is compiler_receipt.LINK_TLS_PROFILE. This tool writes the
profile's generated C inputs under WORK, builds them with the host's clang and
ar, then for every cell links once with each binary and requires the two
images to be byte-identical and to print the cell's frozen output. Only then
does it time the cell: warmups alternating baseline and candidate, then fixed
alternating pairs of fresh `taskset -c CPU ide ...` link processes, recording
wall and CPU nanoseconds of each. OUTPUT receives summary.json with every raw
sample; compiler_receipt.validate_link_tls re-derives every statistic from it.
Linked images stay under WORK. The exit status is 0 only for a complete run.

Map: source_text (generated inputs), build_inputs, timed_link, identity,
measure_cell, main.
"""

from __future__ import annotations

import hashlib
import json
import os
import subprocess
import sys
import threading
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent / "bench_direct"))
from compiler_receipt import (LINK_TLS_INVOCATION_TIMEOUT_SECONDS, LINK_TLS_PROFILE, LINK_TLS_ROLES,  # noqa: E402
                              LINK_TLS_SCHEMA, observed_cpu_model)

# A linked image prints two numbers; anything slower than this is a broken image.
RUN_TIMEOUT_SECONDS = 10
BUILD_TIMEOUT_SECONDS = 600
PLAIN_FUNCTIONS = 4096


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1 << 20), b""):
            digest.update(block)
    return digest.hexdigest()


def source_text(kind: str) -> str:
    """The C source of one profile input kind: main, tls:N or plain."""
    if kind == "main":
        text = ("#include <stdio.h>\nlong sum_all(void);\n"
                "int main(void){ long a=sum_all(), b=sum_all(); printf(\"%ld %ld\\n\",a,b); return 0; }\n")
    elif kind.startswith("tls:"):
        count = int(kind[4:])
        text = "".join(f"__thread long v{i} = {i};\n" for i in range(count))
        text += "long sum_all(void){ long s=0;\n" + "".join(f" s += v{i}; v{i} += 1;\n" for i in range(count))
        text += " return s; }\n"
    else:
        text = "".join(f"long g{i}={i}; long f{i}(long x){{return x+g{i};}}\n" for i in range(PLAIN_FUNCTIONS))
        text += "long sum_all(void){long s=0;\n" + "".join(f"s=f{i}(s);\n" for i in range(PLAIN_FUNCTIONS))
        text += "return s;}\n"
    return text


def first_line(argv: list[str]) -> str:
    try:
        result = subprocess.run(argv, capture_output=True, text=True, timeout=60, check=False)
        lines = (result.stdout + result.stderr).strip().splitlines()
        line = lines[0].strip() if result.returncode == 0 and lines else ""
    except (OSError, subprocess.TimeoutExpired):
        line = ""
    return line


def build_inputs(directory: Path, log: Path) -> tuple[dict[str, str], list[str]]:
    """Write and build every profile input in directory; (name -> sha256, errors)."""
    errors: list[str] = []
    hashes: dict[str, str] = {}
    directory.mkdir(parents=True, exist_ok=True)
    with log.open("a", encoding="utf-8") as stream:
        for name, recipe in LINK_TLS_PROFILE["inputs"].items():
            if errors:
                break
            if "archive" in recipe:
                argv = ["ar", "rcs", name, *recipe["archive"]]
            else:
                source = Path(name).stem + ".c"
                (directory / source).write_text(source_text(recipe["source"]), encoding="utf-8")
                argv = [LINK_TLS_PROFILE["compiler"], *recipe["arguments"], source, "-o", name]
            stream.write("$ " + " ".join(argv) + "\n")
            stream.flush()
            try:
                status = subprocess.run(argv, cwd=directory, stdout=stream, stderr=subprocess.STDOUT,
                                        timeout=BUILD_TIMEOUT_SECONDS, check=False).returncode
            except (OSError, subprocess.TimeoutExpired) as error:
                status = f"{error.__class__.__name__}"
            if status != 0 or not (directory / name).is_file():
                errors.append(f"building input {name} failed ({status})")
            else:
                hashes[name] = sha256(directory / name)
    return hashes, errors


def timed_link(binary: Path, arguments: list[str], inputs: Path, output: Path, cpu: int | None) -> dict:
    """One fresh link process: its exit, wall nanoseconds and the process's own user plus system CPU nanoseconds.

    A blocking wait4 ends the interval as soon as the child exits (Popen.wait with a timeout polls with
    sleeps, which quantizes short links); a timer kills a link that exceeds the invocation timeout.
    """
    argv = (["taskset", "-c", str(cpu)] if cpu is not None else []) + [str(binary), *arguments, "-o", str(output)]
    killed = threading.Event()
    started = time.perf_counter_ns()
    try:
        process = subprocess.Popen(argv, cwd=inputs, stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
                                   stderr=subprocess.DEVNULL)
    except OSError:
        process = None
    if process is None:
        result = {"exit": 127, "wall_ns": time.perf_counter_ns() - started, "cpu_ns": 0}
    else:
        def kill() -> None:
            killed.set()
            process.kill()

        timer = threading.Timer(LINK_TLS_INVOCATION_TIMEOUT_SECONDS, kill)
        timer.start()
        unused_pid, status, usage = os.wait4(process.pid, 0)
        wall = time.perf_counter_ns() - started
        timer.cancel()
        process.returncode = os.waitstatus_to_exitcode(status)
        result = {"exit": 124 if killed.is_set() else process.returncode, "wall_ns": wall,
                  "cpu_ns": round((usage.ru_utime + usage.ru_stime) * 1e9)}
    return result


def identity(binary: Path, arguments: list[str], inputs: Path, image: Path) -> dict:
    """Link once, untimed, then hash and run the image."""
    record = {"exit": timed_link(binary, arguments, inputs, image, None)["exit"], "sha256": "", "size_bytes": 0,
              "run_exit": None, "stdout": ""}
    if record["exit"] == 0 and image.is_file():
        record["sha256"] = sha256(image)
        record["size_bytes"] = image.stat().st_size
        try:
            result = subprocess.run([str(image)], capture_output=True, text=True, timeout=RUN_TIMEOUT_SECONDS,
                                    check=False, stdin=subprocess.DEVNULL)
            record["run_exit"], record["stdout"] = result.returncode, result.stdout
        except (OSError, subprocess.TimeoutExpired) as error:
            record["run_exit"] = error.__class__.__name__
    return record


def measure_cell(cell: dict, binaries: dict[str, Path], inputs: Path, work: Path) -> tuple[dict, list[str]]:
    """Identity first; only a cell whose images agree and print the frozen output is timed."""
    profile = LINK_TLS_PROFILE
    record = {"name": cell["name"], "arguments": cell["arguments"], "identity": {},
              "warmups": {role: [] for role in LINK_TLS_ROLES}, "pairs": []}
    for role in LINK_TLS_ROLES:
        record["identity"][role] = identity(binaries[role], cell["arguments"], inputs,
                                            work / "images" / f"{cell['name']}-{role}")
    images = [record["identity"][role] for role in LINK_TLS_ROLES]
    errors = []
    if any(image["exit"] != 0 or image["run_exit"] != 0 or image["stdout"] != cell["stdout"] for image in images):
        errors.append(f"{cell['name']}: an image did not link, run and print the frozen output")
    elif images[0]["sha256"] != images[1]["sha256"]:
        errors.append(f"{cell['name']}: baseline and candidate images differ")
    if not errors:
        targets = {role: work / "timed" / role for role in LINK_TLS_ROLES}
        for unused_warmup in range(profile["warmups"]):
            for role in LINK_TLS_ROLES:
                record["warmups"][role].append(timed_link(binaries[role], cell["arguments"], inputs, targets[role],
                                                          profile["cpu"]))
        for index in range(profile["pairs"]):
            order = ["baseline", "candidate"] if index % 2 == 0 else ["candidate", "baseline"]
            pair = {"order": order}
            for role in order:
                pair[role] = timed_link(binaries[role], cell["arguments"], inputs, targets[role], profile["cpu"])
            record["pairs"].append(pair)
        if any(item["exit"] != 0 for role in LINK_TLS_ROLES for item in record["warmups"][role]) or \
                any(pair[role]["exit"] != 0 for pair in record["pairs"] for role in LINK_TLS_ROLES):
            errors.append(f"{cell['name']}: a timed link failed")
    return record, errors


def main(argv: list[str]) -> int:
    if argv == ["--profile"]:
        print(json.dumps(LINK_TLS_PROFILE, sort_keys=True, indent=2))
        return 0
    if len(argv) != 4:
        print(__doc__.split("\n\n")[1], file=sys.stderr)
        return 2
    binaries = {"baseline": Path(argv[0]).resolve(), "candidate": Path(argv[1]).resolve()}
    work, output = Path(argv[2]).resolve(), Path(argv[3]).resolve()
    for directory in (work / "images", work / "timed", output):
        directory.mkdir(parents=True, exist_ok=True)
    hashes = {role: sha256(path) if path.is_file() else None for role, path in binaries.items()}
    summary = {"schema": LINK_TLS_SCHEMA, "profile": LINK_TLS_PROFILE["name"], "cpu": LINK_TLS_PROFILE["cpu"],
               "cpu_model": observed_cpu_model(), "binaries": hashes,
               "toolchain": {"clang": first_line([LINK_TLS_PROFILE["compiler"], "--version"]),
                             "ar": first_line(["ar", "--version"])},
               "inputs": {}, "cells": [], "errors": []}
    inputs = work / "inputs"
    summary["inputs"], summary["errors"] = build_inputs(inputs, output / "inputs.log")
    if not summary["errors"]:
        for cell in LINK_TLS_PROFILE["cells"]:
            record, errors = measure_cell(cell, binaries, inputs, work)
            summary["cells"].append(record)
            summary["errors"].extend(errors)
    if {role: sha256(path) if path.is_file() else None for role, path in binaries.items()} != hashes:
        summary["errors"].append("a measured binary changed during the run")
    summary["status"] = "complete" if not summary["errors"] else "failed"
    (output / "summary.json").write_text(json.dumps(summary, sort_keys=True, indent=2) + "\n", encoding="utf-8")
    for line in summary["errors"]:
        print(f"link_tls_bench: {line}", file=sys.stderr)
    return 0 if summary["status"] == "complete" else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
