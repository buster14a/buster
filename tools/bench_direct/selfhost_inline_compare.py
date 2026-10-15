#!/usr/bin/env python3
"""Self-built inline-mode base/head compiler comparison for issue #58.

The owner-authorized pull comparison builds the merge base and the head with
their tests-off Clang Release toolchains. Right after each role's build, while
that revision and its build/generated closure are checked out, `stage1`
compiles the role's own unity source once with SELFHOST_INLINE_PROFILE's fixed
stage-1 command, so each role yields the compiler it would self-build with
`-fcanonical-inline`. After the ordinary comparison, with the frozen merge-base
tree restored, `compare` times those two stage-1 compilers compiling that
tree with the same flag: fixed ABBA pairs, byte-identical outputs required
before timing, then the topdown and sampling profile steps for both. No
request-provided commands, flags, sources or pair counts are accepted.

Entry points: stage1 (one role's untimed stage-1 construction), compare (the
timed pair and profile steps), validate_compare_documents (raw uarch_lab
documents against the frozen profile), main.
"""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import subprocess
import sys
import time

from compiler_receipt import (SELFHOST_INLINE_PROFILE, SELFHOST_INLINE_SCHEMA, SELFHOST_INLINE_STAGE1_SCHEMA,
                              selfhost_inline_frontend_problems)
from inline_acceptance import git, hash_inputs, sha256

PROFILE = SELFHOST_INLINE_PROFILE
UARCH_SCHEMA = "buster-uarch-lab-compare-v2"
STAGE1_TIMEOUT_SECONDS = 15 * 60
COMPARE_TIMEOUT_SECONDS = 60 * 60
ROLES = ("baseline", "candidate")


def stage1(ide: Path, root: Path, revision: str, output: Path) -> dict:
    """Build one role's stage-1 compiler from its own checked-out revision; return its identity record."""
    if not ide.is_file() or not (root / "src/buster/apps/ide/ide.c").is_file() or not (root / "build/generated").is_dir():
        raise RuntimeError("role compiler or its generated unity inputs are missing")
    observed = git(root, "rev-parse", "HEAD")
    if observed != revision:
        raise RuntimeError(f"source checkout is {observed}, expected {revision}")
    if output.exists() and any(output.iterdir()):
        raise RuntimeError("output directory must be new or empty")
    output.mkdir(parents=True, exist_ok=True)
    compiler = output / "stage1.exe"
    frozen_before = hash_inputs(root)
    ide_sha = sha256(ide)
    started = time.monotonic()
    with (output / "stage1.log").open("wb") as log:
        status = subprocess.run([str(ide), *PROFILE["stage1_command"], "-o", str(compiler)], cwd=root, stdout=log,
                                stderr=subprocess.STDOUT, timeout=STAGE1_TIMEOUT_SECONDS).returncode
    elapsed = round(time.monotonic() - started, 3)
    if status != 0 or not compiler.is_file():
        raise RuntimeError(f"stage-1 construction exited {status}")
    if hash_inputs(root) != frozen_before or git(root, "rev-parse", "HEAD") != revision or sha256(ide) != ide_sha:
        raise RuntimeError("role inputs or compiler changed during stage-1 construction")
    return {"schema": SELFHOST_INLINE_STAGE1_SCHEMA, "status": "complete", "revision": revision,
            "compiler": {"sha256": ide_sha, "size_bytes": ide.stat().st_size},
            "stage1": {"sha256": sha256(compiler), "size_bytes": compiler.stat().st_size},
            "source_sha256": sha256(root / "src/buster/apps/ide/ide.c"), "frozen_input_tree": frozen_before,
            "command": list(PROFILE["stage1_command"]), "elapsed_seconds": elapsed}


def validate_compare_documents(summary: object, meta: object, labs: object, expected: dict) -> list[str]:
    """Reasons the raw uarch_lab documents are not one complete SELFHOST_INLINE_PROFILE run.

    expected maps baseline/candidate to the sha256 of the stage-1 compiler that role must have timed."""
    problems: list[str] = []
    if not isinstance(summary, dict) or not isinstance(meta, dict) or not isinstance(labs, dict):
        return ["self-host inline uarch evidence is not a set of JSON objects"]
    plan = summary.get("plan") if isinstance(summary.get("plan"), dict) else {}
    if summary.get("schema") != UARCH_SCHEMA or plan.get("pairs") != PROFILE["pairs"] or \
            plan.get("complete_pairs") != PROFILE["pairs"] or plan.get("order") != PROFILE["order"] or \
            plan.get("fresh_copy") is not True:
        problems.append("self-host inline comparison is incomplete or not the fixed ABBA plan")
    for role in ROLES:
        info = summary.get(role) if isinstance(summary.get(role), dict) else {}
        if info.get("failed") or not info.get("deterministic") or info.get("runs") != PROFILE["pairs"]:
            problems.append(f"self-host inline {role} did not produce {PROFILE['pairs']} deterministic timed runs")
        if info.get("sha256") != expected.get(role):
            problems.append(f"self-host inline timed {role} is not its recorded stage-1 compiler")
    config = meta.get("config") if isinstance(meta.get("config"), dict) else {}
    flags = list(PROFILE["compile_flags"])
    if config.get("extra") != flags or config.get("extra_by_variant") != {"a": flags, "b": flags} or \
            config.get("canonical_inline_pair") is not False or config.get("pairs") != PROFILE["pairs"] or \
            config.get("warmups") != PROFILE["warmups"] or config.get("cpu") != PROFILE["cpu"] or \
            config.get("profile_steps") != PROFILE["profile_steps"] or config.get("require_identical_output") is not True or \
            config.get("min_effect_percent") != PROFILE["min_effect_percent"] or config.get("fresh_copy") is not True:
        problems.append("self-host inline run did not use the fixed profile configuration")
    for key in ("a", "b"):
        lab = labs.get(key) if isinstance(labs.get(key), dict) else {}
        lab_config = lab.get("config") if isinstance(lab.get("config"), dict) else {}
        if lab_config.get("extra") != flags or lab_config.get("cpu") != PROFILE["cpu"]:
            problems.append(f"self-host inline {key} compile flags or CPU pin do not match the fixed profile")
    if meta.get("outputs_identical") is not True:
        problems.append("self-host inline stage-1 compilers did not produce byte-identical outputs")
    steps = meta.get("steps") if isinstance(meta.get("steps"), dict) else {}
    for step in ("prepare", "timed", *PROFILE["profile_steps"]):
        state = steps.get(step) if isinstance(steps.get(step), dict) else {}
        if state.get("status") != "ok":
            problems.append(f"self-host inline {step} step did not complete")
    metrics = summary.get("metrics") if isinstance(summary.get("metrics"), dict) else {}
    wall = metrics.get("wall") if isinstance(metrics.get("wall"), dict) else {}
    if not all(_positive(wall.get(key)) for key in ("a_median", "b_median", "ratio", "ci_low", "ci_high")):
        problems.append("self-host inline wall-time measurements are missing")
    counters = summary.get("counters") if isinstance(summary.get("counters"), dict) else {}
    if counters.get("perf_stat") is not True:
        problems.append("self-host inline hardware counters were unavailable; its PMU evidence is required")
    for metric in ("instructions", "cycles", "branch_misses"):
        row = metrics.get(metric) if isinstance(metrics.get(metric), dict) else {}
        if not all(_positive(row.get(key)) for key in ("a_median", "b_median", "ratio")):
            problems.append(f"self-host inline {metric} counts are missing")
    problems.extend(selfhost_inline_frontend_problems(summary))
    return problems


def _positive(value: object) -> bool:
    return type(value) in (int, float) and value == value and 0 < value < float("inf")


def compare(lab: Path, stage1_records: dict, root: Path, revision: str, output: Path, perf: str) -> dict:
    """Time the two stage-1 compilers on the frozen merge-base tree; return the profile result."""
    compilers = {role: Path(stage1_records[role]["path"]) for role in ROLES}
    if not lab.is_file() or any(not path.is_file() for path in compilers.values()):
        raise RuntimeError("trusted lab or a stage-1 compiler is missing")
    expected = {role: stage1_records[role]["sha256"] for role in ROLES}
    if any(sha256(compilers[role]) != expected[role] for role in ROLES):
        raise RuntimeError("a stage-1 compiler differs from its recorded identity")
    observed = git(root, "rev-parse", "HEAD")
    if observed != revision:
        raise RuntimeError(f"frozen tree is {observed}, expected merge base {revision}")
    if output.exists() and any(output.iterdir()):
        raise RuntimeError("output directory must be new or empty")
    frozen_before = hash_inputs(root)
    started = time.monotonic()
    argv = [sys.executable, "-B", str(lab), "compare", "--baseline", str(compilers["baseline"]),
            "--candidate", str(compilers["candidate"]), "--repo-root", str(root), "--cpu", str(PROFILE["cpu"]),
            "--output", str(output), "--pairs", str(PROFILE["pairs"]), "--warmups", str(PROFILE["warmups"]),
            "--perf", perf, "--profile-steps", ",".join(PROFILE["profile_steps"]), "--require-identical-output",
            "--min-effect", str(PROFILE["min_effect_percent"]), "--", *PROFILE["compile_flags"]]
    status = subprocess.run(argv, timeout=COMPARE_TIMEOUT_SECONDS).returncode
    if hash_inputs(root) != frozen_before or git(root, "rev-parse", "HEAD") != revision:
        raise RuntimeError("frozen merge-base inputs changed during the self-host inline comparison")
    if any(sha256(compilers[role]) != expected[role] for role in ROLES):
        raise RuntimeError("a stage-1 compiler changed during the self-host inline comparison")
    try:
        summary = json.loads((output / "summary.json").read_text(encoding="utf-8"))
        meta = json.loads((output / "compare.json").read_text(encoding="utf-8"))
        labs = {key: json.loads((output / key / "lab.json").read_text(encoding="utf-8")) for key in ("a", "b")}
    except (OSError, ValueError) as error:
        raise RuntimeError(f"uarch evidence unreadable (lab exited {status}): {error}") from error
    problems = validate_compare_documents(summary, meta, labs, expected)
    if status != 0:
        problems.insert(0, f"uarch_lab compare exited {status}")
    if problems:
        raise RuntimeError("; ".join(problems))
    return {"schema": SELFHOST_INLINE_SCHEMA, "status": "complete", "profile": PROFILE["name"],
            "base_revision": revision, "frozen_input_tree": frozen_before,
            "stage1_compilers": {role: {"sha256": expected[role]} for role in ROLES},
            "elapsed_seconds": round(time.monotonic() - started, 3)}


def write_json(path: Path, value: dict) -> None:
    temporary = path.with_name(path.name + ".tmp")
    temporary.write_text(json.dumps(value, sort_keys=True, indent=2) + "\n", encoding="utf-8")
    os.replace(temporary, path)


def parse(argv=None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    commands = parser.add_subparsers(dest="command", required=True)
    first = commands.add_parser("stage1", help="build one role's stage-1 compiler from its checked-out revision")
    first.add_argument("--ide", type=Path, required=True, help="the role's Clang Release ide")
    first.add_argument("--repo-root", type=Path, required=True, help="the role's checkout with generated headers")
    first.add_argument("--revision", required=True)
    first.add_argument("--output", type=Path, required=True)
    timed = commands.add_parser("compare", help="time both stage-1 compilers on the frozen merge-base tree")
    timed.add_argument("--lab", type=Path, required=True, help="trusted uarch_lab.py")
    for role in ROLES:
        timed.add_argument(f"--{role}-stage1", type=Path, required=True, help=f"the {role} stage1.json")
    timed.add_argument("--repo-root", type=Path, required=True, help="the frozen merge-base tree")
    timed.add_argument("--base-revision", required=True)
    timed.add_argument("--perf", default="perf")
    timed.add_argument("--output", type=Path, required=True)
    commands.add_parser("profile", help="print the frozen profile")
    return parser.parse_args(argv)


def main(argv=None) -> int:
    args = parse(argv)
    status = 0
    if args.command == "profile":
        print(json.dumps(PROFILE, sort_keys=True, indent=2))
    else:
        try:
            if args.command == "stage1":
                output = args.output.resolve()
                record = stage1(args.ide.resolve(), args.repo_root.resolve(), args.revision, output)
                write_json(output / "stage1.json", record)
            else:
                records = {}
                for role in ROLES:
                    path = getattr(args, f"{role}_stage1").resolve()
                    record = json.loads(path.read_text(encoding="utf-8"))
                    if not isinstance(record, dict) or record.get("schema") != SELFHOST_INLINE_STAGE1_SCHEMA or \
                            record.get("status") != "complete":
                        raise RuntimeError(f"{role} stage-1 record is incomplete")
                    records[role] = {"path": path.parent / "stage1.exe", "sha256": record["stage1"]["sha256"]}
                output = args.output.resolve()
                result = compare(args.lab.resolve(), records, args.repo_root.resolve(), args.base_revision,
                                 output / "lab", args.perf)
                write_json(output / "result.json", result)
        except (OSError, ValueError, KeyError, TypeError, RuntimeError, subprocess.SubprocessError) as error:
            print(f"selfhost_inline_compare: {error}", file=sys.stderr)
            status = 1
    if status == 0 and args.command != "profile":
        print(f"selfhost_inline_compare: {args.command} complete")
    return status


if __name__ == "__main__":
    raise SystemExit(main())
