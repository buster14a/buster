#!/usr/bin/env python3
"""Verify the retained matched census from the failed #2628 proof run."""

import hashlib
import re
import subprocess
import sys
import zipfile
from pathlib import Path


RUN = 37690908702
ARTIFACT = 11515676185
ZIP_SHA = "cf00a4c2e5d4447c04765839047f96247d3201fc4799064a725beb76b2c84eb2"
BASELINE = "cc2f84737b30e4115b0663742f80803c8267ef25"
CANDIDATE = "7dddf4ff7e213d8d8509d2adc125465b8ddd66cd"
CANDIDATE_TREE = "30cc608b9f634179813962f38012d9d78565c96e"
STAGE1_SHA = "1e969810ed757aeb48ba08c4af941184afb99917624564baafc28bd8ca1645b0"
WORK_PREFIX = "ir_construction."
OWNERSHIP = {
    "validation_ownership_function_scans",
    "validation_ownership_functions",
    "validation_ownership_blocks",
    "validation_ownership_instructions",
    "validation_ownership_bytes_cleared",
}
POPULATION = (
    "validation_calls",
    "validation_functions",
    "validation_blocks",
    "validation_values",
    "validation_instructions",
)


def require(condition, message):
    if not condition:
        raise ValueError(message)


def digest(data):
    return hashlib.sha256(data).hexdigest()


def git(checkout, *args):
    return subprocess.check_output(["git", "-C", str(checkout), *args]).decode().strip()


def receipt(zf, name, member):
    line = zf.read(name).decode().strip()
    match = re.fullmatch(r"([0-9a-f]{64})  (.+)", line)
    require(match is not None, f"invalid receipt {name}")
    require(Path(match.group(2)).name == Path(member).name, f"receipt member mismatch {name}")
    require(digest(zf.read(member)) == match.group(1), f"binary digest mismatch {member}")
    return match.group(1)


def source_manifest(zf, name, checkout, revision):
    checkout = checkout.resolve()
    require(git(checkout, "rev-parse", "HEAD") == revision, f"wrong checkout {checkout}")
    tracked = set(subprocess.check_output(["git", "-C", str(checkout), "ls-files", "-z"]).decode().strip("\0").split("\0"))
    rows = zf.read(name).decode().splitlines()
    paths = set()
    for row in rows:
        match = re.fullmatch(r"([0-9a-f]{64})  (.+)", row)
        require(match is not None, f"invalid source manifest row in {name}")
        path = match.group(2)
        require(path not in paths and path in tracked, f"unexpected source path {path}")
        paths.add(path)
        file = (checkout / path).resolve()
        require(file.is_relative_to(checkout) and file.is_file(), f"unsafe or missing source path {path}")
        require(digest(file.read_bytes()) == match.group(1), f"source digest mismatch {path}")
    require(paths == tracked and len(paths) > 0, f"incomplete source manifest {name}")
    return len(paths)


def work_counters(zf, name):
    result = {}
    for line in zf.read(name).decode().splitlines():
        if not line.startswith((WORK_PREFIX + "validation", WORK_PREFIX + "preparation")):
            continue
        key, separator, value = line.partition("=")
        require(separator and key not in result and re.fullmatch(r"[0-9]+", value), f"invalid counter {key} in {name}")
        result[key.removeprefix(WORK_PREFIX)] = int(value)
    return result


def verify(zip_path, baseline_checkout, candidate_checkout, report_path):
    require(digest(zip_path.read_bytes()) == ZIP_SHA, "artifact ZIP digest mismatch")
    with zipfile.ZipFile(zip_path) as zf:
        baseline_files = source_manifest(zf, "baseline/source.sha256", baseline_checkout, BASELINE)
        candidate_files = source_manifest(zf, "candidate/source.sha256", candidate_checkout, CANDIDATE)
        candidate_revision = [CANDIDATE, CANDIDATE_TREE]
        require(zf.read("candidate/revisions.txt").decode().splitlines() == candidate_revision, "candidate revision mismatch")
        require(zf.read("census/input-revisions.txt").decode().splitlines() == candidate_revision, "census input revision mismatch")
        require(git(candidate_checkout, "rev-parse", "HEAD^{tree}") == CANDIDATE_TREE, "candidate checkout tree mismatch")
        require(zf.read("census/input-root.txt").decode().strip(), "missing frozen input root")

        binaries = [
            ("census/baseline-compiler.sha256", "binaries/baseline-census-ide"),
            ("census/candidate-compiler.sha256", "binaries/candidate-census-ide"),
        ]
        binary_shas = [receipt(zf, name, member) for name, member in binaries]
        output_shas = [
            re.fullmatch(r"([0-9a-f]{64})  .+", zf.read(name).decode().strip())
            for name in ("census/baseline-stage1.sha256", "census/candidate-stage1.sha256")
        ]
        require(all(output_shas), "missing stage-1 output digest")
        require(all(match.group(1) == STAGE1_SHA for match in output_shas), "stage-1 output digest mismatch")

        generated = zf.read("census/input-generated.sha256").decode().splitlines()
        require(generated and all(re.fullmatch(r"[0-9a-f]{64}  build/generated/.+", row) for row in generated),
                "invalid generated-input manifest")
        candidate_log = zf.read("census/candidate-build.log").decode()
        baseline_log = zf.read("census/baseline-build.log").decode()
        for label, log in (("baseline", baseline_log), ("candidate", candidate_log)):
            require("BUSTER_INCLUDE_TESTS:BOOL=ON" in log and "BUSTER_BENCH_ALLOCATIONS:BOOL=ON" in log,
                    f"{label} census configuration missing")
        for row in generated:
            path = row.split("  ", 1)[1]
            require(candidate_log.count(path + ": OK") >= 3, f"generated input not checked around both compiles: {path}")
        require(re.search(r"module=ir_tests .*passed=502658 failed=0 assertions=502658 status=pass",
                          zf.read("census/ir-tests.log").decode()), "diagnostic IR tests did not pass")

        base = work_counters(zf, "census/baseline-stage1.metrics")
        cand = work_counters(zf, "census/candidate-stage1.metrics")
        require(base.keys() == cand.keys() and len(base) == 47, "incomplete or unmatched census key set")
        require(all(base[key] == cand[key] > 0 for key in POPULATION), "unmatched or empty validation population")
        changed = {key for key in base if base[key] != cand[key]}
        require(changed == OWNERSHIP, f"unexpected changed counters: {sorted(changed ^ OWNERSHIP)}")
        require(all(base[key] > cand[key] for key in OWNERSHIP), "ownership work failed to decrease")
        require(base["validation_ownership_bytes_cleared"] == 4 * base["validation_ownership_instructions"],
                "baseline ownership clear formula mismatch")
        require(cand["validation_ownership_bytes_cleared"] ==
                cand["validation_instructions"] + cand["validation_values"],
                "candidate ownership clear formula mismatch")
        for ownership, semantic in (
            ("validation_ownership_function_scans", "validation_functions"),
            ("validation_ownership_functions", "validation_functions"),
            ("validation_ownership_blocks", "validation_blocks"),
            ("validation_ownership_instructions", "validation_instructions"),
        ):
            require(cand[ownership] == cand[semantic], f"candidate ownership relationship mismatch: {ownership}")

    report = [
        "# Retained matched validator census verification",
        "",
        f"Original run {RUN} at c479d3169 is **failed** because its in-run guard incorrectly required ownership work counters to remain equal. This independent verification checks its immutable artifact {ARTIFACT}; it does not change that run's result.",
        "",
        f"- Artifact ZIP SHA-256: `{ZIP_SHA}`.",
        f"- Baseline compiler source: `{BASELINE}` ({baseline_files} tracked files rehashed).",
        f"- Candidate compiler and frozen input source: `{CANDIDATE}`, tree `{CANDIDATE_TREE}` ({candidate_files} tracked files rehashed).",
        f"- Both frozen compiler binaries match their retained digest receipts: `{binary_shas[0]}`, `{binary_shas[1]}`.",
        f"- Both stage-1 output digest receipts equal `{STAGE1_SHA}`; original run's strict byte comparison and 502,658 diagnostic IR assertions completed before its guard failed.",
        f"- Both compilers were built with tests and allocation counters on; the frozen generated-input manifest has {len(generated)} file(s) and the original log records its checks around both compiles.",
        f"- All {len(base)} validation/preparation counter keys are present. Every semantic/preparation counter matches, with positive validation calls, functions, blocks, values, and instructions. Exactly five expected ownership-work counters decrease.",
        "",
        "| ownership work counter | baseline | candidate |",
        "| --- | ---: | ---: |",
    ]
    for key in sorted(OWNERSHIP):
        report.append(f"| `{key}` | {base[key]:,} | {cand[key]:,} |")
    report += [
        "",
        "The baseline clear count is four bytes per baseline ownership instruction. The fused candidate clears one byte per validated instruction plus one per validated value; its ownership scans, functions, blocks and instructions equal the semantic populations. Hosted timing and dedicated Ryzen 9700X performance are outside this verifier's scope.",
        "",
    ]
    report_path.parent.mkdir(parents=True, exist_ok=True)
    report_path.write_text("\n".join(report))


if __name__ == "__main__":
    try:
        verify(Path(sys.argv[1]), Path(sys.argv[2]), Path(sys.argv[3]), Path(sys.argv[4]))
    except (OSError, ValueError, IndexError, KeyError, zipfile.BadZipFile) as error:
        sys.exit(f"retained census verification failed: {error}")
