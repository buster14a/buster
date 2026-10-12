#!/usr/bin/env python3
"""Bounded same-candidate -fc-ast-pilot adoption comparison for issue #3397.

The owner-authorized compiler comparison has already built candidate HEAD with
its tests-off Clang Release toolchain. While that checkout and its
build/generated closure remain at HEAD, this profile freezes the source/header
tree digests and runs one fixed `uarch_lab.py compare --ast-pilot-pair` per
cell of AST_PILOT_ACCEPTANCE_PROFILE (-fsyntax-only, then -c) on that tree,
with the one candidate binary as both A (default) and B (-fc-ast-pilot). An
untimed `-v` syntax-only run of each variant then records the tree's retained
bytes from B and proves A built no tree. The verdict is the ast.md stage-1
adoption budget (compiler_receipt.ast_pilot_budget_verdict), report-only.
No request-provided commands, compiler flags, source paths or pair counts are
accepted.

Map: run_compare, probe_tree, validate_documents (shared with the publisher),
validate_probe, load_complete, metrics, execute, main.
"""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import time

from compiler_receipt import (AST_PILOT_ACCEPTANCE_PROFILE, AST_PILOT_ACCEPTANCE_SCHEMA, AST_PILOT_CELLS,
                              AST_PILOT_COMPILE, AST_PILOT_FLAG, _finite_positive, ast_pilot_budget_verdict)
from inline_acceptance import git, hash_inputs, sha256

PAIRS = AST_PILOT_ACCEPTANCE_PROFILE["pairs"]
WARMUPS = AST_PILOT_ACCEPTANCE_PROFILE["warmups"]
CPU = AST_PILOT_ACCEPTANCE_PROFILE["cpu"]
UARCH_SCHEMA = "buster-uarch-lab-compare-v2"
EXTRA_BY_VARIANT = {"a": [], "b": [AST_PILOT_FLAG]}
COMPARE_TIMEOUT_SECONDS = 45 * 60
PROBE_TIMEOUT_SECONDS = 10 * 60
PROBE_TEXT_LIMIT = 1024 * 1024
# The `-v` line ide.c prints when the pilot built a tree (c_ast_pilot report).
TREE_LINE = re.compile(r"C_AST nodes=([0-9]+) tokens=([0-9]+) build_ns=[0-9]+ retained_bytes=([0-9]+) "
                       r"transient_high_water=([0-9]+) column_copy_bytes=[0-9]+ finalize_child_entries=[0-9]+ layout=([a-z]+)")


def cell_command(cell: str) -> list[str]:
    """The compiler argv after the binary, exactly as uarch_lab records it."""
    return AST_PILOT_COMPILE + [AST_PILOT_CELLS[cell]] + (["-o", "OUT"] if cell == "object" else [])


def run_compare(lab: Path, compiler: Path, root: Path, cpu: int, output: Path, perf: str, cell: str) -> None:
    argv = [sys.executable, "-B", str(lab), "compare",
            "--baseline", str(compiler), "--candidate", str(compiler),
            "--repo-root", str(root), "--cpu", str(cpu), "--output", str(output),
            "--pairs", str(PAIRS), "--warmups", str(WARMUPS), "--perf", perf,
            "--ast-pilot-pair", cell]
    subprocess.run(argv, check=True, timeout=COMPARE_TIMEOUT_SECONDS)


def probe_tree(compiler: Path, root: Path, output: Path) -> dict:
    """Untimed `-v` syntax-only runs of A and B; their logs and probe.json go to output."""
    output.mkdir(parents=True, exist_ok=True)
    result = {"command": AST_PILOT_COMPILE + [AST_PILOT_CELLS["syntax-only"], "-v"]}
    for key, extra in EXTRA_BY_VARIANT.items():
        argv = [str(compiler), *AST_PILOT_COMPILE, AST_PILOT_CELLS["syntax-only"], *extra, "-v"]
        completed = subprocess.run(argv, cwd=root, capture_output=True, timeout=PROBE_TIMEOUT_SECONDS, check=False)
        text = (completed.stdout + completed.stderr)[-PROBE_TEXT_LIMIT:].decode("utf-8", "replace")
        (output / f"{key}.log").write_text(text, encoding="utf-8")
        lines = [line for line in text.splitlines() if line.startswith("C_AST ")]
        result[key] = {"exit": completed.returncode, "extra": extra, "tree_lines": lines}
    (output / "probe.json").write_text(json.dumps(result, sort_keys=True, indent=2) + "\n", encoding="utf-8")
    return result


def validate_probe(probe: object) -> dict:
    """{retained_bytes, nodes, tokens, transient_high_water, layout, a_tree_lines} from probe.json."""
    if not isinstance(probe, dict) or probe.get("command") != AST_PILOT_COMPILE + [AST_PILOT_CELLS["syntax-only"], "-v"]:
        raise RuntimeError("tree probe command is not the fixed syntax-only -v command")
    rows = {key: probe.get(key) for key in EXTRA_BY_VARIANT}
    for key, row in rows.items():
        if not isinstance(row, dict) or row.get("exit") != 0 or row.get("extra") != EXTRA_BY_VARIANT[key] or \
                not isinstance(row.get("tree_lines"), list):
            raise RuntimeError(f"tree probe {key} did not run the fixed flags successfully")
    if rows["a"]["tree_lines"]:
        raise RuntimeError("variant A built a syntax tree; -fc-ast-pilot was not omitted")
    lines = rows["b"]["tree_lines"]
    match = TREE_LINE.fullmatch(lines[0]) if len(lines) == 1 and isinstance(lines[0], str) else None
    if match is None or int(match.group(3)) <= 0 or int(match.group(1)) <= 0:
        raise RuntimeError("variant B reported no single C_AST line with retained bytes; -fc-ast-pilot did not run")
    return {"nodes": int(match.group(1)), "tokens": int(match.group(2)), "retained_bytes": int(match.group(3)),
            "transient_high_water": int(match.group(4)), "layout": match.group(5), "a_tree_lines": 0}


def _positive_number(value) -> bool:
    return _finite_positive(value)


def validate_documents(cell: str, summary: dict, meta: dict, labs: dict, expected_sha: str) -> dict:
    """Validate one cell's raw bounded lab documents without trusting the host wrapper."""
    if cell not in AST_PILOT_CELLS:
        raise RuntimeError(f"unknown ast-pilot cell {cell!r}")
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
    command = " ".join(["IDE"] + cell_command(cell))
    config = meta.get("config") or {}
    if (config.get("ast_pilot_pair") != cell or config.get("extra_by_variant") != EXTRA_BY_VARIANT or
            config.get("canonical_inline_pair") is not False or config.get("command") != command or
            config.get("pairs") != PAIRS or config.get("warmups") != WARMUPS or config.get("cpu") != CPU or
            config.get("extra") != [] or config.get("profile_steps") != [] or config.get("fresh_copy") is not True):
        raise RuntimeError(f"uarch run did not use the fixed -fc-ast-pilot {cell} profile")
    for key, expected in EXTRA_BY_VARIANT.items():
        lab_config = (labs.get(key) or {}).get("config") or {}
        ide = lab_config.get("ide")
        argv = cell_command(cell)
        wanted = " ".join([str(ide)] + argv[:-2] + expected + argv[-2:] if cell == "object" else [str(ide)] + argv + expected)
        if lab_config.get("extra") != expected or lab_config.get("cpu") != CPU or \
                lab_config.get("ast_pilot_cell") != cell or not isinstance(ide, str) or lab_config.get("command") != wanted:
            raise RuntimeError(f"uarch {key} compile flags, command or CPU pin do not match the fixed profile")
    for key in ("outputs_identical", "diagnostics_identical"):
        if type(summary.get(key)) is not bool:
            raise RuntimeError(f"uarch {cell} {key} is not recorded")
    values = summary.get("metrics") or {}
    for key in ("wall", "instructions", "peak_rss"):
        if not isinstance(values.get(key), dict):
            raise RuntimeError(f"uarch metric {key} missing")
    wall = values["wall"]
    if not all(_positive_number(wall.get(key)) for key in ("a_median", "b_median", "ratio", "ci_low", "ci_high")):
        raise RuntimeError("uarch wall-time measurements are missing")
    if not all(_positive_number(values["peak_rss"].get(key)) for key in ("a_median", "b_median")):
        raise RuntimeError("uarch peak-RSS measurements are missing")
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
    return {"wall": values["wall"], "instructions": values["instructions"], "peak_rss": values["peak_rss"],
            "counter_availability": summary.get("counters")}


def cell_record(summary: dict) -> dict:
    return {"metrics": metrics(summary), "outputs_identical": summary["outputs_identical"],
            "diagnostics_identical": summary["diagnostics_identical"]}


def verdict(cells: dict, tree: dict) -> dict:
    correctness = {"object_outputs_identical": cells["object"]["outputs_identical"],
                   "diagnostics_identical": all(cells[cell]["diagnostics_identical"] for cell in AST_PILOT_CELLS)}
    return ast_pilot_budget_verdict({cell: cells[cell]["metrics"] for cell in AST_PILOT_CELLS},
                                    tree["retained_bytes"], correctness)


def execute(args: argparse.Namespace) -> dict:
    lab, root, candidate = args.lab.resolve(), args.repo_root.resolve(), args.candidate_ide.resolve()
    output = args.output.resolve()
    source = root / "src/buster/apps/ide/ide.c"
    generated = root / "build/generated"
    if not lab.is_file() or not candidate.is_file() or not source.is_file() or not generated.is_dir():
        raise RuntimeError("trusted lab, candidate compiler, or generated candidate-HEAD unity inputs are missing")
    if args.cpu != CPU:
        raise RuntimeError(f"the fixed profile pins CPU {CPU}")
    observed = git(root, "rev-parse", "HEAD")
    if observed != args.head_revision:
        raise RuntimeError(f"source checkout is {observed}, expected candidate HEAD {args.head_revision}")
    if output.exists() and any(output.iterdir()):
        raise RuntimeError("output directory must be new or empty")
    output.mkdir(parents=True, exist_ok=True)
    candidate_sha = sha256(candidate)
    frozen_before = hash_inputs(root)
    started = time.monotonic()
    cells = {}
    for cell in AST_PILOT_CELLS:
        run_compare(lab, candidate, root, args.cpu, output / cell, args.perf, cell)
        cells[cell] = cell_record(load_complete(output / cell, cell, candidate_sha))
    tree = validate_probe(probe_tree(candidate, root, output / "probe"))
    frozen_after = hash_inputs(root)
    if frozen_after != frozen_before or git(root, "rev-parse", "HEAD") != args.head_revision:
        raise RuntimeError("candidate source/generated-header inputs changed during the ast-pilot profile")
    if sha256(candidate) != candidate_sha:
        raise RuntimeError("candidate compiler binary changed during the ast-pilot profile")
    profile = dict(AST_PILOT_ACCEPTANCE_PROFILE, source_sha256=sha256(source))
    return {"schema": AST_PILOT_ACCEPTANCE_SCHEMA, "status": "complete", "profile": profile,
            "source_revision": observed, "frozen_input_tree": frozen_before,
            "candidate_compiler": {"sha256": candidate_sha, "size_bytes": candidate.stat().st_size},
            "cells": cells, "tree": tree, "verdict": verdict(cells, tree),
            "elapsed_seconds": round(time.monotonic() - started, 3)}


def parse(argv=None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--lab", type=Path, required=True, help="trusted uarch_lab.py")
    parser.add_argument("--candidate-ide", type=Path, required=True, help="the already-built candidate ide")
    parser.add_argument("--repo-root", type=Path, required=True, help="candidate-HEAD source checkout with generated headers")
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
        print(f"ast_pilot_acceptance: {error}", file=sys.stderr)
        return 1
    print("ast_pilot_acceptance: complete")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
