#!/usr/bin/env python3
"""Evaluate zen5-calibration-v1 attempt bundles into the #426 A/A policy document.

Ownership: this offline reader owns the version-1 empirical A/A eligibility
calculation proposed by #1188 (`docs/zen5-aa-eligibility-proposal.md` on that
branch), instantiated with the inputs decided on #36: quantile q = 0.90 under
the proposed 42-member family and 0.05 Bonferroni allocation (so at least 64
confirmatory attempts), #881 current-job pair count P = 60 with runtime rows
(U = R), and binding by the recipe-profile pin `aa-policy-sha256=`. It never
runs a compiler, never touches a host, and always reports `ab_authorized=false`.

Inputs: a reviewer-authored protocol (`buster-zen5-aa-protocol-v1`), frozen and
published before the first confirmatory attempt, and an attempt ledger
(`buster-zen5-aa-attempt-ledger-v1`) that names every retained attempt bundle
in execution order with the final-manifest, profile, plan and capture digests
the authenticated service channel supplied, plus the complete list of jobs the
service reports in the protocol's confirmatory job range. Output: the canonical
policy document whose SHA-256 is the value a later `aa-policy-sha256=` pin
would carry, plus a Markdown report.

Entry points: `evaluate`, `verify` (byte replay of a policy document),
`template` (unapproved protocol skeleton) and `--self-test`
(`zen5_aa_evaluator_test.py`).

Map: protocol_problems/protocol_unset (protocol contract), ledger_problems
(order and roles), window_problems (complete confirmatory job set),
read_manifest/bundle_problems (BQ-BUNDLE-V1 replay), manifest_reasons
(authenticated final manifest), attempt_record (zen5_calibration_handoff.replay,
PMU record, member_values with the constant-series rule),
order_statistic_rank/minimum_attempts (exact binomial bound),
doubled_ranks/stationarity (seeded rank permutation tests, packed integer
fields), family_statistics, build_policy (status precedence invalid >
unavailable > inconclusive > eligible), render_report, main.
"""

from __future__ import annotations

import argparse
from decimal import Decimal
from fractions import Fraction
import json
import math
import operator
import os
from pathlib import Path
import platform
import re
import stat
import statistics
import struct
import sys
from typing import Any, Iterator

from zen5_aa_noise import (
    SHA256_RE, NoiseError, analyze_capture as analyze_aa, canonical_bytes, load_json,
    sha256_bytes, sha256_file, write_json,
)
from zen5_build_control import analyze_capture as analyze_build
from zen5_calibration_handoff import KINDS, replay as handoff_replay
from zen5_qualification_common import QualificationError, load_manifest
from zen5_qualification_replay import validate_result as validate_pmu

POLICY_SCHEMA = "buster-zen5-aa-policy-v1"
PROTOCOL_SCHEMA = "buster-zen5-aa-protocol-v1"
LEDGER_SCHEMA = "buster-zen5-aa-attempt-ledger-v1"
POLICY_ID = "buster-zen5-aa-eligibility-v1"
RECIPE = "zen5-calibration-v1"
# Decided on #36 (comment 5919407135); the family and alpha are #1188's proposal.
QUANTILE = "0.90"
FAMILY_ALPHA = "0.05"
# Across-attempt independence, decided on #36 (comment 5921978144): family alpha
# 0.05, Bonferroni over 2K = 84 checks, at least 100,000 seeded permutations.
STATIONARITY_ALPHA = "0.05"
MIN_PERMUTATIONS = 100_000
PAIRS_PER_ROUND = 60
RUNTIME_ROWS = "U=R"
CONTROLS = ("immutable", *KINDS)
METRICS = ("wall_time_ns", "peak_rss_bytes")
CHECKS = ("pair_resolution", "variant_effect", "path_effect", "order_effect",
          "block_shift", "serial_effect", "linear_drift")
MEMBERS = tuple(f"{control}.{metric}.{check}" for control in CONTROLS for metric in METRICS for check in CHECKS)
FAMILY_SIZE = len(MEMBERS)
STATIONARITY_TESTS = ("trend", "serial")
APPLICABILITY = ("revision", "tree", "source_identity_sha256", "profile_sha256")
CONTROL_FILES = frozenset({f"{RECIPE}.manifest", f"{RECIPE}.bundle", f"{RECIPE}.outcome"})
# Service-authenticated digests per attempt: the final manifest bytes (the RESULT
# reply's manifest-sha256, BQEXP001 offset 112) and the compiled recipe profile
# (BQEXP001 offset 608), beside the plan and capture digests the manifest names.
TRUSTED_DIGESTS = ("manifest_sha256", "profile_sha256", "plan_sha256")
REQUIRED_FILES = {"plan": "zen5/plan.json", "pmu": "zen5/pmu/zen5-pmu-v1.json",
                  **{kind: f"zen5/captures/{kind}.json" for kind in CONTROLS}}
MANIFEST_FIXED = {"schema": "1", "recipe": RECIPE, "status": "succeeded", "stage": "complete",
                  "process-result": "success", "ab-authorized": "false", "aa-decision": "not-evaluated",
                  "pmu-status": "pmu-qualified", "oracle-consistent": "true", "trusted-builds": "5",
                  "pairs": "360", "timed-children": "720"}
MAX_MANIFEST_BYTES = 64 * 1024
MAX_BUNDLE_BYTES = 8 * 1024 * 1024
MAX_PERMUTATIONS = 1_000_000
# Bounds the packed n*n serial table in stationarity; the service queue holds
# 64 lifetime submissions, so a single confirmatory window is far below this.
MAX_CONFIRMATORY = 256
NANOSECONDS = 1_000_000_000
DECIMAL_RE = re.compile(r"^(0|[1-9][0-9]{0,8})(\.[0-9]{1,12})?$")
TOKEN_RE = re.compile(r"^[1-9][0-9]{0,19}$")
COUNT_RE = re.compile(r"^(0|[1-9][0-9]{0,19})$")
COMMIT_RE = re.compile(r"^[0-9a-f]{40}$")
BUNDLE_LINE_RE = re.compile(r"^([0-9a-f]{64}) (0|[1-9][0-9]{0,19}) (\S{1,192})$")
MASK64 = (1 << 64) - 1
EVALUATOR_SOURCES = ("zen5_aa_evaluator.py", "zen5_aa_noise.py", "zen5_build_control.py",
                     "zen5_calibration_handoff.py", "zen5_qualification_common.py",
                     "zen5_qualification_replay.py", "zen5_pmu_events_v1.json")


class EvaluatorError(ValueError):
    """A protocol or ledger that cannot be evaluated at all."""


def fraction(text: str) -> Fraction:
    return Fraction(Decimal(text))


def member_alpha() -> Fraction:
    return fraction(FAMILY_ALPHA) / FAMILY_SIZE


def order_statistic_rank(n: int) -> int | None:
    """Smallest k with Pr[Binomial(n, q) <= k - 1] >= 1 - alpha/K, else None (unbounded)."""
    q = fraction(QUANTILE)
    target = 1 - member_alpha()
    cumulative = Fraction(0)
    rank = None
    k = 1
    while rank is None and k <= n:
        cumulative += math.comb(n, k - 1) * q ** (k - 1) * (1 - q) ** (n - k + 1)
        if cumulative >= target:
            rank = k
        k += 1
    return rank


def minimum_attempts() -> int:
    """Fewest attempts with a finite bound: q**n <= alpha/K (64 for q = 0.90, K = 42)."""
    q = fraction(QUANTILE)
    n = 1
    while q ** n > member_alpha():
        n += 1
    return n


def is_decimal(value: Any) -> bool:
    return isinstance(value, str) and DECIMAL_RE.fullmatch(value) is not None


def is_integer(value: Any, low: int, high: int) -> bool:
    return isinstance(value, int) and not isinstance(value, bool) and low <= value <= high


def is_digest(value: Any, pattern: re.Pattern[str] = SHA256_RE) -> bool:
    return isinstance(value, str) and pattern.fullmatch(value) is not None


def is_token(value: Any) -> bool:
    return isinstance(value, str) and TOKEN_RE.fullmatch(value) is not None


def template_protocol() -> dict[str, Any]:
    """The version-1 protocol shape: decided inputs filled, reviewer choices unset."""
    return {
        "schema": PROTOCOL_SCHEMA, "version": 1, "policy_id": POLICY_ID,
        "quantile": QUANTILE, "family_alpha": FAMILY_ALPHA, "family_size": FAMILY_SIZE,
        "confirmatory_attempts": minimum_attempts(), "confirmatory_jobs": None, "pilot_attempts": None,
        "limits": {member: None for member in MEMBERS},
        "stationarity": {"alpha": STATIONARITY_ALPHA, "permutations": MIN_PERMUTATIONS, "seed": None},
        "applicability": {key: None for key in APPLICABILITY},
        "current_job": {"pairs_per_round": PAIRS_PER_ROUND, "runtime_rows": RUNTIME_ROWS,
                        "equivalence_band": None},
        "approval": None,
    }


def protocol_problems(protocol: Any) -> list[str]:
    """Structural problems; a protocol with any of these is refused outright."""
    if not isinstance(protocol, dict) or protocol.get("schema") != PROTOCOL_SCHEMA:
        return [f"protocol schema must be {PROTOCOL_SCHEMA}"]
    problems: list[str] = []
    template = template_protocol()
    if set(protocol) != set(template):
        problems.append("protocol fields differ from version 1")
        return problems
    for key in ("version", "policy_id", "quantile", "family_alpha", "family_size"):
        if protocol[key] != template[key] or type(protocol[key]) is not type(template[key]):
            problems.append(f"protocol {key} must be {template[key]!r} (decided or fixed by evaluator version 1)")
    count = protocol["confirmatory_attempts"]
    if not is_integer(count, minimum_attempts(), MAX_CONFIRMATORY):
        problems.append(f"confirmatory_attempts must be an integer in {minimum_attempts()}..{MAX_CONFIRMATORY}")
    jobs = protocol["confirmatory_jobs"]
    if jobs is not None and (not isinstance(jobs, dict) or set(jobs) != {"first_job_id", "last_job_id"} or
                             not all(is_token(value) for value in jobs.values()) or
                             int(jobs["first_job_id"]) > int(jobs["last_job_id"])):
        problems.append("confirmatory_jobs must be null or {first_job_id, last_job_id} tokens with first <= last")
    pilots = protocol["pilot_attempts"]
    if pilots is not None and (not isinstance(pilots, list) or not all(
            isinstance(item, dict) and set(item) == {"job_id", "attempt"} and
            all(is_token(item[key]) for key in item) for item in pilots)):
        problems.append("pilot_attempts must be null or a list of {job_id, attempt} decimal tokens")
    limits = protocol["limits"]
    if not isinstance(limits, dict) or set(limits) != set(MEMBERS):
        problems.append(f"limits must name exactly the {FAMILY_SIZE} family members")
    elif not all(value is None or is_decimal(value) for value in limits.values()):
        problems.append("each limit must be null or a nonnegative decimal string")
    station = protocol["stationarity"]
    if not isinstance(station, dict) or set(station) != {"alpha", "permutations", "seed"}:
        problems.append("stationarity must contain alpha, permutations and seed")
    else:
        alpha, count_p, seed = station["alpha"], station["permutations"], station["seed"]
        if alpha != STATIONARITY_ALPHA:
            problems.append(f"stationarity.alpha must be {STATIONARITY_ALPHA!r} (decided on #36)")
        if not is_integer(count_p, MIN_PERMUTATIONS, MAX_PERMUTATIONS):
            problems.append(f"stationarity.permutations must be an integer in {MIN_PERMUTATIONS}..{MAX_PERMUTATIONS} "
                            "(decided on #36)")
        elif Fraction(1, count_p + 1) > fraction(STATIONARITY_ALPHA) / (len(STATIONARITY_TESTS) * FAMILY_SIZE):
            problems.append("stationarity.permutations cannot reach the family-corrected stationarity alpha")
        if seed is not None and not is_integer(seed, 1, MASK64):
            problems.append("stationarity.seed must be null or a positive 64-bit integer")
    applicability = protocol["applicability"]
    if not isinstance(applicability, dict) or set(applicability) != set(APPLICABILITY) or not all(
            value is None or is_digest(value, COMMIT_RE if key in ("revision", "tree") else SHA256_RE)
            for key, value in applicability.items()):
        problems.append("applicability must name revision, tree, source_identity_sha256 and profile_sha256")
    job = protocol["current_job"]
    if not isinstance(job, dict) or set(job) != {"pairs_per_round", "runtime_rows", "equivalence_band"} or (
            job["pairs_per_round"] != PAIRS_PER_ROUND or type(job["pairs_per_round"]) is not int or
            job["runtime_rows"] != RUNTIME_ROWS):
        problems.append(f"current_job must keep pairs_per_round={PAIRS_PER_ROUND} and runtime_rows={RUNTIME_ROWS}")
    else:
        band = job["equivalence_band"]
        if band is not None and (not isinstance(band, dict) or set(band) != {"lower", "upper"} or
                                 not all(is_decimal(band[key]) for key in band) or
                                 not 0 < fraction(band["lower"]) < 1 < fraction(band["upper"])):
            problems.append("equivalence_band must be null or {lower, upper} decimals with 0 < lower < 1 < upper")
    approval = protocol["approval"]
    if approval is not None and (not isinstance(approval, dict) or set(approval) != {"approved_by", "reference"} or
                                 not all(isinstance(value, str) and value for value in approval.values())):
        problems.append("approval must be null or {approved_by, reference} nonempty strings")
    return problems


def protocol_unset(protocol: dict[str, Any]) -> list[str]:
    """Reviewer choices #1188 leaves open; any unset one makes the result unavailable."""
    unset = [f"limits.{member}" for member in MEMBERS if protocol["limits"][member] is None]
    unset += ["stationarity.seed"] if protocol["stationarity"]["seed"] is None else []
    unset += [f"applicability.{key}" for key in APPLICABILITY if protocol["applicability"][key] is None]
    for key, value in (("confirmatory_jobs", protocol["confirmatory_jobs"]),
                       ("pilot_attempts", protocol["pilot_attempts"]),
                       ("current_job.equivalence_band", protocol["current_job"]["equivalence_band"]),
                       ("approval", protocol["approval"])):
        if value is None:
            unset.append(key)
    return unset


def ledger_problems(ledger: Any) -> list[str]:
    if not isinstance(ledger, dict) or ledger.get("schema") != LEDGER_SCHEMA or ledger.get("version") != 1 or (
            set(ledger) != {"schema", "version", "window_jobs", "attempts"}):
        return [f"ledger must be {LEDGER_SCHEMA} version 1 with exactly schema, version, window_jobs and attempts"]
    attempts = ledger["attempts"]
    if not isinstance(attempts, list) or not attempts:
        return ["ledger attempts must be a nonempty list"]
    problems: list[str] = []
    window = ledger["window_jobs"]
    if not isinstance(window, list) or not all(is_token(job) for job in window) or any(
            int(left) >= int(right) for left, right in zip(window, window[1:])):
        problems.append("ledger window_jobs must be a strictly increasing list of service job id tokens")
    previous: tuple[int, int] | None = None
    confirmatory_seen = False
    for index, entry in enumerate(attempts):
        name = f"ledger attempt {index}"
        if not isinstance(entry, dict) or set(entry) != {"role", "job_id", "attempt", "bundle", "trusted"}:
            problems.append(f"{name} must contain exactly role, job_id, attempt, bundle and trusted")
            continue
        if entry["role"] not in ("pilot", "confirmatory"):
            problems.append(f"{name} role must be pilot or confirmatory")
        elif entry["role"] == "pilot" and confirmatory_seen:
            problems.append(f"{name}: a pilot follows a confirmatory attempt")
        confirmatory_seen = confirmatory_seen or entry["role"] == "confirmatory"
        if not all(is_token(entry[key]) for key in ("job_id", "attempt")):
            problems.append(f"{name} job_id and attempt must be decimal tokens")
        else:
            current = (int(entry["job_id"]), int(entry["attempt"]))
            if previous is not None and current <= previous:
                problems.append(f"{name} is not after its predecessor in execution order")
            previous = current
        bundle = entry["bundle"]
        if not isinstance(bundle, str) or not bundle or bundle.startswith("/") or ".." in bundle.split("/"):
            problems.append(f"{name} bundle must be a relative path without ..")
        trusted = entry["trusted"]
        if trusted is not None and (
                not isinstance(trusted, dict) or set(trusted) != set(TRUSTED_DIGESTS) | {"captures"} or
                not all(is_digest(trusted[key]) for key in TRUSTED_DIGESTS) or
                not isinstance(trusted["captures"], dict) or set(trusted["captures"]) != set(CONTROLS) or
                not all(is_digest(value) for value in trusted["captures"].values())):
            problems.append(f"{name} trusted must be null or {{{', '.join(TRUSTED_DIGESTS)}, captures}} "
                            "service digests")
    return problems


def window_problems(protocol: dict[str, Any], window: list[str], records: list[dict[str, Any]]) -> list[str]:
    """The confirmatory set must be exactly the service's complete job list for the declared range."""
    problems: list[str] = []
    confirmatory = [record["job_id"] for record in records if record["role"] == "confirmatory"]
    problems += [f"confirmatory job {job} has more than one attempt; a replaced attempt voids the set"
                 for job in sorted(set(confirmatory), key=int) if confirmatory.count(job) > 1]
    jobs = protocol["confirmatory_jobs"]
    if jobs is not None:
        first, last = int(jobs["first_job_id"]), int(jobs["last_job_id"])
        problems += [f"ledger window job {job} is outside the declared confirmatory job range"
                     for job in window if not first <= int(job) <= last]
        problems += [f"confirmatory job {job} is outside the declared confirmatory job range"
                     for job in dict.fromkeys(confirmatory) if not first <= int(job) <= last]
        problems += [f"pilot job {record['job_id']} lies inside the declared confirmatory job range"
                     for record in records if record["role"] == "pilot" and first <= int(record["job_id"]) <= last]
        problems += [f"service job {job} in the declared confirmatory range is missing from the ledger"
                     for job in window if job not in confirmatory]
        problems += [f"confirmatory job {job} is not in the service's job list for the declared range"
                     for job in dict.fromkeys(confirmatory) if job not in window]
    return problems


def read_bounded(path: Path, maximum: int) -> bytes | None:
    data = None
    try:
        if stat.S_ISREG(os.lstat(path).st_mode):
            with path.open("rb") as source:
                data = source.read(maximum + 1)
    except OSError:
        data = None
    return data if data is not None and len(data) <= maximum else None


def read_manifest(path: Path) -> tuple[dict[str, str] | None, list[str], str | None]:
    raw = read_bounded(path, MAX_MANIFEST_BYTES)
    if raw is None:
        return None, ["final manifest is missing; the attempt is incomplete"], None
    fields: dict[str, str] = {}
    problems: list[str] = []
    try:
        text = raw.decode("utf-8")
    except UnicodeDecodeError:
        return None, ["final manifest is not UTF-8"], sha256_bytes(raw)
    if not text.endswith("\n"):
        problems.append("final manifest is truncated")
    for line in text[:-1].split("\n"):
        key, separator, value = line.partition("=")
        if not separator or not key or key in fields:
            problems.append(f"final manifest line is malformed or duplicated: {line[:64]!r}")
        else:
            fields[key] = value
    return fields, problems, sha256_bytes(raw)


def bundle_problems(root: Path, expected_sha256: str) -> tuple[list[str], set[str]]:
    """Replay the BQ-BUNDLE-V1 index exactly as the worker binds it."""
    raw = read_bounded(root / f"{RECIPE}.bundle", MAX_BUNDLE_BYTES)
    if raw is None:
        return ["bundle index is missing"], set()
    problems: list[str] = []
    listed: set[str] = set()
    if sha256_bytes(raw) != expected_sha256:
        problems.append("bundle index digest differs from the final manifest")
    lines = raw.decode("ascii", errors="replace").split("\n")
    header = lines[:3]
    entries = lines[3:-1]
    if lines[-1] != "" or header[:1] != ["BQ-BUNDLE-V1"] or len(header) != 3 or \
            header[1] != f"entries={len(entries)}" or not header[2].startswith("bytes="):
        return problems + ["bundle index header is malformed"], listed
    total = 0
    previous = b""
    for line in entries:
        match = BUNDLE_LINE_RE.fullmatch(line)
        path = match.group(3) if match else ""
        if not match or path in CONTROL_FILES or any(part in ("", ".", "..") for part in path.split("/")) or \
                path.encode("utf-8") <= previous:
            problems.append(f"bundle entry is malformed or unsorted: {line[:96]!r}")
            continue
        previous = path.encode("utf-8")
        listed.add(path)
        total += int(match.group(2))
        target = root / path
        try:
            info = os.lstat(target)
            ok = stat.S_ISREG(info.st_mode) and info.st_size == int(match.group(2)) and \
                sha256_file(target) == match.group(1)
        except (OSError, NoiseError):
            ok = False
        if not ok:
            problems.append(f"bundle file differs from its index: {path}")
    if header[2] != f"bytes={total}":
        problems.append("bundle index byte total differs")
    walk_errors: list[OSError] = []
    for directory, subdirectories, files in os.walk(root, onerror=walk_errors.append, followlinks=False):
        for name in subdirectories + files:
            full = Path(directory) / name
            relative = full.relative_to(root).as_posix()
            try:
                mode = os.lstat(full).st_mode
            except OSError:
                mode = None
            if mode is None:
                problems.append(f"bundle entry changed or vanished while it was inventoried: {relative}")
            elif stat.S_ISDIR(mode):
                continue
            elif not stat.S_ISREG(mode):
                problems.append(f"bundle contains a non-regular file: {relative}")
            elif relative not in listed and relative not in CONTROL_FILES:
                problems.append(f"bundle contains an unlisted file: {relative}")
    problems += [f"bundle directory cannot be inventoried: {error.strerror}" for error in walk_errors]
    return problems, listed


def member_values(analyses: dict[str, dict[str, Any]]) -> tuple[dict[str, float | None], list[str]]:
    """Reduce each capture to the 42 predeclared worst-block/within-capture statistics.

    Decided on #36 (comment 5921955897): a zero-variance pair-center series is
    recorded as `constant` with serial effect exactly 0, since it cannot be
    serially dependent. The analyzers' other undefined values (a lag-one
    correlation with one constant window, a drift over a zero mean) stay None,
    never zero. A constant series already has a defined drift of 0."""
    values: dict[str, float | None] = {}
    constant: list[str] = []
    for control in CONTROLS:
        immutable = control == "immutable"
        effect = "median_label_effect" if immutable else "median_build_effect"
        resolution = "empirical_resolution_fraction" if immutable else "observed_absolute_build_difference_p95"
        for metric in METRICS:
            model = analyses[control]["metrics"][metric]
            blocks = model["per_block"]
            center = model["pair_center"]
            lag, drift = center["lag_one_correlation"], center["relative_linear_drift_per_pair"]
            flat = center["minimum"] == center["maximum"]
            if flat:
                constant.append(f"{control}.{metric}")
            computed = {
                "pair_resolution": model[resolution],
                "variant_effect": max(abs(block[effect]) for block in blocks),
                "path_effect": max(abs(block["median_path_effect"]) for block in blocks),
                "order_effect": max(abs(block["median_order_effect"]) for block in blocks),
                "block_shift": center["maximum_absolute_block_median_shift"],
                "serial_effect": 0.0 if flat else None if lag is None else abs(lag),
                "linear_drift": None if drift is None else abs(drift),
            }
            for check in CHECKS:
                value = computed[check]
                values[f"{control}.{metric}.{check}"] = (
                    float(value) if value is not None and math.isfinite(value) else None)
    return values, constant


def manifest_reasons(manifest: dict[str, str], digest: str | None, entry: dict[str, Any]) -> list[str]:
    reasons = [f"final manifest {key} is {manifest.get(key)!r}, not {value!r}"
               for key, value in MANIFEST_FIXED.items() if manifest.get(key) != value]
    budget, elapsed = manifest.get("budget-seconds", ""), manifest.get("elapsed-ns", "")
    if not TOKEN_RE.fullmatch(budget) or not COUNT_RE.fullmatch(elapsed):
        reasons.append("final manifest budget-seconds or elapsed-ns is not a decimal count")
    elif int(elapsed) > int(budget) * NANOSECONDS:
        reasons.append(f"final manifest elapsed-ns {elapsed} exceeds budget-seconds {budget}")
    if manifest.get("status") == "failed":
        reasons.append(f"attempt failed at stage {manifest.get('stage')!r}: {manifest.get('reason', '')}")
    if (manifest.get("job-id"), manifest.get("attempt-token")) != (entry["job_id"], entry["attempt"]):
        reasons.append("final manifest job/attempt differs from the ledger")
    if manifest.get("base-revision") != manifest.get("candidate-revision"):
        reasons.append("final manifest base and candidate revisions differ")
    for kind in CONTROLS:
        status = manifest.get(f"{kind}-capture-status")
        if status is not None and status != "complete":
            reasons.append(f"final manifest {kind} capture status is {status!r}")
    trusted = entry["trusted"]
    if trusted is None:
        reasons.append("no authenticated service manifest/profile/plan/capture digests were supplied")
    else:
        if digest != trusted["manifest_sha256"]:
            reasons.append("final manifest bytes differ from the service-authenticated manifest digest")
        if manifest.get("profile-sha256") != trusted["profile_sha256"]:
            reasons.append("final manifest profile digest differs from the authenticated profile digest")
        if manifest.get("plan-sha256") != trusted["plan_sha256"]:
            reasons.append("final manifest plan digest differs from the authenticated digest")
        for kind in CONTROLS:
            if manifest.get(f"{kind}-capture-sha256") != trusted["captures"][kind]:
                reasons.append(f"final manifest {kind} capture digest differs from the authenticated digest")
    return reasons


def attempt_record(entry: dict[str, Any], base: Path, pmu_manifest: dict[str, Any], pmu_digest: str) -> dict[str, Any]:
    """Validate one retained attempt; invalid attempts keep their reasons and no member values."""
    root = base / entry["bundle"]
    reasons: list[str] = []
    identities: dict[str, Any] = {"manifest_sha256": None, "bundle_sha256": None, "plan_sha256": None,
                                  "captures": None, "pmu_sha256": None, "profile_sha256": None,
                                  "revision": None, "tree": None, "source_identity_sha256": None}
    members = None
    constant = None
    manifest, problems, manifest_digest = (None, ["attempt bundle directory is missing"], None) if not root.is_dir() \
        else read_manifest(root / f"{RECIPE}.manifest")
    reasons += problems
    listed: set[str] = set()
    if manifest is not None:
        identities.update(manifest_sha256=manifest_digest, bundle_sha256=manifest.get("bundle-sha256"),
                          plan_sha256=manifest.get("plan-sha256"), profile_sha256=manifest.get("profile-sha256"),
                          revision=manifest.get("candidate-revision"), tree=manifest.get("source-tree"),
                          source_identity_sha256=manifest.get("source-identity-sha256"),
                          captures={kind: manifest.get(f"{kind}-capture-sha256") for kind in CONTROLS})
        bundle_issues, listed = bundle_problems(root, str(manifest.get("bundle-sha256")))
        reasons += bundle_issues
        reasons += manifest_reasons(manifest, manifest_digest, entry)
        reasons += [f"bundle does not list {path}" for path in REQUIRED_FILES.values() if path not in listed]
    if not reasons:
        try:
            # Parse exactly the bytes that were hashed, never a second read.
            raw = {name: read_bounded(root / path, 64 * 1024 * 1024) for name, path in REQUIRED_FILES.items()}
            documents = {name: json.loads(data) for name, data in raw.items()}
            digests = {name: sha256_bytes(data) for name, data in raw.items()}
            identities["pmu_sha256"] = digests["pmu"]
            plan, pmu = documents["plan"], documents["pmu"]
            captures = {kind: documents[kind] for kind in CONTROLS}
            if digests["plan"] != entry["trusted"]["plan_sha256"]:
                reasons.append("plan bytes differ from the authenticated plan digest")
            report = handoff_replay(plan, entry["trusted"]["plan_sha256"], captures,
                                    {kind: digests[kind] for kind in CONTROLS}, entry["trusted"]["captures"])
            reasons += [f"replay: {error}" for error in report["errors"]]
            for kind, capture in captures.items():
                if capture.get("ab_authorized") is not False:
                    reasons.append(f"{kind} capture must carry ab_authorized=false")
                if (capture.get("recipe"), capture.get("job"), capture.get("attempt")) != (
                        RECIPE, entry["job_id"], entry["attempt"]):
                    reasons.append(f"{kind} capture recipe/job/attempt differs from the ledger")
            reasons += [f"PMU record: {problem}" for problem in validate_pmu(pmu, pmu_manifest, pmu_digest)]
            if pmu.get("qualification_status") != "pmu-qualified":
                reasons.append("PMU record is not pmu-qualified")
            if not (digests["pmu"] == plan.get("host_qualification_sha256") == manifest["host-qualification-sha256"]):
                reasons.append("PMU record digest differs from the plan or final manifest")
            if pmu.get("environment_fingerprint_sha256") != plan.get("environment_fingerprint_sha256"):
                reasons.append("PMU environment fingerprint differs from the plan")
            repository = plan.get("repository") if isinstance(plan.get("repository"), dict) else {}
            if (repository.get("revision"), repository.get("tree"), plan.get("source_identity_sha256"),
                    plan.get("expected_output_sha256")) != (manifest["candidate-revision"], manifest["source-tree"],
                                                          manifest["source-identity-sha256"],
                                                          manifest["oracle-output-sha256"]):
                reasons.append("plan source/output identity differs from the final manifest")
            if not reasons:
                analyses = {kind: (analyze_aa if kind == "immutable" else analyze_build)(captures[kind], digests[kind])
                            for kind in CONTROLS}
                members, constant = member_values(analyses)
        except (NoiseError, QualificationError, OSError, KeyError, TypeError, ValueError, AttributeError,
                RecursionError) as error:
            reasons.append(f"bundle replay failed: {error}")
            members = None
    return {"role": entry["role"], "job_id": entry["job_id"], "attempt": entry["attempt"],
            "valid": not reasons, "invalid_reasons": reasons, "identities": identities,
            "members": members if not reasons else None, "constant_series": constant if not reasons else None}


def splitmix_orders(n: int, count: int, seed: int) -> Iterator[list[int]]:
    """Fisher-Yates orders from a splitmix64 stream with threshold rejection (fixed seed)."""
    state = seed & MASK64
    for _ in range(count):
        order = list(range(n))
        for remaining in range(n, 1, -1):
            threshold = (1 << 64) % remaining
            number = -1
            while number < threshold:
                state = (state + 0x9E3779B97F4A7C15) & MASK64
                mixed = state
                mixed = ((mixed ^ (mixed >> 30)) * 0xBF58476D1CE4E5B9) & MASK64
                mixed = ((mixed ^ (mixed >> 27)) * 0x94D049BB133111EB) & MASK64
                number = mixed ^ (mixed >> 31)
            index = number % remaining
            order[remaining - 1], order[index] = order[index], order[remaining - 1]
        yield order


def doubled_ranks(values: list[float]) -> list[int]:
    """Twice the average (tie-shared) rank, so every statistic stays an exact integer."""
    order = sorted(range(len(values)), key=lambda index: (values[index], index))
    ranks = [0] * len(values)
    start = 0
    while start < len(order):
        end = start
        while end + 1 < len(order) and values[order[end + 1]] == values[order[start]]:
            end += 1
        for position in range(start, end + 1):
            ranks[order[position]] = start + end + 2
        start = end + 1
    return ranks


def stationarity(table: dict[str, list[float]], permutations: int, seed: int) -> dict[str, dict[str, str]]:
    """Two-sided rank permutation p-values in execution order: Spearman trend and lag-one serial.

    Every varying member shares each permutation. Its shifted, nonnegative
    integer ranks occupy one 64-bit field of a packed integer, so one dot
    product gives every member's trend statistic and one walk of a packed n*n
    product table every member's serial statistic, exactly (the offsets are
    known because centered ranks and weights both sum to zero)."""
    if not table:
        return {}
    n = len(next(iter(table.values())))
    span = n - 1
    centered = {member: [rank - (n + 1) for rank in doubled_ranks(values)] for member, values in table.items()}
    # A fully tied series has zero statistics under every order: p = 1 exactly.
    varying = [member for member in table if any(centered[member])]
    fields = len(varying)
    packed = [sum((centered[member][index] + span) << (64 * slot) for slot, member in enumerate(varying))
              for index in range(n)]
    products = [sum((centered[member][left] * centered[member][right] + span * span) << (64 * slot)
                    for slot, member in enumerate(varying)) for left in range(n) for right in range(n)]
    shifted_weights = [2 * index for index in range(n)]
    layout = struct.Struct(f"<{fields}Q")

    def statistics_of(order: list[int]) -> tuple[tuple[int, ...], tuple[int, ...]]:
        trend = sum(map(operator.mul, shifted_weights, map(packed.__getitem__, order)))
        serial = sum(map(products.__getitem__, map(operator.add, map(n.__mul__, order), order[1:])))
        return (tuple(abs(value - n * span * span) for value in layout.unpack(trend.to_bytes(8 * fields, "little"))),
                tuple(abs(value - span ** 3) for value in layout.unpack(serial.to_bytes(8 * fields, "little"))))

    observed = statistics_of(list(range(n))) if varying else ((), ())
    hits = [[0] * fields, [0] * fields]
    for order in splitmix_orders(n, permutations if varying else 0, seed):
        for slot_test, values in enumerate(statistics_of(order)):
            counts, reference = hits[slot_test], observed[slot_test]
            for slot, value in enumerate(values):
                counts[slot] += value >= reference[slot]
    exceed = {member: [permutations, permutations] for member in table}
    for slot, member in enumerate(varying):
        exceed[member] = [hits[0][slot], hits[1][slot]]
    return {member: {test: f"{exceed[member][slot] + 1}/{permutations + 1}"
                     for slot, test in enumerate(STATIONARITY_TESTS)} for member in table}


def family_statistics(protocol: dict[str, Any], confirmatory: list[dict[str, Any]]) -> tuple[dict[str, Any], list[str]]:
    """Simultaneous order-statistic upper bounds plus the stationarity checks for the complete set."""
    n = len(confirmatory)
    rank = order_statistic_rank(n)
    station = protocol["stationarity"]
    reject_at = fraction(STATIONARITY_ALPHA) / (len(STATIONARITY_TESTS) * FAMILY_SIZE)
    table = {member: [record["members"][member] for record in confirmatory] for member in MEMBERS}
    available = {member: values for member, values in table.items() if all(value is not None for value in values)}
    pvalues = stationarity(available, station["permutations"], station["seed"])
    reasons: list[str] = []
    members: dict[str, Any] = {}
    for member in MEMBERS:
        limit = protocol["limits"][member]
        entry: dict[str, Any] = {"n": n, "rank": rank, "bound": None, "limit": limit,
                                 "stationarity": pvalues.get(member), "verdict": "unavailable"}
        if member not in available:
            reasons.append(f"{member}: a confirmatory value is unavailable (undefined, never zero)")
        elif rank is None:
            entry["verdict"] = "unbounded"
            reasons.append(f"{member}: no finite order-statistic bound at n={n}")
        else:
            entry["bound"] = sorted(available[member])[rank - 1]
            flags = []
            # Exact decimal comparison: the bound's shortest round-trip repr against the limit string.
            if Fraction(repr(entry["bound"])) > fraction(limit):
                flags.append("exceeds-limit")
                reasons.append(f"{member}: upper bound {entry['bound']!r} exceeds limit {limit}")
            for test in STATIONARITY_TESTS:
                numerator, denominator = (int(part) for part in pvalues[member][test].split("/"))
                if Fraction(numerator, denominator) <= reject_at:
                    flags.append(f"{test}-dependence")
                    reasons.append(f"{member}: across-attempt {test} p={pvalues[member][test]} <= {reject_at}")
            entry["verdict"] = "+".join(flags) or "within-limit"
        members[member] = entry
    return {"rank": rank, "stationarity_reject_at": str(reject_at), "members": members}, reasons


def pilot_summary(pilots: list[dict[str, Any]]) -> dict[str, Any]:
    summary: dict[str, Any] = {}
    valid = [record for record in pilots if record["valid"]]
    for member in MEMBERS:
        values = [record["members"][member] for record in valid if record["members"][member] is not None]
        summary[member] = {"count": len(values), "unavailable": len(valid) - len(values),
                           "minimum": min(values) if values else None,
                           "median": statistics.median(values) if values else None,
                           "maximum": max(values) if values else None}
    return summary


def evaluator_identity() -> dict[str, Any]:
    """Source digests only. The analyzers use math.fsum since 383fa7763 (#2110) and the
    evaluator's own statistics are exact integers or rationals, so the document bytes do not
    depend on the interpreter; the interpreter is reported, never hashed."""
    here = Path(__file__).resolve().parent
    return {"sources": {name: sha256_file(here / name) for name in EVALUATOR_SOURCES}}


def build_policy(protocol: dict[str, Any], window: list[str], records: list[dict[str, Any]]) -> dict[str, Any]:
    confirmatory = [record for record in records if record["role"] == "confirmatory"]
    pilots = [record for record in records if record["role"] == "pilot"]
    declared = protocol["confirmatory_attempts"]
    valid_count = sum(record["valid"] for record in confirmatory)
    invalid = [f"confirmatory attempt job {record['job_id']} attempt {record['attempt']} is invalid and retained"
               for record in confirmatory if not record["valid"]]
    if len(confirmatory) > declared:
        invalid.append(f"{len(confirmatory)} confirmatory attempts exceed the fixed count {declared}")
    declared_pilots = protocol["pilot_attempts"]
    if declared_pilots is not None and declared_pilots != [
            {"job_id": record["job_id"], "attempt": record["attempt"]} for record in pilots]:
        invalid.append("ledger pilot attempts differ from the protocol's declared pilot attempts")
    invalid += window_problems(protocol, window, records)
    unavailable = [f"protocol {field} is unset (unapproved)" for field in protocol_unset(protocol)]
    for record in confirmatory:
        for key in APPLICABILITY:
            expected = protocol["applicability"][key]
            if record["valid"] and expected is not None and record["identities"][key] != expected:
                unavailable.append(f"confirmatory job {record['job_id']} attempt {record['attempt']} "
                                   f"{key} is outside the declared applicability")
    inconclusive: list[str] = []
    if len(confirmatory) < declared:
        inconclusive.append(f"insufficient evidence: {len(confirmatory)} of {declared} predeclared "
                            f"confirmatory attempts are present")
    family = None
    withheld = None
    if invalid or unavailable or len(confirmatory) != declared:
        withheld = "confirmatory statistics are withheld until a complete, valid, fixed-count set is " \
                   "evaluated under a complete protocol"
    else:
        family, family_reasons = family_statistics(protocol, confirmatory)
        inconclusive += family_reasons
    status = "invalid" if invalid else "unavailable" if unavailable else "inconclusive" if inconclusive else "eligible"
    attempts = []
    for index, record in enumerate(records):
        shown = dict(record, index=index)
        if record["role"] == "confirmatory" and family is None:
            shown["members"] = shown["constant_series"] = None
        attempts.append(shown)
    return {
        "schema": POLICY_SCHEMA, "version": 1, "policy_id": POLICY_ID, "status": status,
        "reasons": invalid + unavailable + inconclusive,
        "evidence": {"confirmatory_declared": declared, "confirmatory_listed": len(confirmatory),
                     "confirmatory_valid": valid_count, "minimum_for_finite_bound": minimum_attempts(),
                     "sufficient": len(confirmatory) == declared and valid_count == declared,
                     "pilot_listed": len(pilots), "pilot_valid": sum(record["valid"] for record in pilots),
                     "window_jobs": list(window)},
        "ab_authorized": False, "candidate_decision": "not-evaluated",
        "protocol": protocol, "protocol_sha256": sha256_bytes(canonical_bytes(protocol)),
        "method": {"members": list(MEMBERS), "family_size": FAMILY_SIZE, "family_alpha": FAMILY_ALPHA,
                   "member_alpha": str(member_alpha()), "quantile": QUANTILE,
                   "bound": "smallest T_(k) with Pr[Binomial(n,q) <= k-1] >= 1 - alpha/K; ties retained",
                   "stationarity": "seeded two-sided rank permutation tests in execution order "
                                   "(Spearman trend, lag-one serial), Bonferroni over 2K checks",
                   "constant_series": "a zero-variance pair-center series has serial effect exactly 0; "
                                      "every other undefined value stays unavailable",
                   "optional_stopping": False, "outlier_deletion": False},
        "current_job_aa": protocol["current_job"],
        "attempts": attempts, "family": family, "family_withheld": withheld,
        "pilot_summary": pilot_summary(pilots), "evaluator": evaluator_identity(),
    }


def evaluate(protocol: Any, ledger: Any, base: Path) -> dict[str, Any]:
    problems = protocol_problems(protocol) + ledger_problems(ledger)
    if problems:
        raise EvaluatorError("; ".join(problems))
    pmu_manifest, pmu_digest = load_manifest()
    records = [attempt_record(entry, base, pmu_manifest, pmu_digest) for entry in ledger["attempts"]]
    return build_policy(protocol, ledger["window_jobs"], records)


def number(value: Any) -> str:
    return "n/a" if value is None else f"{value:.6g}"


def cell(text: Any) -> str:
    return str(text).replace("|", "\\|").replace("\n", " ")


def render_report(policy: dict[str, Any], digest: str) -> str:
    evidence = policy["evidence"]
    lines = [
        "# Zen 5 A/A policy evaluation", "",
        f"- Status: **{policy['status']}**",
        f"- aa-policy-sha256: `{digest}`",
        f"- Protocol sha256: `{policy['protocol_sha256']}`",
        f"- Interpreter (reported, not hashed): {platform.python_implementation()} {platform.python_version()}",
        "- ab_authorized: false. This document never authorizes A/B and never decides a candidate.",
        f"- Confirmatory evidence: {evidence['confirmatory_listed']} listed, {evidence['confirmatory_valid']} valid, "
        f"{evidence['confirmatory_declared']} predeclared (finite bound needs at least "
        f"{evidence['minimum_for_finite_bound']}).",
    ]
    if not evidence["sufficient"]:
        lines.append("- **INSUFFICIENT EVIDENCE**: the fixed confirmatory set is incomplete or contains invalid "
                     "attempts; no eligibility follows.")
    lines += [f"- Pilot (exploratory) attempts: {evidence['pilot_listed']} listed, {evidence['pilot_valid']} valid; "
              "never counted toward the confirmatory set.", "",
              "## Fixed parameters", "",
              f"q = {QUANTILE}; family K = {FAMILY_SIZE}; family alpha = {FAMILY_ALPHA} "
              f"(per member {member_alpha()}); current-job P = {PAIRS_PER_ROUND}, {RUNTIME_ROWS}.", "",
              "## Reasons", ""]
    lines += [f"- {reason}" for reason in policy["reasons"]] or ["- none"]
    lines += ["", "## Attempts (execution order)", "",
              "| # | Role | Job | Attempt | Valid | Constant series | Invalid reasons |", "|---|---|---|---|---|---|---|"]
    for record in policy["attempts"]:
        constant = "n/a" if not record["valid"] else "withheld" if record["constant_series"] is None else \
            ", ".join(record["constant_series"]) or "none"
        lines.append(f"| {record['index']} | {record['role']} | {record['job_id']} | {record['attempt']} | "
                     f"{'yes' if record['valid'] else 'no'} | {constant} | {cell('; '.join(record['invalid_reasons']))} |")
    lines += ["", "## Confirmatory family", ""]
    if policy["family"] is None:
        lines.append(f"Withheld: {policy['family_withheld']}.")
    else:
        lines += [f"Order-statistic rank k = {policy['family']['rank']}; stationarity rejects at p <= "
                  f"{policy['family']['stationarity_reject_at']}.", "",
                  "| Member | Bound | Limit | Trend p | Serial p | Verdict |", "|---|---|---|---|---|---|"]
        for member, entry in policy["family"]["members"].items():
            station = entry["stationarity"] or {}
            lines.append(f"| {member} | {number(entry['bound'])} | {entry['limit']} | {station.get('trend', 'n/a')} | "
                         f"{station.get('serial', 'n/a')} | {entry['verdict']} |")
    lines += ["", "## Pilot attempts (exploratory, descriptive only)", "",
              "| Member | Values | Unavailable | Minimum | Median | Maximum |", "|---|---|---|---|---|---|"]
    for member, entry in policy["pilot_summary"].items():
        lines.append(f"| {member} | {entry['count']} | {entry['unavailable']} | {number(entry['minimum'])} | "
                     f"{number(entry['median'])} | {number(entry['maximum'])} |")
    return "\n".join(lines) + "\n"


def exit_status(policy: dict[str, Any]) -> int:
    return {"eligible": 0, "invalid": 2}.get(policy["status"], 1)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--self-test", action="store_true")
    commands = parser.add_subparsers(dest="command")
    template = commands.add_parser("template", help="write the unapproved version-1 protocol skeleton")
    template.add_argument("--output", type=Path, required=True)
    for name in ("evaluate", "verify"):
        command = commands.add_parser(name)
        command.add_argument("--protocol", type=Path, required=True)
        command.add_argument("--ledger", type=Path, required=True, help="bundle paths resolve beside the ledger")
    commands.choices["evaluate"].add_argument("--policy-output", type=Path, required=True)
    commands.choices["evaluate"].add_argument("--report-output", type=Path, required=True)
    commands.choices["verify"].add_argument("--policy", type=Path, required=True)
    arguments = parser.parse_args()
    result = 2
    try:
        if arguments.self_test:
            from zen5_aa_evaluator_test import run_self_test

            result = run_self_test()
        elif arguments.command == "template":
            write_json(arguments.output, template_protocol())
            result = 0
        elif arguments.command in ("evaluate", "verify"):
            policy = evaluate(load_json(arguments.protocol), load_json(arguments.ledger), arguments.ledger.parent)
            payload = canonical_bytes(policy)
            digest = sha256_bytes(payload)
            if arguments.command == "evaluate":
                write_json(arguments.policy_output, policy)
                arguments.report_output.parent.mkdir(parents=True, exist_ok=True)
                arguments.report_output.write_text(render_report(policy, digest), encoding="utf-8")
                result = exit_status(policy)
            else:
                matches = read_bounded(arguments.policy, 64 * 1024 * 1024) == payload
                result = exit_status(policy) if matches else 2
                if not matches:
                    print("error: policy bytes do not replay from the protocol and ledger", file=sys.stderr)
            print(f"{policy['status']} aa-policy-sha256={digest}")
        else:
            parser.error("select template, evaluate, verify or --self-test")
    except (ValueError, NoiseError, QualificationError, OSError, RecursionError) as error:
        print(f"error: {error}", file=sys.stderr)
        result = 2
    return result


if __name__ == "__main__":
    raise SystemExit(main())
