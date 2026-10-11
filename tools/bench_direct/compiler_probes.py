#!/usr/bin/env python3
"""Build and time the compiler-internal scaling probes of COMPILER_PROBES_PROFILE (#2761).

Ownership: `tools/bench_direct`, trusted `main` only. compiler_compare runs
`compiler_probes.py run` as the compiler-probes-v1 leg of a pull comparison;
the same command reproduces the leg by hand. tools/fast_ra_scaling.c and
tools/machine_line_mark_scaling.c link compiler internals, so they cannot be
direct workloads. Following their audits, the head's probe source is copied
outside both checkouts and compiled once against the merge base's src/ and
once against the head's src/, each in an isolated shared clone, so the base
checkout and its closure checks stay untouched. Every case then runs the two
binaries with fixed arguments as warmups and alternating pairs pinned to the
profile's CPU; nothing about the counts depends on the request or the timings.
The summary keeps every raw sample and each invocation's self-check fields;
compiler_receipt.validate_compiler_probes re-derives the results from them.

Map: probe_argv (one audit build command), checkout (isolated clone),
parse_output (one invocation's sample lines), invoke (one pinned run),
measure_case (warmups and pairs), build_all, run_leg, main.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import shutil
import subprocess
import sys
from pathlib import Path

from compiler_receipt import (COMPILER_PROBES_PROFILE, COMPILER_PROBES_ROLES, COMPILER_PROBES_SCHEMA,
                              compiler_probes_digest)

GIT_TIMEOUT_SECONDS = 600
# Measured: one probe build (machine.c dominates) took 35-38 s on a hosted 2.8 GHz Xeon.
BUILD_TIMEOUT_SECONDS = 600
# The slowest case (65536-leaf schedule) took 0.39 s per invocation on the same host.
INVOCATION_TIMEOUT_SECONDS = 60
OUTPUT_LIMIT = 1024 * 1024


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1 << 20), b""):
            digest.update(block)
    return digest.hexdigest()


def probe_argv(compiler: str, probe: str, source: Path, output: Path) -> list[str]:
    """One probe's audit build command, run from the root of the revision being built."""
    definition = COMPILER_PROBES_PROFILE["probes"][probe]
    return [compiler, *definition["compile"], str(source), *definition["sources"], *definition["link"], "-o", str(output)]


def logged(argv: list[str], cwd: Path, log: Path, timeout: int) -> int:
    """Run argv with output appended to log; 124 on timeout, 127 when it cannot start."""
    with log.open("ab") as stream:
        stream.write(("$ " + " ".join(argv) + "\n").encode())
        stream.flush()
        try:
            status = subprocess.run(argv, cwd=cwd, stdout=stream, stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL,
                                    timeout=timeout, check=False).returncode
        except subprocess.TimeoutExpired:
            status = 124
        except OSError as error:
            stream.write(f"{error}\n".encode())
            status = 127
        stream.write(f"exit {status}\n".encode())
    return status


def checkout(repository: Path, commit: str, destination: Path, log: Path) -> int:
    """A shared clone of repository at commit; the repository's own checkout is never touched."""
    status = logged(["git", "-c", "core.hooksPath=/dev/null", "clone", "--quiet", "--shared", "--no-checkout",
                     str(repository), str(destination)], repository, log, GIT_TIMEOUT_SECONDS)
    if status == 0:
        status = logged(["git", "-c", "gc.auto=0", "-c", "maintenance.auto=false", "-c", "core.hooksPath=/dev/null",
                         "-C", str(destination), "checkout", "--quiet", "--detach", commit], repository, log,
                        GIT_TIMEOUT_SECONDS)
    return status


def parse_output(text: str, metric: str) -> tuple[list[int], dict | None]:
    """(samples, self-check fields) of one invocation; fields are None when lines disagree or are malformed."""
    samples: list[int] = []
    check: dict | None = None
    valid = True
    for line in text.splitlines():
        fields = dict(token.partition("=")[::2] for token in line.split())
        valid = valid and bool(fields) and all("=" in token for token in line.split()) and fields.get(metric, "").isdigit()
        if valid:
            samples.append(int(fields[metric]))
            line_check = {key: value for key, value in fields.items() if not key.endswith("_ns")}
            valid = check is None or check == line_check
            check = line_check
    return samples, check if valid else None


def invoke(binary: Path, probe: str, arguments: list[str], log: Path) -> dict:
    """One pinned invocation: its exit, timed samples and self-check fields."""
    definition = COMPILER_PROBES_PROFILE["probes"][probe]
    argv = ["taskset", "-c", str(COMPILER_PROBES_PROFILE["cpu"]), str(binary), *arguments]
    try:
        result = subprocess.run(argv, stdout=subprocess.PIPE, stderr=subprocess.PIPE, stdin=subprocess.DEVNULL,
                                timeout=INVOCATION_TIMEOUT_SECONDS, check=False)
        status, stdout, stderr = result.returncode, result.stdout[:OUTPUT_LIMIT], result.stderr[:OUTPUT_LIMIT]
    except subprocess.TimeoutExpired:
        status, stdout, stderr = 124, b"", b"timeout\n"
    except OSError as error:
        status, stdout, stderr = 127, b"", f"{error}\n".encode()
    samples, check = parse_output(stdout.decode("utf-8", errors="replace"), definition["metric"])
    if status != 0 or check is None or stderr:
        with log.open("ab") as stream:
            stream.write(("$ " + " ".join(argv) + f"\nexit {status}\n").encode() + stdout + stderr)
    return {"exit": status, "samples": samples, "check": check}


def measure_case(binaries: dict, probe: str, case: dict, log: Path) -> dict:
    """The fixed warmups and alternating pairs of one case on both binaries."""
    profile = COMPILER_PROBES_PROFILE
    warmups = {role: [] for role in COMPILER_PROBES_ROLES}
    for unused_index in range(profile["warmups"]):
        for role in COMPILER_PROBES_ROLES:
            warmups[role].append(invoke(binaries[role], probe, case["arguments"], log))
    pairs = []
    for index in range(profile["pairs"]):
        order = list(COMPILER_PROBES_ROLES) if index % 2 == 0 else list(reversed(COMPILER_PROBES_ROLES))
        pair: dict = {"order": order}
        for role in order:
            pair[role] = invoke(binaries[role], probe, case["arguments"], log)
        pairs.append(pair)
    return {"probe": probe, "name": case["name"], "arguments": case["arguments"], "warmups": warmups, "pairs": pairs}


def build_all(repository: Path, revisions: dict, work: Path, output: Path, compiler: str, errors: list[str]) -> dict:
    """Clone both revisions and build every probe against each; returns probe -> role -> binary path."""
    profile = COMPILER_PROBES_PROFILE
    log = output / "build.log"
    roots = {}
    for role in COMPILER_PROBES_ROLES:
        root = work / f"probes-{role}"
        if checkout(repository, revisions[role], root, log) == 0:
            roots[role] = root
        else:
            errors.append(f"{role} revision {revisions[role]} could not be checked out (see build.log)")
    sources = work / "probe-sources"
    sources.mkdir(parents=True, exist_ok=True)
    built: dict = {}
    for probe, definition in profile["probes"].items():
        copy = sources / Path(definition["source"]).name
        if "candidate" in roots:
            shutil.copyfile(roots["candidate"] / definition["source"], copy)
        for role, root in roots.items():
            binary = output / "bin" / f"{probe}-{role}"
            status = logged(probe_argv(compiler, probe, copy, binary), root, log, BUILD_TIMEOUT_SECONDS) \
                if copy.is_file() else 127
            if status == 0 and binary.is_file():
                built.setdefault(probe, {})[role] = binary
            else:
                errors.append(f"{probe} {role} build exited {status} (see build.log)")
    return built


def run_leg(repository: Path, base: str, head: str, work: Path, output: Path) -> int:
    """Build, time and summarize the whole profile; 0 only for a complete run."""
    profile = COMPILER_PROBES_PROFILE
    (output / "bin").mkdir(parents=True, exist_ok=True)
    work.mkdir(parents=True, exist_ok=True)
    errors: list[str] = []
    revisions = {"baseline": base, "candidate": head}
    built = build_all(repository, revisions, work, output, profile["compiler"], errors)
    complete = len(built) == len(profile["probes"]) and all(len(roles) == 2 for roles in built.values())
    cases = []
    if complete:
        for probe, definition in profile["probes"].items():
            for case in definition["cases"]:
                cases.append(measure_case(built[probe], probe, case, output / "invocations.log"))
    summary = {
        "schema": COMPILER_PROBES_SCHEMA,
        "profile": profile,
        "base": base,
        "head": head,
        "probe_source_sha256": {probe: sha256_file(work / "probe-sources" / Path(definition["source"]).name)
                                for probe, definition in profile["probes"].items()
                                if (work / "probe-sources" / Path(definition["source"]).name).is_file()},
        "binaries": {probe: {role: {"sha256": sha256_file(path), "size_bytes": path.stat().st_size,
                                    "revision": revisions[role]} for role, path in roles.items()}
                     for probe, roles in built.items()},
        "cases": cases,
    }
    summary["results"] = compiler_probes_digest(summary)
    if complete and len(summary["results"]) != len(cases):
        errors.append("a case has failed, partial or inconsistent invocations (see invocations.log)")
    summary["status"] = "complete" if complete and not errors else "failed"
    summary["errors"] = errors
    (output / "summary.json").write_text(json.dumps(summary, indent=1, sort_keys=True) + "\n", encoding="utf-8")
    return 0 if summary["status"] == "complete" else 1


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    commands = parser.add_subparsers(dest="command", required=True)
    leg = commands.add_parser("run", help="build both revisions' probes and time them")
    leg.add_argument("--repository", type=Path, required=True, help="a checkout holding both revisions' objects")
    leg.add_argument("--base", required=True)
    leg.add_argument("--head", required=True)
    leg.add_argument("--work", type=Path, required=True, help="scratch for the clones; must not exist yet")
    leg.add_argument("--output", type=Path, required=True)
    arguments = parser.parse_args(argv)
    if arguments.work.exists():
        parser.error(f"{arguments.work} already exists")
    return run_leg(arguments.repository.resolve(), arguments.base, arguments.head, arguments.work.resolve(),
                   arguments.output.resolve())


if __name__ == "__main__":
    sys.exit(main())
