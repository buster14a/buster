#!/usr/bin/env python3
"""Bounded same-candidate per-process floor comparison for issue #1125.

The owner-authorized compiler comparison has already built candidate HEAD with
its tests-off Clang Release toolchain. This profile writes PROCESS_FLOOR_SOURCE
as tiny.c into a directory of its own and runs one fixed `uarch_lab.py compare
--process-floor-pair` per cell of PROCESS_FLOOR_PROFILE there, with the one
candidate binary as both A and B. A is always the native x86-64 `-g0` object
compile; B is the same command (aa, the A/A calibration), the AArch64 object
compile (aarch64) or `-fsyntax-only` (syntax-only). It is report-only: it
records wall time, task-clock, the compiler's own wall_ns, available retired
instructions and peak RSS, requires identical diagnostics in every cell and
byte-identical A/A objects, and applies the issue's stop rule
(compiler_receipt.process_floor_verdict). No request-provided commands,
compiler flags, sources or pair counts are accepted.

Map: write_source, run_compare, validate_documents (shared with the publisher),
load_complete, metrics, cell_record, execute, main.
"""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import subprocess
import sys
import time

from compiler_receipt import (PROCESS_FLOOR_CELLS, PROCESS_FLOOR_OBJECT, PROCESS_FLOOR_PROFILE, PROCESS_FLOOR_SCHEMA,
                              PROCESS_FLOOR_SOURCE, PROCESS_FLOOR_SOURCE_NAME, _finite_positive, process_floor_verdict)
from inline_acceptance import git, sha256

PAIRS = PROCESS_FLOOR_PROFILE["pairs"]
WARMUPS = PROCESS_FLOOR_PROFILE["warmups"]
CPU = PROCESS_FLOOR_PROFILE["cpu"]
UARCH_SCHEMA = "buster-uarch-lab-compare-v2"
# One cell is 2 * (PAIRS + WARMUPS) tiny compiles plus a fresh binary copy per
# timed run. All three cells took 99 s on a 4-vCPU development container with
# a 70 MB tests-on binary and no perf.
COMPARE_TIMEOUT_SECONDS = 20 * 60


def variant_command(cell: str, key: str) -> list[str]:
    """The compiler argv after the binary for one variant, exactly as uarch_lab records it."""
    argv = list(PROCESS_FLOOR_CELLS[cell] if key == "b" else PROCESS_FLOOR_OBJECT)
    return argv + (["-o", "OUT"] if "-c" in argv else [])


def write_source(directory: Path) -> Path:
    """The frozen source, alone in a new directory; refused when its digest is not the profile's."""
    directory.mkdir(parents=True)
    path = directory / PROCESS_FLOOR_SOURCE_NAME
    path.write_bytes(PROCESS_FLOOR_SOURCE.encode("utf-8"))
    if sha256(path) != PROCESS_FLOOR_PROFILE["source_sha256"]:
        raise RuntimeError("the written floor source does not match the profile digest")
    return path


def run_compare(lab: Path, compiler: Path, root: Path, cpu: int, output: Path, perf: str, cell: str) -> None:
    argv = [sys.executable, "-B", str(lab), "compare",
            "--baseline", str(compiler), "--candidate", str(compiler),
            "--repo-root", str(root), "--cpu", str(cpu), "--output", str(output),
            "--pairs", str(PAIRS), "--warmups", str(WARMUPS), "--perf", perf,
            "--process-floor-pair", cell]
    subprocess.run(argv, check=True, timeout=COMPARE_TIMEOUT_SECONDS)


def _positive_number(value) -> bool:
    return _finite_positive(value)


def validate_documents(cell: str, summary: dict, meta: dict, labs: dict, expected_sha: str) -> dict:
    """Validate one cell's raw bounded lab documents without trusting the host wrapper."""
    if cell not in PROCESS_FLOOR_CELLS:
        raise RuntimeError(f"unknown process-floor cell {cell!r}")
    if not isinstance(summary, dict) or not isinstance(meta, dict) or not isinstance(labs, dict):
        raise RuntimeError("uarch evidence is not a set of JSON objects")
    plan = summary.get("plan") or {}
    if summary.get("schema") != UARCH_SCHEMA or plan.get("pairs") != PAIRS or plan.get("complete_pairs") != PAIRS or \
            plan.get("order") != "ABBA" or plan.get("fresh_copy") is not True:
        raise RuntimeError("uarch comparison incomplete or wrong schema")
    for role in ("baseline", "candidate"):
        info = summary.get(role) or {}
        if info.get("failed") or not info.get("deterministic") or info.get("runs") != PAIRS:
            raise RuntimeError(f"{role} did not produce {PAIRS} deterministic timed runs")
        if info.get("sha256") != expected_sha:
            raise RuntimeError(f"timed {role} binary differs from the frozen candidate-HEAD compiler")
    config = meta.get("config") or {}
    commands = {key: " ".join(["IDE"] + variant_command(cell, key)) for key in ("a", "b")}
    if (config.get("process_floor_pair") != cell or config.get("extra_by_variant") != {"a": [], "b": []} or
            config.get("command_by_variant") != commands or config.get("command") != commands["a"] or
            config.get("canonical_inline_pair") is not False or config.get("ast_pilot_pair") is not None or
            config.get("constant_reuse_pair") is not None or config.get("pairs") != PAIRS or
            config.get("warmups") != WARMUPS or config.get("cpu") != CPU or config.get("extra") != [] or
            config.get("profile_steps") != [] or config.get("fresh_copy") is not True):
        raise RuntimeError(f"uarch run did not use the fixed process-floor {cell} profile")
    for key in ("a", "b"):
        lab_config = (labs.get(key) or {}).get("config") or {}
        ide = lab_config.get("ide")
        wanted = " ".join([str(ide)] + variant_command(cell, key))
        if lab_config.get("extra") != [] or lab_config.get("cpu") != CPU or \
                lab_config.get("process_floor_cell") != cell or not isinstance(ide, str) or lab_config.get("command") != wanted:
            raise RuntimeError(f"uarch {key} compile command or CPU pin does not match the fixed profile")
    if summary.get("diagnostics_identical") is not True:
        raise RuntimeError(f"uarch {cell} diagnostics differ between the two commands")
    if cell == "aa" and summary.get("outputs_identical") is not True:
        raise RuntimeError("uarch aa objects differ between two runs of the same command")
    values = summary.get("metrics") or {}
    for key in ("wall", "task_clock", "compiler_wall", "instructions", "peak_rss"):
        if not isinstance(values.get(key), dict):
            raise RuntimeError(f"uarch metric {key} missing")
    wall = values["wall"]
    if not all(_positive_number(wall.get(key)) for key in ("a_median", "b_median", "ratio", "ci_low", "ci_high")):
        raise RuntimeError("uarch wall-time measurements are missing")
    if not all(_positive_number(values["task_clock"].get(key)) for key in ("a_median", "b_median")):
        raise RuntimeError("uarch task-clock measurements are missing")
    for key in ("compiler_wall", "peak_rss"):
        if any(values[key].get(field) is not None and not _positive_number(values[key].get(field))
               for field in ("a_median", "b_median")):
            raise RuntimeError(f"uarch {key} measurements are invalid")
    counters = summary.get("counters") or {}
    instruction = values["instructions"]
    if instruction.get("ratio") is None:
        if counters.get("perf_stat") is not False or not counters.get("reason"):
            raise RuntimeError("instruction counts are NA without a recorded unavailable-counter reason")
    elif not _positive_number(instruction["ratio"]) or \
            not all(_positive_number(instruction.get(key)) for key in ("a_median", "b_median")):
        raise RuntimeError("instruction counts are invalid")
    return summary


def load_complete(path: Path, cell: str, expected_sha: str) -> dict:
    """Read one cell's local lab documents, then apply the shared publisher validator."""
    try:
        summary = json.loads((path / "summary.json").read_text(encoding="utf-8"))
        meta = json.loads((path / "compare.json").read_text(encoding="utf-8"))
        labs = {key: json.loads((path / key / "lab.json").read_text(encoding="utf-8")) for key in ("a", "b")}
    except (OSError, ValueError) as error:
        raise RuntimeError(f"uarch evidence unreadable at {path}: {error}") from error
    try:
        return validate_documents(cell, summary, meta, labs, expected_sha)
    except (RuntimeError, AttributeError, TypeError, KeyError) as error:
        raise RuntimeError(f"invalid or incomplete uarch evidence at {path}: {error}") from error


def metrics(summary: dict) -> dict:
    values = summary["metrics"]
    return {"wall": values["wall"], "task_clock": values["task_clock"], "compiler_wall": values["compiler_wall"],
            "instructions": values["instructions"], "peak_rss": values["peak_rss"],
            "counter_availability": summary.get("counters")}


def cell_record(summary: dict) -> dict:
    return {"metrics": metrics(summary), "outputs_identical": summary["outputs_identical"],
            "diagnostics_identical": summary["diagnostics_identical"]}


def execute(args: argparse.Namespace) -> dict:
    lab, root, candidate = args.lab.resolve(), args.repo_root.resolve(), args.candidate_ide.resolve()
    output = args.output.resolve()
    if not lab.is_file() or not candidate.is_file() or not root.is_dir():
        raise RuntimeError("trusted lab, candidate compiler or candidate-HEAD checkout is missing")
    if args.cpu != CPU:
        raise RuntimeError(f"the fixed profile pins CPU {CPU}")
    observed = git(root, "rev-parse", "HEAD")
    if observed != args.head_revision:
        raise RuntimeError(f"source checkout is {observed}, expected candidate HEAD {args.head_revision}")
    if output.exists() and any(output.iterdir()):
        raise RuntimeError("output directory must be new or empty")
    output.mkdir(parents=True, exist_ok=True)
    source = write_source(output / "source")
    candidate_sha = sha256(candidate)
    started = time.monotonic()
    cells = {}
    for cell in PROCESS_FLOOR_CELLS:
        run_compare(lab, candidate, source.parent, args.cpu, output / cell, args.perf, cell)
        cells[cell] = cell_record(load_complete(output / cell, cell, candidate_sha))
    if sha256(source) != PROCESS_FLOOR_PROFILE["source_sha256"] or \
            sorted(path.name for path in source.parent.iterdir()) != [PROCESS_FLOOR_SOURCE_NAME]:
        raise RuntimeError("the floor source directory changed during the process-floor profile")
    if sha256(candidate) != candidate_sha:
        raise RuntimeError("candidate compiler binary changed during the process-floor profile")
    return {"schema": PROCESS_FLOOR_SCHEMA, "status": "complete", "profile": PROCESS_FLOOR_PROFILE,
            "source_revision": observed,
            "candidate_compiler": {"sha256": candidate_sha, "size_bytes": candidate.stat().st_size},
            "cells": cells, "verdict": process_floor_verdict(cells),
            "elapsed_seconds": round(time.monotonic() - started, 3)}


def parse(argv=None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--lab", type=Path, required=True, help="trusted uarch_lab.py")
    parser.add_argument("--candidate-ide", type=Path, required=True, help="the already-built candidate ide")
    parser.add_argument("--repo-root", type=Path, required=True, help="candidate-HEAD source checkout")
    parser.add_argument("--head-revision", required=True)
    parser.add_argument("--cpu", type=int, required=True)
    parser.add_argument("--perf", default="perf")
    parser.add_argument("--output", type=Path, required=True)
    return parser.parse_args(argv)


def main(argv=None) -> int:
    args = parse(argv)
    try:
        result = execute(args)
        temporary = args.output / "acceptance.json.tmp"
        temporary.write_text(json.dumps(result, sort_keys=True, indent=2) + "\n", encoding="utf-8")
        os.replace(temporary, args.output / "acceptance.json")
    except (OSError, RuntimeError, subprocess.SubprocessError) as error:
        print(f"process_floor_acceptance: {error}", file=sys.stderr)
        return 1
    print("process_floor_acceptance: complete")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
