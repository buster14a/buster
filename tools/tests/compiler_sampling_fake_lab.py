#!/usr/bin/python3
"""Hosted-only sampling stand-in: synthetic records, never compiler execution.

Ownership: native packet-export functional fixture. The fixed compare and
compiler_closure verify routes exercise real process/export/consumer plumbing.
The actual Ryzen 7 9700X is independently refused before either route.
"""
from __future__ import annotations

import argparse
import csv
import hashlib
import json
import os
import re
import stat
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "bench_direct"))
import sampling_qualification_receipt as receipt
import sampling_qualification_test as fixture

HOST = re.compile(r"Ryzen\s+7\s+9700X\b", re.I)
SOURCE_IDS = {"base": "a" * 40, "base_tree": "b" * 40,
              "request_head": "c" * 40, "trusted_revision": "d" * 40,
              "freeze_revision": "d" * 40}
SPAN = 0.000001
LIMIT = 8 * 1024 * 1024


def refuse_actual_host() -> None:
    if sys.platform != "linux":
        raise ValueError("hosted packet fixture requires Linux")
    with open("/proc/cpuinfo", "rb") as handle:
        cpu = handle.read(65537)
    if not cpu or len(cpu) > 65536 or HOST.search(cpu.decode("ascii", errors="strict")):
        raise ValueError("hosted diagnostic fixture refuses the actual Ryzen 7 9700X")


def regular_bytes(path: Path, limit: int = LIMIT) -> bytes:
    path = path.absolute()
    if path != path.resolve(strict=True):
        raise ValueError("fixture input must be canonical, without symbolic links")
    before = path.stat(follow_symlinks=False)
    if not stat.S_ISREG(before.st_mode) or not 0 < before.st_size <= limit:
        raise ValueError("fixture input is not a bounded nonempty regular file")
    descriptor = os.open(path, os.O_RDONLY | os.O_CLOEXEC | os.O_NOFOLLOW)
    try:
        observed = os.fstat(descriptor)
        with os.fdopen(descriptor, "rb", closefd=False) as handle:
            data = handle.read(limit + 1)
        after = os.fstat(descriptor)
        if (before.st_dev, before.st_ino, before.st_size, before.st_mtime_ns) != (
                observed.st_dev, observed.st_ino, observed.st_size, observed.st_mtime_ns) or (
                observed.st_dev, observed.st_ino, observed.st_size, observed.st_mtime_ns) != (
                after.st_dev, after.st_ino, after.st_size, after.st_mtime_ns) or len(data) != before.st_size:
            raise ValueError("fixture input changed while read")
    finally:
        os.close(descriptor)
    return data


def metadata(source: Path) -> dict:
    source = source.absolute()
    if source != source.resolve(strict=True) or not source.is_dir():
        raise ValueError("fixture source root is not a canonical directory")
    value = json.loads(regular_bytes(source / "fixture-metadata.json", 16384))
    if not isinstance(value, dict) or value.get("diagnostic_fixture") is not True or \
            value.get("actual_approved_host") is not False or any(
                value.get(key) != expected for key, expected in SOURCE_IDS.items()):
        raise ValueError("source metadata is not the declared synthetic fixture")
    return value


def write_json(path: Path, value: object) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("x", encoding="utf-8", newline="\n") as handle:
        json.dump(value, handle, sort_keys=True, allow_nan=False, separators=(",", ":"))
        handle.write("\n")


def verify(arguments: list[str]) -> None:
    if len(arguments) != 8 or arguments[:2] != ["compiler_closure", "verify"]:
        raise ValueError("unsupported diagnostic closure argv")
    source, closure, base, tree, output, expected = arguments[2:]
    metadata(Path(source))
    if base != SOURCE_IDS["base"] or tree != SOURCE_IDS["base_tree"] or \
            not re.fullmatch(r"[0-9a-f]{64}", expected):
        raise ValueError("closure verification does not match fixed synthetic source pins")
    observed = hashlib.sha256(regular_bytes(Path(closure))).hexdigest()
    if observed != expected:
        raise ValueError("actual closure bytes differ from the expected digest")
    write_json(Path(output), {"schema": "buster-hosted-sampling-closure-fixture-v1",
               "diagnostic_fixture": True, "actual_approved_host": False,
               "state": "complete", "base": base, "base_tree": tree,
               "closure_sha256": observed, "compiler_executed": False})


def compare(arguments: list[str]) -> None:
    parser = argparse.ArgumentParser(description="Hosted-only synthetic comparison")
    parser.add_argument("command", choices=["compare"])
    for name in ("baseline", "candidate", "repo-root", "output"):
        parser.add_argument("--" + name, required=True)
    parser.add_argument("--cpu", type=int, required=True)
    parser.add_argument("--target-minutes", type=int, required=True)
    parser.add_argument("--warmups", type=int, required=True)
    parser.add_argument("--seed", type=int, required=True)
    parser.add_argument("--min-effect", type=float, required=True)
    parser.add_argument("--pairs", type=int)
    options = parser.parse_args(arguments)
    if (options.cpu, options.target_minutes, options.warmups, options.seed, options.min_effect) != (
            2, 10, 1, 20261003, 0.5) or options.pairs not in (None, 40, 80):
        raise ValueError("compare controls differ from the frozen hosted fixture")
    source = Path(options.repo_root).absolute()
    metadata(source)
    blobs = [regular_bytes(Path(options.baseline)), regular_bytes(Path(options.candidate))]
    if blobs[0] != blobs[1]:
        raise ValueError("pilot-zero A/A fixture must use identical immutable binary bytes")
    binaries = {role: {"sha256": hashlib.sha256(data).hexdigest(),
                       "size_bytes": len(data), "revision": SOURCE_IDS["base"]}
                for role, data in zip(("baseline", "candidate"), blobs)}
    workload = dict(fixture.fixture("pilot", 0)[4]["workload_config"], repo_root=str(source))
    profile = receipt.LONG if options.pairs is None else receipt.SHORT if options.pairs == 40 else receipt.LARGE
    data = fixture.bundle(profile, options.pairs or 0, binaries, workload, 1.0)
    count = data["compare"]["plan"]["pairs"]
    groups = [{"pair": index, "order": receipt._lab.abba_order(index),
               "metrics_a": {"wall": SPAN}, "metrics_b": {"wall": SPAN}}
              for index in range(1, count + 1)]
    for row in data["pairs"]:
        row["span_s"] = SPAN
    wall = receipt._lab.compare_series([(SPAN, SPAN)] * count, "s", "lower",
                                      20261003, time_metric=True, floor=0.005)
    data["summary"]["metrics"]["wall"] = wall
    data["summary"]["verdict"] = dict(wall, metric="wall", min_effect_percent=0.5)
    data["summary"]["checks"] = receipt._lab.compare_checks(groups)
    for row in data["compare"]["steps"].values():
        row["elapsed_s"] = SPAN
    for key in ("compare", "summary"):
        data[key].update(diagnostic_fixture=True, actual_approved_host=False, compiler_executed=False)
    output = Path(options.output).absolute()
    if output.exists() or output.is_symlink():
        raise ValueError("diagnostic trial output must be fresh")
    output.mkdir()
    for name, key in (("compare.json", "compare"), ("pairs.json", "pairs"), ("summary.json", "summary")):
        write_json(output / name, data[key])
    for key in ("a", "b"):
        write_json(output / key / "metadata.json", {
            "diagnostic_fixture": True, "actual_approved_host": False,
            "binary": binaries["baseline" if key == "a" else "candidate"],
            "workload": workload, "compiler_executed": False})
        with (output / key / "commands.log").open("x", encoding="utf-8") as handle:
            handle.write("Hosted synthetic fixture: no compiler command was executed.\n")
    pair_directory = output / "pairs"
    pair_directory.mkdir()
    for row in data["pairs"]:
        with (pair_directory / ("%04d-%s.csv" % (row["pair"], row["variant"]))).open(
                "x", encoding="ascii", newline="") as handle:
            writer = csv.writer(handle)
            writer.writerow(["diagnostic_fixture", "actual_approved_host", "pair", "order", "variant", "span_s"])
            writer.writerow(["true", "false", row["pair"], row["order"], row["variant"], SPAN])
    print("HOSTED_FAKE_LAB diagnostic_fixture=true actual_approved_host=false compiler_executed=false")


def main() -> int:
    result = 1
    try:
        refuse_actual_host()
        arguments = sys.argv[1:]
        if arguments and arguments[0] == "compiler_closure":
            verify(arguments)
        else:
            compare(arguments)
        result = 0
    except (OSError, ValueError, TypeError, KeyError) as error:
        print("hosted sampling fixture rejected: " + str(error), file=sys.stderr)
    return result


if __name__ == "__main__":
    sys.exit(main())
