#!/usr/bin/env python3
"""Fixed #4058 default/default unity and corpus comparison on pull-compare.

The trusted main harness uses the two already prepared Clang Release binaries
on one frozen base tree. Its unchanged native throughput generator supplies six
CI inputs. Twenty fixed syntax/object cells reuse uarch_lab's ABBA/perf/wait4
collector; untimed verbose probes record tree/fact retained bytes separately.
No request-provided commands, source paths, flags, budgets or counts are accepted.

Map: cell_command, validate_documents, validate_probe, split_reference,
execute, main. The publisher independently replays raw pairs and probes.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import shutil
import subprocess
import sys
import time

from compiler_receipt import (APPROVED_HOST, AST_RETIREMENT_CELLS, AST_RETIREMENT_CORPUS,
                              AST_RETIREMENT_INPUT_DIRECTORY, AST_RETIREMENT_PROFILE, AST_RETIREMENT_SCHEMA,
                              _finite_positive, ast_retirement_cell_command, observed_cpu_model,
                              validate_ast_retirement_acceptance)
from inline_acceptance import git, hash_inputs, sha256

CPU = AST_RETIREMENT_PROFILE["cpu"]
PAIRS = AST_RETIREMENT_PROFILE["pairs"]
WARMUPS = AST_RETIREMENT_PROFILE["warmups"]
ROLES = (("a", "baseline"), ("b", "candidate"))
UARCH_SCHEMA = "buster-uarch-lab-compare-v2"
CHUNK_BYTES = 32 * 1024 * 1024
COMPARE_TIMEOUT_SECONDS = 45 * 60
PROBE_TIMEOUT_SECONDS = 10 * 60
PROBE_TEXT_LIMIT = 1024 * 1024
COUNTER_LINES = ("C_AST", "C_AST_FACTS", "C_SYMBOL_BINDINGS")


def cell_command(cell: str) -> list[str]:
    return ast_retirement_cell_command(cell) + (["-o", "OUT"] if AST_RETIREMENT_CELLS[cell]["operation"] == "object" else [])


def shell_join(argv: list[str]) -> str:
    return " ".join(shlex.quote(value) for value in argv)


def binary_binding(binaries: dict) -> dict:
    return {role: {key: binaries[role][key] for key in ("sha256", "size_bytes", "revision")} for _, role in ROLES}


def validate_documents(cell: str, summary: dict, meta: dict, labs: dict, binaries: dict, source_binding: dict) -> dict:
    """Validate fixed commands and counted native metrics, never a flag-pair result."""
    if cell not in AST_RETIREMENT_CELLS or not all(isinstance(value, dict) for value in (summary, meta, labs, binaries, source_binding)):
        raise RuntimeError("retirement cell evidence is not a complete set of objects")
    plan = summary.get("plan") or {}
    if summary.get("schema") != UARCH_SCHEMA or plan.get("pairs") != PAIRS or plan.get("complete_pairs") != PAIRS or \
            plan.get("order") != "ABBA" or plan.get("fresh_copy") is not True:
        raise RuntimeError("retirement series is incomplete or differs from the frozen ABBA plan")
    config = meta.get("config") or {}
    if config.get("ast_retirement_cell") != cell or config.get("command") != shell_join(["IDE", *cell_command(cell)]) or \
            config.get("extra_by_variant") != {"a": [], "b": []} or config.get("extra") != [] or \
            config.get("canonical_inline_pair") is not False or config.get("profile_steps") != [] or \
            config.get("pairs") != PAIRS or config.get("warmups") != WARMUPS or config.get("cpu") != CPU or config.get("fresh_copy") is not True or \
            any(config.get(name) for name in ("ast_pilot_pair", "constant_reuse_pair", "process_floor_pair")):
        raise RuntimeError("retirement series command/variants differ from the fixed default/default cell")
    if summary.get("phase_metrics", {}).get("enabled") is not False or meta.get("phase_metrics", {}).get("enabled") is not False:
        raise RuntimeError("retirement timings enabled phase/output metric collection")
    model = summary.get("host", {}).get("cpu_model")
    if not isinstance(model, str) or not APPROVED_HOST.search(model) or source_binding.get("revision") != binaries.get("baseline", {}).get("revision"):
        raise RuntimeError("retirement host or baseline source binding is unavailable")
    for key, role in ROLES:
        binary = binaries.get(role) or {}
        info, lab = summary.get(role) or {}, labs.get(key) or {}
        lab_config = lab.get("config") or {}
        variant = (meta.get("variants") or {}).get(key) or {}
        if info.get("sha256") != binary.get("sha256") or info.get("size_bytes") != binary.get("size_bytes") or \
                info.get("runs") != PAIRS or info.get("failed") or info.get("deterministic") is not True or info.get("metrics_out_enabled") is not False:
            raise RuntimeError(f"retirement {role} binary/observations differ from the prepared compiler")
        ide = lab_config.get("ide")
        if variant.get("role") != role or variant.get("ide") != ide or variant.get("sha256") != binary.get("sha256") or \
                variant.get("size_bytes") != binary.get("size_bytes") or info.get("path") != ide:
            raise RuntimeError(f"retirement {role} compare identity differs from the raw lab/prepared compiler")
        if not isinstance(ide, str) or lab_config.get("command") != shell_join([ide, *cell_command(cell)]) or \
                lab_config.get("ast_retirement_cell") != cell or lab_config.get("extra") != [] or lab_config.get("cpu") != CPU or \
                lab_config.get("fresh_copy") is not True or lab.get("collection", {}).get("metrics_out") is not False or \
                lab_config.get("repo_root") != config.get("repo_root"):
            raise RuntimeError(f"retirement {role} raw configuration differs from the fixed cell")
    if summary.get("outputs_identical") is not True or summary.get("diagnostics_identical") is not True:
        raise RuntimeError("retirement output or diagnostics differ between the two compilers")
    values = summary.get("metrics") or {}
    for name, fields in (("wall", ("a_median", "b_median", "ratio", "ci_low", "ci_high")),
                         ("instructions", ("a_median", "b_median", "ratio")), ("peak_rss", ("a_median", "b_median"))):
        if not isinstance(values.get(name), dict) or any(not _finite_positive(values[name].get(field)) for field in fields):
            reason = summary.get("counters", {}).get("reason") or "required native metric was not counted"
            raise RuntimeError(f"retirement {name} is incomplete: {reason}")
    return summary


def cell_record(summary: dict) -> dict:
    return {"metrics": {name: summary["metrics"][name] for name in ("wall", "instructions", "peak_rss")},
            "outputs_identical": summary["outputs_identical"], "diagnostics_identical": summary["diagnostics_identical"]}


def counter_lines(log: bytes) -> dict:
    text = log.decode("utf-8", "replace")
    return {name: [line for line in text.splitlines() if line.startswith(name + " ")] for name in COUNTER_LINES}


def retained_field(lines: list, name: str, positive: bool = False) -> int:
    if len(lines) != 1 or not isinstance(lines[0], str):
        raise RuntimeError(f"retirement probe did not report exactly one {name} line")
    found = re.findall(r"(?:^| )retained_bytes=([0-9]+)(?: |$)", lines[0])
    if len(found) != 1 or int(found[0]) > (1 << 64) - 1 or (positive and int(found[0]) == 0):
        raise RuntimeError(f"retirement probe has missing/invalid {name} retained bytes")
    return int(found[0])


def validate_probe(cell: str, probe: dict, binaries: dict, source_binding: dict, logs: dict) -> dict:
    """Re-read raw verbose lines; retain component scopes instead of summing aliases."""
    command = cell_command(cell) + ["-v"]
    source = source_binding["unity"] if AST_RETIREMENT_CELLS[cell]["workload"] == "unity" else source_binding["corpus"][AST_RETIREMENT_CELLS[cell]["workload"]]
    if not isinstance(probe, dict) or probe.get("command") != command or probe.get("source_sha256") != source["sha256"]:
        raise RuntimeError("retirement probe command/source differs from the frozen cell")
    retained = {}
    for key, role in ROLES:
        row = probe.get(key)
        log = logs.get(key)
        if not isinstance(row, dict) or row.get("exit") != 0 or row.get("sha256") != binaries[role]["sha256"] or \
                row.get("size_bytes") != binaries[role]["size_bytes"] or not isinstance(log, bytes) or row.get("lines") != counter_lines(log):
            raise RuntimeError(f"retirement {role} probe identity or raw lines differ")
        lines = row["lines"]
        tree, facts = retained_field(lines["C_AST"], "C_AST", True), retained_field(lines["C_AST_FACTS"], "C_AST_FACTS")
        bindings = retained_field(lines["C_SYMBOL_BINDINGS"], "C_SYMBOL_BINDINGS") if lines["C_SYMBOL_BINDINGS"] else None
        retained[role] = {"tree_retained_bytes": tree, "fact_retained_bytes": facts,
                          "symbol_binding_retained_bytes": bindings,
                          "symbol_binding_status": "reported" if bindings is not None else "unreported",
                          "symbol_binding_reason": "" if bindings is not None else "compiler emits no separate symbol-binding ledger counter"}
    return retained


def split_reference(directory: Path) -> dict:
    """Keep fixed transport chunks under the existing 32-MiB member bound."""
    result = {}
    for key, _ in ROLES:
        reference = directory / key / "reference.exe"
        if reference.stat().st_size > 2 * CHUNK_BYTES:
            raise RuntimeError("retirement reference exceeds its fixed two-chunk transport bound")
        data = reference.read_bytes()
        chunks = []
        for number in range(2):
            block = data[number * CHUNK_BYTES:(number + 1) * CHUNK_BYTES]
            relative = f"{key}/reference.{number:03}.bin"
            (directory / relative).write_bytes(block)
            chunks.append({"path": relative, "size_bytes": len(block), "sha256": hashlib.sha256(block).hexdigest()})
        result[key] = {"size_bytes": len(data), "sha256": hashlib.sha256(data).hexdigest(), "chunks": chunks}
    (directory / "reference.json").write_text(json.dumps(result, sort_keys=True, indent=2) + "\n")
    return result


def probe_cell(cell: str, binaries: dict, root: Path, output: Path, source: dict) -> tuple[dict, dict]:
    output.mkdir(parents=True, exist_ok=True)
    descriptor = source["unity"] if AST_RETIREMENT_CELLS[cell]["workload"] == "unity" else source["corpus"][AST_RETIREMENT_CELLS[cell]["workload"]]
    probe = {"command": cell_command(cell) + ["-v"], "source_sha256": descriptor["sha256"]}
    logs = {}
    for key, role in ROLES:
        produced = output / f"{key}.o"
        argv = [binaries[role]["path"], *[str(produced) if value == "OUT" else value for value in probe["command"]]]
        completed = subprocess.run(argv, cwd=root, capture_output=True, timeout=PROBE_TIMEOUT_SECONDS, check=False)
        log = completed.stdout + completed.stderr
        if len(log) > PROBE_TEXT_LIMIT:
            raise RuntimeError("retirement verbose probe exceeds its fixed text bound")
        (output / f"{key}.log").write_bytes(log)
        logs[key] = log
        probe[key] = {"exit": completed.returncode, "sha256": binaries[role]["sha256"], "size_bytes": binaries[role]["size_bytes"],
                      "lines": counter_lines(log), "output_sha256": sha256(produced) if produced.exists() else None,
                      "output_size_bytes": produced.stat().st_size if produced.exists() else None}
    (output / "probe.json").write_text(json.dumps(probe, sort_keys=True, indent=2) + "\n")
    return probe, logs


def execute(args: argparse.Namespace) -> dict:
    root, output, lab, harness = args.repo_root.resolve(), args.output.resolve(), args.lab.resolve(), args.harness.resolve()
    observed = observed_cpu_model()
    if args.cpu != CPU or not APPROVED_HOST.search(observed):
        raise RuntimeError("retirement acceptance requires CPU 2 on the approved physical Ryzen 7 9700X")
    if git(root, "rev-parse", "HEAD") != args.base_revision or not lab.is_file() or not harness.is_file():
        raise RuntimeError("retirement source or trusted lab/base throughput generator is missing")
    if output.exists() and any(output.iterdir()):
        raise RuntimeError("retirement output directory must be new or empty")
    output.mkdir(parents=True, exist_ok=True)
    binaries = {role: {"path": str(path.resolve()), "sha256": sha256(path), "size_bytes": path.stat().st_size, "revision": revision}
                for role, path, revision in (("baseline", args.baseline_ide, args.base_revision), ("candidate", args.candidate_ide, args.head_revision))}
    before = hash_inputs(root)
    inputs = root / AST_RETIREMENT_INPUT_DIRECTORY
    if inputs.exists():
        raise RuntimeError("retirement corpus input directory must not already exist")
    inputs.parent.mkdir(parents=True, exist_ok=True)
    started = time.monotonic()
    with (output / "generator.log").open("wb") as log:
        subprocess.run([str(harness), "generate", "--profile", "ci", "--output", str(inputs)], cwd=root,
                       stdout=log, stderr=subprocess.STDOUT, check=True, timeout=120)
    (output / "inputs").mkdir()
    corpus = {}
    for name in AST_RETIREMENT_CORPUS:
        path = inputs / f"{name}.c"
        if path.is_symlink() or not path.is_file():
            raise RuntimeError("retirement generator produced no regular fixed corpus source")
        corpus[name] = {"path": path.relative_to(root).as_posix(), "sha256": sha256(path), "size_bytes": path.stat().st_size}
        shutil.copyfile(path, output / "inputs" / path.name)
    unity = root / "src/buster/apps/ide/ide.c"
    shutil.copyfile(unity, output / "inputs" / "unity.c")
    source = {"revision": args.base_revision, "frozen_input_tree": before,
              "unity": {"path": unity.relative_to(root).as_posix(), "sha256": sha256(unity), "size_bytes": unity.stat().st_size},
              "corpus": corpus, "generator": {"sha256": sha256(harness), "size_bytes": harness.stat().st_size, "revision": args.base_revision}}
    (output / "source.json").write_text(json.dumps(source, sort_keys=True, indent=2) + "\n")
    cells, probes = {}, {}
    for cell in AST_RETIREMENT_CELLS:
        directory = output / cell
        subprocess.run([sys.executable, "-B", str(lab), "compare", "--baseline", binaries["baseline"]["path"],
                        "--candidate", binaries["candidate"]["path"], "--repo-root", str(root), "--cpu", str(CPU),
                        "--output", str(directory), "--pairs", str(PAIRS), "--warmups", str(WARMUPS), "--perf", args.perf,
                        "--ast-retirement-cell", cell, "--require-identical-output"], check=True, timeout=COMPARE_TIMEOUT_SECONDS)
        summary = json.loads((directory / "summary.json").read_text())
        meta = json.loads((directory / "compare.json").read_text())
        labs = {key: json.loads((directory / key / "lab.json").read_text()) for key, _ in ROLES}
        cells[cell] = cell_record(validate_documents(cell, summary, meta, labs, binaries, source))
        references = split_reference(directory)
        probe, logs = probe_cell(cell, binaries, root, output / "probe" / cell, source)
        probes[cell] = validate_probe(cell, probe, binaries, source, logs)
        if AST_RETIREMENT_CELLS[cell]["operation"] == "object" and any(
                probe[key]["output_sha256"] != references[key]["sha256"] or probe[key]["output_size_bytes"] != references[key]["size_bytes"]
                for key, _ in ROLES):
            raise RuntimeError("untimed retained-byte probe differs from its timed reference object")
    if hash_inputs(root) != before or git(root, "rev-parse", "HEAD") != args.base_revision or sha256(harness) != source["generator"]["sha256"] or any(
            sha256(inputs / f"{name}.c") != corpus[name]["sha256"] for name in AST_RETIREMENT_CORPUS) or any(
            sha256(Path(binary["path"])) != binary["sha256"] for binary in binaries.values()):
        raise RuntimeError("retirement compiler/source/header/generator identities changed during measurement")
    result = {"schema": AST_RETIREMENT_SCHEMA, "status": "complete", "profile": AST_RETIREMENT_PROFILE,
              "base_revision": args.base_revision, "head_revision": args.head_revision, "binaries": binary_binding(binaries),
              "source": source, "cpu_model": observed, "cells": cells, "probes": probes,
              "elapsed_seconds": round(time.monotonic() - started, 3)}
    problems = validate_ast_retirement_acceptance(result, args.base_revision, args.head_revision, binaries)
    if problems:
        raise RuntimeError("; ".join(problems))
    return result


def parse(argv=None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--lab", type=Path, required=True)
    parser.add_argument("--harness", type=Path, required=True)
    parser.add_argument("--baseline-ide", type=Path, required=True)
    parser.add_argument("--candidate-ide", type=Path, required=True)
    parser.add_argument("--repo-root", type=Path, required=True)
    parser.add_argument("--base-revision", required=True)
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
        temporary.write_text(json.dumps(result, sort_keys=True, indent=2) + "\n")
        os.replace(temporary, args.output / "acceptance.json")
    except (OSError, ValueError, RuntimeError, subprocess.SubprocessError) as error:
        print(f"ast_retirement_acceptance: {error}", file=sys.stderr)
        return 1
    print("ast_retirement_acceptance: complete")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
