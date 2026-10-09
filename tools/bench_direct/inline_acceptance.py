#!/usr/bin/env python3
"""Bounded same-candidate self-hosting inliner comparison for issue #48.

The owner-authorized compiler comparison has already built candidate HEAD with
its tests-off Clang Release toolchain. While that checkout and its
build/generated closure remain at HEAD, this profile freezes the source/header
tree digests and runs two fixed ABBA comparisons on that one tree: first to
create stage-1 off/on compilers, then to time each generated compiler compiling
the same candidate source with the same mode. No request-provided commands,
compiler flags, source paths or pair counts are accepted.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import stat
import subprocess
import sys
import time

from compiler_receipt import INLINE_ACCEPTANCE_PROFILE

PAIRS = INLINE_ACCEPTANCE_PROFILE["pairs"]
WARMUPS = INLINE_ACCEPTANCE_PROFILE["warmups"]
SCHEMA = "buster-inline-self-host-acceptance-v1"
UARCH_SCHEMA = "buster-uarch-lab-compare-v2"
TIMEOUT_SECONDS = 3 * 60 * 60
MAX_TREE_FILES = 32768
MAX_TREE_BYTES = 2 * 1024 * 1024 * 1024


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1 << 20), b""):
            digest.update(block)
    return digest.hexdigest()


def hash_tree(root: Path) -> dict:
    """Hash a bounded regular-file tree; refuse symlinks and special files."""
    digest = hashlib.sha256()
    count = total = 0

    def fail(error):
        raise RuntimeError(f"cannot read frozen input tree: {error}")

    for directory, names, leaves in os.walk(root, topdown=True, followlinks=False, onerror=fail):
        names.sort()
        leaves.sort()
        for name in list(names):
            path = Path(directory) / name
            mode = path.lstat().st_mode
            if not stat.S_ISDIR(mode):
                raise RuntimeError(f"non-directory or symlink in frozen inputs: {path.relative_to(root)}")
        for name in leaves:
            path = Path(directory) / name
            status = path.lstat()
            if not stat.S_ISREG(status.st_mode):
                raise RuntimeError(f"non-regular file in frozen inputs: {path.relative_to(root)}")
            count += 1
            total += status.st_size
            if count > MAX_TREE_FILES or total > MAX_TREE_BYTES:
                raise RuntimeError("frozen source/header tree exceeds its file or byte bound")
            relative = path.relative_to(root).as_posix().encode("utf-8")
            digest.update(len(relative).to_bytes(8, "big"))
            digest.update(relative)
            with path.open("rb") as stream:
                for block in iter(lambda: stream.read(1 << 20), b""):
                    digest.update(block)
    return {"sha256": digest.hexdigest(), "files": count, "bytes": total}


def hash_inputs(root: Path) -> dict:
    return {"src": hash_tree(root / "src"), "build/generated": hash_tree(root / "build/generated")}


def git(root: Path, *args: str) -> str:
    result = subprocess.run(["git", "-C", str(root), *args], check=True, capture_output=True, text=True, timeout=30)
    return result.stdout.strip()


def run_compare(lab: Path, compiler_a: Path, compiler_b: Path, root: Path, cpu: int, output: Path, perf: str) -> None:
    argv = [sys.executable, "-B", str(lab), "compare",
            "--baseline", str(compiler_a), "--candidate", str(compiler_b),
            "--repo-root", str(root), "--cpu", str(cpu), "--output", str(output),
            "--pairs", str(PAIRS), "--warmups", str(WARMUPS), "--perf", perf,
            "--canonical-inline-pair"]
    subprocess.run(argv, check=True, timeout=TIMEOUT_SECONDS)


def _positive_number(value) -> bool:
    return isinstance(value, (int, float)) and not isinstance(value, bool) and math.isfinite(value) and value > 0


def validate_documents(summary: dict, meta: dict, labs: dict, expected_baseline_sha: str,
                        expected_candidate_sha: str) -> dict:
    """Validate raw bounded publisher documents without trusting the host wrapper."""
    if not isinstance(summary, dict) or not isinstance(meta, dict) or not isinstance(labs, dict):
        raise RuntimeError("uarch evidence is not a set of JSON objects")
    plan = summary.get("plan") or {}
    if summary.get("schema") != UARCH_SCHEMA or plan.get("pairs") != PAIRS or plan.get("complete_pairs") != PAIRS or plan.get("order") != "ABBA" or plan.get("fresh_copy") is not True:
        raise RuntimeError("uarch comparison incomplete or wrong schema")
    expected_hashes = {"baseline": expected_baseline_sha, "candidate": expected_candidate_sha}
    for role, expected_sha in expected_hashes.items():
        info = summary.get(role) or {}
        if info.get("failed") or not info.get("deterministic") or info.get("runs") != PAIRS:
            raise RuntimeError(f"{role} did not produce {PAIRS} deterministic timed runs")
        if info.get("sha256") != expected_sha:
            raise RuntimeError(f"timed {role} binary identity differs from its frozen input")
    config = meta.get("config") or {}
    expected_extras = {"a": [], "b": ["-fcanonical-inline"]}
    if (config.get("canonical_inline_pair") is not True or config.get("extra_by_variant") != expected_extras or
            config.get("pairs") != PAIRS or config.get("warmups") != WARMUPS or config.get("cpu") != 2 or
            config.get("extra") != []):
        raise RuntimeError("uarch run did not use the fixed canonical-inline A/B profile")
    for key, expected in expected_extras.items():
        lab_config = (labs.get(key) or {}).get("config") or {}
        if lab_config.get("extra") != expected or lab_config.get("cpu") != 2:
            raise RuntimeError(f"uarch {key} compile flags or CPU pin do not match the fixed profile")
    values = summary.get("metrics") or {}
    for key in ("wall", "instructions", "peak_rss"):
        if not isinstance(values.get(key), dict):
            raise RuntimeError(f"uarch metric {key} missing")
    wall = values["wall"]
    if not all(_positive_number(wall.get(key)) for key in ("a_median", "b_median", "ratio")):
        raise RuntimeError("uarch wall-time measurements are missing")
    code = summary.get("code_bytes") or {}
    if not all(_positive_number(code.get(key)) for key in ("a_value", "b_value", "ratio")):
        raise RuntimeError("executable-section code-byte measurements are missing")
    counters = summary.get("counters") or {}
    instruction = values["instructions"]
    instruction_ratio = instruction.get("ratio")
    if instruction_ratio is None:
        if counters.get("perf_stat") is not False or not counters.get("reason"):
            raise RuntimeError("instruction counts are NA without a recorded unavailable-counter reason")
    elif not _positive_number(instruction_ratio):
        raise RuntimeError("instruction-count ratio is invalid")
    elif not all(_positive_number(instruction.get(key)) for key in ("a_median", "b_median")):
        raise RuntimeError("instruction medians are missing")
    return summary


def load_complete(path: Path, expected_baseline_sha: str, expected_candidate_sha: str) -> dict:
    """Read local profile documents, then apply the shared publisher validator."""
    try:
        summary = json.loads((path / "summary.json").read_text(encoding="utf-8"))
        meta = json.loads((path / "compare.json").read_text(encoding="utf-8"))
        labs = {key: json.loads((path / key / "lab.json").read_text(encoding="utf-8")) for key in ("a", "b")}
    except (OSError, ValueError) as error:
        raise RuntimeError(f"uarch evidence unreadable at {path}: {error}") from error
    try:
        return validate_documents(summary, meta, labs, expected_baseline_sha, expected_candidate_sha)
    except (RuntimeError, AttributeError, TypeError, KeyError) as error:
        raise RuntimeError(f"invalid or incomplete uarch evidence at {path}: {error}") from error


def metrics(summary: dict) -> dict:
    values = summary["metrics"]
    return {"wall": values["wall"], "instructions": values["instructions"],
            "peak_rss": values["peak_rss"], "code_bytes": summary["code_bytes"],
            "counter_availability": summary.get("counters")}


def execute(args: argparse.Namespace) -> dict:
    lab, root, candidate = args.lab.resolve(), args.repo_root.resolve(), args.candidate_ide.resolve()
    output = args.output.resolve()
    source = root / "src/buster/apps/ide/ide.c"
    generated = root / "build/generated"
    if not lab.is_file() or not candidate.is_file() or not source.is_file() or not generated.is_dir():
        raise RuntimeError("trusted lab, candidate compiler, or generated candidate-HEAD unity inputs are missing")
    observed = git(root, "rev-parse", "HEAD")
    if observed != args.head_revision:
        raise RuntimeError(f"source checkout is {observed}, expected candidate HEAD {args.head_revision}")
    if output.exists() and any(output.iterdir()):
        raise RuntimeError("output directory must be new or empty")
    output.mkdir(parents=True, exist_ok=True)
    candidate_sha = sha256(candidate)
    frozen_before = hash_inputs(root)
    started = time.monotonic()
    stage1 = output / "stage1"
    stage2 = output / "selfhost"
    run_compare(lab, candidate, candidate, root, args.cpu, stage1, args.perf)
    first = load_complete(stage1, candidate_sha, candidate_sha)
    off1, on1 = stage1 / "a/reference.exe", stage1 / "b/reference.exe"
    if not off1.is_file() or not on1.is_file():
        raise RuntimeError("stage-1 compiler outputs are missing")
    identities = {"off": {"sha256": sha256(off1), "size_bytes": off1.stat().st_size},
                  "on": {"sha256": sha256(on1), "size_bytes": on1.stat().st_size}}
    (stage1 / "identities.json").write_text(json.dumps(
        {"off": identities["off"], "on": identities["on"]}, sort_keys=True, indent=2) + "\n", encoding="utf-8")
    run_compare(lab, off1, on1, root, args.cpu, stage2, args.perf)
    second = load_complete(stage2, identities["off"]["sha256"], identities["on"]["sha256"])
    frozen_after = hash_inputs(root)
    if frozen_after != frozen_before or git(root, "rev-parse", "HEAD") != args.head_revision:
        raise RuntimeError("candidate source/generated-header inputs changed during the inliner profile")
    if sha256(candidate) != candidate_sha:
        raise RuntimeError("candidate compiler binary changed during the inliner profile")
    off2, on2 = stage2 / "a/reference.exe", stage2 / "b/reference.exe"
    stage2_identities = {
        "off": {"sha256": sha256(off2), "size_bytes": off2.stat().st_size} if off2.is_file() else None,
        "on": {"sha256": sha256(on2), "size_bytes": on2.stat().st_size} if on2.is_file() else None}
    (stage2 / "identities.json").write_text(json.dumps(
        stage2_identities, sort_keys=True, indent=2) + "\n", encoding="utf-8")
    fixed = {"off": stage2_identities["off"] == identities["off"],
             "on": stage2_identities["on"] == identities["on"]}
    if not all(fixed.values()):
        raise RuntimeError("generated stage-2 compiler did not reproduce its own stage-1 bytes under the same mode")
    profile = dict(INLINE_ACCEPTANCE_PROFILE)
    profile.update(source_sha256=sha256(source), cpu=args.cpu)
    return {"schema": SCHEMA, "status": "complete", "profile": profile,
            "source_revision": observed, "frozen_input_tree": frozen_before,
            "candidate_compiler": {"sha256": candidate_sha, "size_bytes": candidate.stat().st_size},
            "stage1_compilers": identities, "fixed_point": fixed,
            "stage1": {"metrics": metrics(first), "outputs_identical": first.get("outputs_identical")},
            "selfhost_runtime": {"metrics": metrics(second), "outputs_identical": second.get("outputs_identical")},
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
        print(f"inline_acceptance: {error}", file=sys.stderr)
        return 1
    print("inline_acceptance: complete")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
