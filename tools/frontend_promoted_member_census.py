#!/usr/bin/env python3
"""Hosted work-count census for c_ir_promoted_member_path.

This diagnostic temporarily instruments the production source on a disposable
checkout. It collects deterministic work counts only; it does not time a
compiler or introduce a production counter.
"""
from __future__ import annotations

import argparse
import collections
import difflib
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys


SOURCE = Path("src/buster/lib/compiler/frontend/c/c_gen.c")
SELF_HOST_SOURCE = Path("src/buster/apps/ide/ide.c")
WORK_RE = re.compile(
    r"PROMOTED_MEMBER_CALL capacity=(\d+) queue=(\d+) comparisons=(\d+) "
    r"fields=(\d+) requested=(\d+) found=(\d+) ambiguous=(\d+)"
)
WORK_KEYS = ("capacity", "queue", "comparisons", "fields", "requested", "found", "ambiguous")


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def replace_once(text: str, old: str, new: str) -> str:
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"expected one source anchor, found {count}: {old!r}")
    return text.replace(old, new, 1)


def record_line(comparisons: str = "probe_comparisons", fields: str = "probe_fields") -> str:
    return (
        'fprintf(stderr, "PROMOTED_MEMBER_CALL capacity=%u queue=%u comparisons=%llu fields=%llu '
        'requested=%llu found=%u ambiguous=%u\\n", '
        "capacity, work_count, (unsigned long long)"
        + comparisons
        + ", (unsigned long long)"
        + fields
        + ", (unsigned long long)(sizeof(CIrPromotedMemberWork) * (u64)capacity), (unsigned)found, (unsigned)ambiguous);\n"
    )


def instrument(root: Path, out: Path) -> int:
    source_path = root / SOURCE
    original = source_path.read_text()
    original_bytes = original.encode()
    begin = original.index("BUSTER_C_INTERNAL bool c_ir_promoted_member_queued(")
    path_begin = original.index("BUSTER_C_INTERNAL bool c_ir_promoted_member_path(", begin)
    path_end = original.index("BUSTER_C_INTERNAL bool c_ir_promoted_member_type(", path_begin)
    helper = original[begin:path_begin]
    path = original[path_begin:path_end]

    helper = replace_once(
        helper,
        "u32 count, IrTypeId type)",
        "u32 count, IrTypeId type, u64* probe_comparisons)",
    )
    helper = replace_once(
        helper,
        "        if (queued[index].type.value == type.value)",
        "        *probe_comparisons += 1;\n        if (queued[index].type.value == type.value)",
    )
    path = replace_once(
        path,
        "BUSTER_C_INTERNAL bool c_ir_promoted_member_path(CIntegerIrBuilder* builder, IrTypeId root, String8 member, CIrPromotedMemberPath* result)\n{\n",
        "BUSTER_C_INTERNAL bool c_ir_promoted_member_path(CIntegerIrBuilder* builder, IrTypeId root, String8 member, CIrPromotedMemberPath* result)\n{\n"
        "    u64 probe_comparisons = 0;\n"
        "    u64 probe_fields = 0;\n",
    )
    path = replace_once(
        path,
        "    if (!capacity || root.value >= capacity)\n    {\n        return false;\n    }",
        "    if (!capacity || root.value >= capacity)\n    {\n"
        "        fprintf(stderr, \"PROMOTED_MEMBER_CALL capacity=%u queue=0 comparisons=0 fields=0 requested=0 found=0 ambiguous=0\\n\", capacity);\n"
        "        return false;\n    }",
    )
    path = replace_once(
        path,
        "            IrField* field = type->fields + field_index;",
        "            probe_fields += 1;\n            IrField* field = type->fields + field_index;",
    )
    path = replace_once(
        path,
        "!c_ir_promoted_member_queued(work, work_count, field->type)",
        "!c_ir_promoted_member_queued(work, work_count, field->type, &probe_comparisons)",
    )
    path = replace_once(
        path,
        "                if (current.offset > UINT64_MAX - field->offset)\n                {\n"
        "                    scratch_end(promoted_member_scratch);\n                    return false;\n                }",
        "                if (current.offset > UINT64_MAX - field->offset)\n                {\n"
        "                    scratch_end(promoted_member_scratch);\n"
        "                    "
        + record_line()
        + "                    return false;\n                }",
    )
    path = replace_once(
        path,
        "    scratch_end(promoted_member_scratch);\n    return found && !ambiguous;",
        "    " + record_line() + "    scratch_end(promoted_member_scratch);\n    return found && !ambiguous;",
    )

    modified = (
        "#include <stdio.h> /* disposable hosted census instrumentation */\n"
        + original[:begin]
        + helper
        + path
        + original[path_end:]
    )
    out.mkdir(parents=True, exist_ok=True)
    (out / "c_gen.c.before").write_bytes(original_bytes)
    (out / "instrumentation.patch").write_text(
        "".join(
            difflib.unified_diff(
                original.splitlines(keepends=True),
                modified.splitlines(keepends=True),
                fromfile=str(SOURCE),
                tofile=str(SOURCE) + ".instrumented",
            )
        )
    )
    source_path.write_text(modified)
    metadata = {
        "source": str(SOURCE),
        "source_sha256_before": hashlib.sha256(original_bytes).hexdigest(),
        "source_sha256_instrumented": sha256(source_path),
        "instrumentation_scope": "local counters in c_ir_promoted_member_path and its queue predicate",
        "instrumentation_effects": "stderr records only; no timing, cache, result change, or production file retained",
    }
    (out / "instrumentation.json").write_text(json.dumps(metadata, indent=2) + "\n")
    print(json.dumps(metadata, sort_keys=True))
    return 0


def run_command(argv: list[str], root: Path, stdout_path: Path, stderr_path: Path, timeout: int = 300) -> dict:
    try:
        result = subprocess.run(argv, cwd=root, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=timeout)
        stdout = result.stdout
        stderr = result.stderr
        code = result.returncode
    except subprocess.TimeoutExpired as exc:
        stdout_value = exc.stdout or ""
        stderr_value = exc.stderr or ""
        stdout = stdout_value.decode(errors="replace") if isinstance(stdout_value, bytes) else stdout_value
        stderr = stderr_value.decode(errors="replace") if isinstance(stderr_value, bytes) else stderr_value
        stderr += "\nCOMMAND TIMEOUT\n"
        code = 124
    stdout_path.write_text(stdout)
    stderr_path.write_text(stderr)
    return {"argv": argv, "exit": code, "stdout": str(stdout_path), "stderr": str(stderr_path)}


def parse_metrics(path: Path) -> dict[str, str]:
    values: dict[str, str] = {}
    if path.is_file():
        for line in path.read_text().splitlines():
            if "=" in line:
                key, value = line.split("=", 1)
                values[key] = value
    return values


def summarize(stderr_path: Path) -> dict:
    calls = []
    if stderr_path.is_file():
        for match in WORK_RE.finditer(stderr_path.read_text()):
            calls.append(dict(zip(WORK_KEYS, map(int, match.groups()))))
    histogram = collections.Counter(row["queue"] for row in calls)
    return {
        "calls": len(calls),
        "comparisons": sum(row["comparisons"] for row in calls),
        "fields": sum(row["fields"] for row in calls),
        "requested_bytes": sum(row["requested"] for row in calls),
        "max_queue": max((row["queue"] for row in calls), default=0),
        "queue_histogram": {str(key): histogram[key] for key in sorted(histogram)},
        "calls_with_comparisons": sum(row["comparisons"] != 0 for row in calls),
        "max_comparisons_per_call": max((row["comparisons"] for row in calls), default=0),
        "total_type_slots": sum(row["capacity"] for row in calls),
        "found": sum(row["found"] for row in calls),
        "ambiguous": sum(row["ambiguous"] for row in calls),
        "records": calls,
    }


def run_census(args: argparse.Namespace) -> int:
    root = args.root.resolve()
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    cases_dir = out / "ordinary"
    cases_dir.mkdir(exist_ok=True)
    summary = {
        "scope": "hosted correctness-only source/work census; no timing or performance verdict",
        "commit": subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=root, text=True).strip(),
        "tree": subprocess.check_output(["git", "rev-parse", "HEAD^{tree}"], cwd=root, text=True).strip(),
        "baseline_compiler": {"path": args.base_compiler, "sha256": sha256(Path(args.base_compiler))},
        "probe_compiler": {"path": args.probe_compiler, "sha256": sha256(Path(args.probe_compiler))},
        "ordinary_inputs": [],
        "self_host": None,
    }
    failures = 0
    for source_arg in args.input:
        source = Path(source_arg)
        case_name = source.stem
        case = cases_dir / case_name
        case.mkdir(exist_ok=True)
        source_path = root / source
        row = {"source": str(source), "source_sha256": sha256(source_path)}
        objects: dict[str, str | None] = {}
        metrics: dict[str, dict[str, str]] = {}
        for label, compiler in (("baseline", args.base_compiler), ("probe", args.probe_compiler)):
            object_path = case / f"{label}.o"
            metrics_path = case / f"{label}.metrics"
            command = [
                compiler,
                "cc",
                "-g0",
                "-std=c11",
                "-c",
                str(source),
                f"-fsource-metrics={metrics_path}",
                "-o",
                str(object_path),
            ]
            run = run_command(command, root, case / f"{label}.stdout", case / f"{label}.stderr")
            row[label] = run
            objects[label] = sha256(object_path) if object_path.is_file() else None
            metrics[label] = parse_metrics(metrics_path)
            if label == "probe":
                row["work"] = summarize(case / "probe.stderr")
        row["object_sha256"] = objects
        row["objects_identical"] = bool(objects["baseline"]) and objects["baseline"] == objects["probe"]
        row["source_metrics_identical"] = metrics["baseline"] == metrics["probe"] and bool(metrics["probe"])
        row["source_metrics"] = metrics["probe"]
        row["status"] = "passed" if all(row[label]["exit"] == 0 for label in ("baseline", "probe")) and row["objects_identical"] and row["source_metrics_identical"] else "failed"
        failures += row["status"] != "passed"
        summary["ordinary_inputs"].append(row)

    self_dir = out / "self-host"
    self_dir.mkdir(exist_ok=True)
    stage1 = self_dir / "ide-stage1"
    stage2 = self_dir / "ide-stage2"
    stage1_metrics = self_dir / "stage1.metrics"
    stage2_metrics = self_dir / "stage2.metrics"
    common = [
        "cc",
        "-Isrc",
        "-Ibuild/generated",
        "-DBUSTER_UNITY_BUILD=1",
        "-DBUSTER_INCLUDE_TESTS=0",
        "-g",
        "-v",
    ]
    first = run_command(
        [args.probe_compiler, *common, f"-fsource-metrics={stage1_metrics}", str(SELF_HOST_SOURCE), "-lm", "-o", str(stage1)],
        root,
        self_dir / "stage1.stdout",
        self_dir / "stage1.stderr",
        timeout=args.self_host_timeout,
    )
    second: dict = {"exit": None, "argv": []}
    if first["exit"] == 0 and stage1.is_file():
        second = run_command(
            [str(stage1), *common, f"-fsource-metrics={stage2_metrics}", str(SELF_HOST_SOURCE), "-lm", "-o", str(stage2)],
            root,
            self_dir / "stage2.stdout",
            self_dir / "stage2.stderr",
            timeout=args.self_host_timeout,
        )
    stage1_hash = sha256(stage1) if stage1.is_file() else None
    stage2_hash = sha256(stage2) if stage2.is_file() else None
    stage1_source_metrics = parse_metrics(stage1_metrics)
    stage2_source_metrics = parse_metrics(stage2_metrics)
    self_result = {
        "source": str(SELF_HOST_SOURCE),
        "source_sha256": sha256(root / SELF_HOST_SOURCE),
        "stage1": first,
        "stage2": second,
        "stage1_binary_sha256": stage1_hash,
        "stage2_binary_sha256": stage2_hash,
        "binary_fixed_point": bool(stage1_hash) and stage1_hash == stage2_hash,
        "stage1_metrics": stage1_source_metrics,
        "stage2_metrics": stage2_source_metrics,
        "preprocessed_fixed_point": all(
            stage1_source_metrics.get(key) == stage2_source_metrics.get(key) and key in stage1_source_metrics
            for key in ("preprocessed.tokens", "preprocessed.bytes")
        ),
        "stage1_work": summarize(self_dir / "stage1.stderr"),
        "stage2_work": summarize(self_dir / "stage2.stderr"),
    }
    self_result["status"] = "passed" if first["exit"] == 0 and second.get("exit") == 0 and self_result["binary_fixed_point"] and self_result["preprocessed_fixed_point"] else "failed"
    summary["self_host"] = self_result
    failures += self_result["status"] != "passed"
    summary["failures"] = int(failures)
    summary["status"] = "passed" if failures == 0 else "failed"
    (out / "results.json").write_text(json.dumps(summary, indent=2) + "\n")
    compact = {
        "status": summary["status"],
        "ordinary_inputs": [
            {
                "source": row["source"],
                "status": row["status"],
                "calls": row.get("work", {}).get("calls", 0),
                "comparisons": row.get("work", {}).get("comparisons", 0),
                "queue_histogram": row.get("work", {}).get("queue_histogram", {}),
                "tokens": row.get("source_metrics", {}).get("preprocessed.tokens"),
            }
            for row in summary["ordinary_inputs"]
        ],
        "self_host": {
            "status": self_result["status"],
            "stage1_calls": self_result["stage1_work"]["calls"],
            "stage1_comparisons": self_result["stage1_work"]["comparisons"],
            "stage1_queue_histogram": self_result["stage1_work"]["queue_histogram"],
            "stage2_calls": self_result["stage2_work"]["calls"],
            "stage2_comparisons": self_result["stage2_work"]["comparisons"],
            "stage2_queue_histogram": self_result["stage2_work"]["queue_histogram"],
            "tokens": stage1_source_metrics.get("preprocessed.tokens"),
            "binary_fixed_point": self_result["binary_fixed_point"],
            "preprocessed_fixed_point": self_result["preprocessed_fixed_point"],
        },
        "failures": summary["failures"],
    }
    print("CENSUS_RESULT " + json.dumps(compact, sort_keys=True), flush=True)
    return int(failures != 0)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="command", required=True)
    instrument_parser = subparsers.add_parser("instrument")
    instrument_parser.add_argument("--root", type=Path, default=Path.cwd())
    instrument_parser.add_argument("--out", type=Path, required=True)
    run_parser = subparsers.add_parser("run")
    run_parser.add_argument("--root", type=Path, default=Path.cwd())
    run_parser.add_argument("--out", type=Path, required=True)
    run_parser.add_argument("--base-compiler", required=True)
    run_parser.add_argument("--probe-compiler", required=True)
    run_parser.add_argument("--self-host-timeout", type=int, default=900)
    run_parser.add_argument("--input", action="append", required=True)
    args = parser.parse_args()
    if args.command == "instrument":
        return instrument(args.root.resolve(), args.out.resolve())
    return run_census(args)


if __name__ == "__main__":
    sys.exit(main())
