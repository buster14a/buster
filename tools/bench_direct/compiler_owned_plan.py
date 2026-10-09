#!/usr/bin/env python3
"""Replay fixed ordinary snapshot and explicitly admitted Utility plans as data.

The caller validates raw owned-phase records and supplies ownership fields pinned
by the trusted producer/publisher route. This helper binds those fields to the
full core/extension argv, cwd, timeout and ordering. It makes no scientific claim.
"""
from __future__ import annotations

import hashlib
import re
from compiler_receipt import PROFILE, THROUGHPUT_PROFILE, SCALING_PROFILE, INLINE_ACCEPTANCE_PROFILE

HASH = re.compile(r"[a-f0-9]{64}\Z")
COMMIT = re.compile(r"[a-f0-9]{40}\Z")
PATH_LIMIT = 4096
CORE_LIMIT = 32
GIT_TIMEOUT = 120
BUILD_TIMEOUT = 1800
LAB_TIMEOUT = 3000
CORPUS_TIMEOUT = 1800
SCALING_TIMEOUT = 1200
INLINE_TIMEOUT = 10800
POPULATION_SCHEMA = "buster-compiler-snapshot-phases-v1"
UTILITY_POPULATION_SCHEMA = "buster-compiler-utility-phases-v1"


def _same(value: object, wanted: object) -> bool:
    """Frozen JSON equality also rejects bool/int and extra-key substitutions."""
    if type(value) is not type(wanted):
        return False
    if isinstance(wanted, dict):
        return set(value) == set(wanted) and all(_same(value[key], item) for key, item in wanted.items())
    if isinstance(wanted, list):
        return len(value) == len(wanted) and all(_same(left, right) for left, right in zip(value, wanted))
    return value == wanted


def _path(value: object) -> bool:
    return isinstance(value, str) and 1 < len(value.encode()) <= PATH_LIMIT and value.startswith("/") and \
        all(part not in ("", ".", "..") for part in value[1:].split("/")) and \
        all(ord(char) >= 32 and ord(char) != 127 for char in value)


def _overlap(first: str, second: str) -> bool:
    return first == second or first.startswith(second + "/") or second.startswith(first + "/")


def _digest(value: object) -> bool:
    return isinstance(value, str) and HASH.fullmatch(value) is not None


def _commands(receipt: dict, ownership: dict, *, utility: bool = False) -> list[dict]:
    identity = receipt["identity"]
    root, trusted = ownership["candidate_root"], ownership["trusted_root"]
    work, evidence, bins = ownership["work_root"], ownership["evidence_root"], ownership["binaries_root"]
    driver, python, lab = ownership["driver_path"], ownership["python_path"], ownership["lab_path"]
    base, head, tree = identity["base"], identity["head"], identity["base_tree"]
    legacy = utility and receipt["preparation_policy"] == "legacy-rebuild"
    manifest = None if legacy else receipt["closure"]["snapshot"]["manifest_sha256"]
    native_harness = root + "/build/throughput-tools/throughput"
    git = ["git", "-c", "gc.auto=0", "-c", "maintenance.auto=false", "-c", "core.hooksPath=/dev/null", "-C", root]
    rows = []

    def add(phase, argv, timeout, cwd=root):
        rows.append({"phase": phase, "kind": "run", "allow_exit_failure": False,
                     "argv": argv, "cwd": cwd, "timeout": timeout})

    def build(role, revision):
        add("build-" + role, [*git, "checkout", "--quiet", "--detach", revision], GIT_TIMEOUT)
        add("build-" + role, ["./build.sh", "generate", "--cc", "clang", "--no-include-tests"], BUILD_TIMEOUT)
        add("build-" + role, ["./build.sh", "build", "--config", "Release", "-t", "ide"], BUILD_TIMEOUT)

    def closure(phase, operation):
        add(phase, [driver, "compiler_closure", operation, root, work + "/frozen-baseline", base, tree,
                    evidence + "/closure-" + operation + ".json", "-" if operation == "snapshot" else manifest],
            BUILD_TIMEOUT, trusted)

    build("baseline", base)
    if not legacy:
        closure("closure-snapshot", "snapshot")
    build("candidate", head)
    if receipt["inline_acceptance"]["requested"]:
        add("inline-acceptance", [python, "-B", trusted + "/tools/bench_direct/inline_acceptance.py",
            "--lab", lab, "--candidate-ide", bins + "/ide-cand", "--repo-root", root, "--head-revision", head,
            "--cpu", str(PROFILE["cpu"]), "--output", work + "/inline-acceptance"], INLINE_TIMEOUT)
    if legacy:
        build("closure", base)
    else:
        add("build-closure", [*git, "checkout", "--quiet", "--detach", base], GIT_TIMEOUT)
        closure("build-closure", "restore")
    add("lab", [python, "-B", lab, "compare", "--baseline", bins + "/ide-base", "--candidate", bins + "/ide-cand",
        "--repo-root", root, "--cpu", str(PROFILE["cpu"]), "--output", work + "/lab",
        "--target-minutes", str(PROFILE["target_minutes"]), "--warmups", str(PROFILE["warmups"])], LAB_TIMEOUT)
    corpus_prefix = ["./build.sh", "bench_throughput"] if legacy else [native_harness]
    add("throughput", [*corpus_prefix, "run", "--baseline", bins + "/ide-base", "--candidate", bins + "/ide-cand",
        "--output", work + "/throughput", "--baseline-id", base, "--candidate-id", head,
        *THROUGHPUT_PROFILE["arguments"]], CORPUS_TIMEOUT)
    rows[-1]["exit_policy"] = "corpus-report-only-v1"
    if receipt.get("scaling_profile") is not None:
        for name, arguments in SCALING_PROFILE["series"].items():
            add("scaling", [native_harness, "scale", "--compiler", bins + "/ide-cand",
                "--output", work + "/scaling/" + name, *arguments], SCALING_TIMEOUT)
    if not legacy:
        closure("validate", "verify")
    return rows


def validate_plan(receipt: object, ownership: object, core_rows: object, *,
                  expected_phase_schema: str = POPULATION_SCHEMA) -> list[str]:
    """Require the admitted full recipe; captured probes are checked elsewhere.

    ownership carries trusted_root/candidate_root/work_root/evidence_root,
    binaries_root, directory, exact emitted python_path, canonical lab_path,
    driver_path and the pinned bootstrap_marker_sha256. External admission binds
    these fields and trusted revision/driver identities to its committed route.
    Utility admission requires the trusted caller to pass UTILITY_POPULATION_SCHEMA.
    Its receipt must declare that exact schema, main mode and one original policy;
    a receipt selfclaim cannot opt into the legacy route. Default snapshot recipes
    and historical legacy receipts remain governed by their existing callers.
    Python argv spelling is preserved: /usr/bin/python3 need not resolve to the
    same spelling. Only core rows of kind=run are accepted here.
    """
    try:
        if expected_phase_schema not in (POPULATION_SCHEMA, UTILITY_POPULATION_SCHEMA):
            return ["ordinary native semantic plan expected phase schema unsupported"]
        utility = expected_phase_schema == UTILITY_POPULATION_SCHEMA
        if not isinstance(receipt, dict) or not isinstance(ownership, dict) or \
                not isinstance(core_rows, list) or not 1 <= len(core_rows) <= CORE_LIMIT:
            return ["ordinary snapshot semantic plan receipt/ownership/core population missing or oversized"]
        if (utility and ownership.get("schema") != UTILITY_POPULATION_SCHEMA) or \
                (not utility and ownership.get("schema") not in (None, POPULATION_SCHEMA)):
            return ["ordinary native semantic plan population schema differs from the trusted route"]
        keys = ("trusted_root", "candidate_root", "work_root", "evidence_root", "binaries_root",
                "directory", "python_path", "lab_path", "driver_path")
        if any(not _path(ownership.get(key)) for key in keys) or not _digest(ownership.get("bootstrap_marker_sha256")):
            return ["ordinary snapshot semantic plan pinned roots/tools/bootstrap marker malformed"]
        root, trusted, work, evidence = (ownership[key] for key in ("candidate_root", "trusted_root", "work_root", "evidence_root"))
        roots = (root, trusted, work, evidence)
        if any(_overlap(left, right) for index, left in enumerate(roots) for right in roots[index + 1:]) or \
                ownership["binaries_root"] != work + "/bin" or ownership["directory"] != evidence + "/owned-phases" or \
                any(_overlap(ownership[key], directory) for key in ("python_path", "lab_path")
                    for directory in (root, work, evidence)):
            return ["ordinary snapshot semantic plan matched roots/binaries/evidence/tools overlap or differ"]
        prefix = trusted + "/.cache/bootstrap-driver/posix/"
        driver = ownership["driver_path"]
        relative = driver[len(prefix):] if driver.startswith(prefix) else ""
        parts = relative.split("/")
        if len(parts) != 2 or not HASH.fullmatch(parts[0]) or not re.fullmatch(r"build-[A-Za-z0-9-]+", parts[1]):
            return ["ordinary snapshot semantic plan driver is outside the exact pinned trusted cache"]
        identity = receipt.get("identity")
        inline = receipt.get("inline_acceptance")
        closure = receipt.get("closure")
        if not isinstance(identity, dict) or any(not isinstance(identity.get(key), str) or not COMMIT.fullmatch(identity[key])
                for key in ("base", "base_tree", "head", "head_tree")) or \
                receipt.get("mode") not in ("main", "pull") or \
                not _same(receipt.get("profile"), PROFILE) or not _same(receipt.get("throughput_profile"), THROUGHPUT_PROFILE) or \
                not isinstance(inline, dict) or type(inline.get("requested")) is not bool:
            return ["ordinary snapshot semantic plan source/profile/policy/extension identity unsupported"]
        policy = receipt.get("preparation_policy")
        legacy = utility and policy == "legacy-rebuild"
        if utility and (receipt.get("mode") != "main" or policy not in ("legacy-rebuild", "snapshot-v1") or
                inline["requested"] or inline.get("profile") is not None or
                any(receipt.get(key) is not None for key in ("scaling_profile", "analyzer_profile", "analyzer"))):
            return ["Utility native semantic plan mode/policy/profile/extensions unsupported"]
        if legacy:
            if closure is not None:
                return ["Utility legacy semantic plan must retain the original rebuild route without a snapshot closure"]
        else:
            if policy != "snapshot-v1" or not isinstance(closure, dict) or \
                    closure.get("policy") != "snapshot-v1" or closure.get("fallback") is not None:
                return ["ordinary snapshot semantic plan source/profile/policy/extension identity unsupported"]
            snapshot = closure.get("snapshot")
            if not isinstance(snapshot, dict) or not _digest(snapshot.get("manifest_sha256")) or \
                    snapshot.get("root_sha256") != hashlib.sha256(root.encode()).hexdigest() or \
                    snapshot.get("base") != identity["base"] or snapshot.get("base_tree") != identity["base_tree"]:
                return ["ordinary snapshot semantic plan frozen root/base/tree/manifest binding mismatch"]
        if inline["requested"] and (receipt["mode"] != "pull" or not _same(inline.get("profile"), INLINE_ACCEPTANCE_PROFILE)) or \
                (not inline["requested"] and inline.get("profile") is not None) or \
                (receipt.get("scaling_profile") is not None and
                    (receipt["mode"] != "pull" or not _same(receipt["scaling_profile"], SCALING_PROFILE))):
            return ["ordinary snapshot semantic plan extension profile/mode unsupported"]
        planned = _commands(receipt, ownership, utility=utility)
        if len(core_rows) != len(planned):
            return ["ordinary snapshot semantic core/extension command count differs from the complete route"]
        reasons = []
        for index, (row, wanted) in enumerate(zip(core_rows, planned), 1):
            if not isinstance(row, dict) or any(not _same(row.get(key), value) for key, value in wanted.items()):
                reasons.append(f"ordinary snapshot semantic phase {index} ({wanted['phase']}) argv/cwd/timeout/kind differs from the fixed route")
        return reasons
    except (ValueError, UnicodeError, TypeError, KeyError, IndexError, AttributeError, RecursionError):
        return ["ordinary snapshot semantic command plan malformed"]
