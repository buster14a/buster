#!/usr/bin/env python3
"""Shared contract of the 9700X compiler comparison (#2752, #2769, #2761).

Ownership: `tools/bench_direct`, trusted `main` only. The host harness
(`compiler_compare.py`) writes a receipt; the hosted publisher
(`compiler_publish.py`) re-validates it as data and publishes one exact-head
check run. Both share the names and rules below. Nothing gates merging on
these checks: they are report-only evidence.

Two modes measure the same way and differ only in what they compare:
    main   a commit after it landed on main against its first parent, the
           main commit it landed on, or, when that commit has no valid
           measurement, the nearest first-parent ancestor with one, so a
           merge burst's unmeasured commits are inside a measured range
           (#2752); merging never waits for it
    pull   an owner pull request head against its merge base with the base
           branch, on request and without merging (#2769)
Each mode publishes its own check name and marker.

Map (searchable symbols):
    RECEIPT_SCHEMA, LAB_SCHEMA, MODES, check_name, check_marker   identities
    attempt_marker                                         one attempt's check (#2803)
    PROFILE                                                frozen profile
    APPROVED_HOST, observed_cpu_model, host_problem        observed Zen 5 host
    MEASURED_OUTCOMES, MIN_PAIRS, classify                 core validity
    REGRESSION_POLICIES, regression_policy                 report-only switch
    range_label                                            main baseline relation
    render                                                 readable report
"""

from __future__ import annotations

import json
import re
from pathlib import Path

RECEIPT_SCHEMA = "buster-9700x-compiler-receipt-v1"
LAB_SCHEMA = "buster-uarch-lab-compare-v2"
# mode: (check name, external-ID marker prefix).
MODES = {
    "main": ("9700X compiler benchmark", "buster-9700x-compiler-main-v1"),
    "pull": ("9700X compiler benchmark (pull request)", "buster-9700x-compiler-pr-v1"),
}
CHECK_NAME, MARKER = MODES["main"]
# The approved Zen 5 host (#2761): the observed CPU model, never a runner
# label or target flag, must name the Ryzen 7 9700X.
APPROVED_HOST = re.compile(r"AMD Ryzen 7 9700X\b")
SHA = re.compile(r"[0-9a-f]{40}\Z")
SHA256 = re.compile(r"[0-9a-f]{64}\Z")
DECIMAL = re.compile(r"[1-9][0-9]*\Z")

# The routine comparison of both modes. It is frozen per profile name:
# changing any value needs a new name so receipts stay comparable. Deeper
# diagnosis (top-down, sampling, IBS) stays a separate, manually requested
# lab run.
PROFILE = {
    "name": "compiler-compare-v1",
    "workload": "uarch_lab compare default: stage-1 self-host compile of the unity "
                "src/buster/apps/ide/ide.c on the configured base tree",
    "frozen_source": "the base revision: a main commit's first parent or a pull request's merge base",
    "build": "./build.sh generate --cc clang --no-include-tests; ./build.sh build --config Release -t ide",
    "cpu": 2,
    "target_minutes": 10,
    "warmups": 1,
    "profile_steps": [],
    "corpus": "not included in compiler-compare-v1",
}
# A wall-time CI needs at least six complete pairs (uarch_lab sign_test_rank).
MIN_PAIRS = 6
# Every complete verdict counts, whatever its direction; "inconclusive" means
# too few pairs and "NO VERDICT" no pair at all, so neither is a measurement.
MEASURED_OUTCOMES = ("faster", "slower", "below-floor", "no detectable difference")
# Only report-only exists. "enforce" is reserved for a separately reviewed
# rollout with qualified A/A noise and thresholds; until then it fails closed.
REGRESSION_POLICIES = ("report-only",)


IDENTITY_KEYS = ("mode", "repository", "ref", "pull", "pull_head", "base", "base_tree", "head", "head_tree",
                 "trusted_revision", "request_run_id", "run_id", "run_attempt")


def check_name(mode: str = "main") -> str:
    return MODES[mode][0]


def check_marker(head: str, mode: str = "main") -> str:
    if not (isinstance(head, str) and SHA.fullmatch(head)):
        raise ValueError("check marker needs an exact 40-hex head")
    return MODES[mode][1] + ":" + head


def attempt_marker(head: str, mode: str, request_run_id: str, request_attempt: str, run_attempt: str) -> str:
    """External ID of one measurement attempt's check (#2803).

    The request run and its attempt name the scheduling, the bench attempt the
    measurement, so a transport retry finds the same check while a deliberate
    re-run (of either workflow) gets its own. The external ID is only a lookup
    key; ownership also needs the GitHub Actions app, the name and the head.
    """
    for value in (request_run_id, request_attempt, run_attempt):
        if not (isinstance(value, str) and DECIMAL.fullmatch(value)):
            raise ValueError("attempt marker needs decimal run and attempt numbers")
    return f"{check_marker(head, mode)}:{request_run_id}.{request_attempt}:{run_attempt}"


def observed_cpu_model() -> str:
    """The running host's CPU model name from /proc/cpuinfo, or 'NA'."""
    model = "NA"
    try:
        for line in Path("/proc/cpuinfo").read_text(encoding="utf-8", errors="replace").splitlines():
            if line.startswith("model name"):
                model = line.split(":", 1)[1].strip()
                break
    except OSError:
        pass
    return model


def host_problem(receipt: object) -> str:
    """Why the receipt's observed host is not the approved Zen 5 host, or ''."""
    host = receipt.get("host") if isinstance(receipt, dict) else None
    model = host.get("cpu_model") if isinstance(host, dict) else None
    problem = ""
    if not (isinstance(model, str) and APPROVED_HOST.search(model)):
        problem = f"observed CPU {model!r} is not the approved Zen 5 host (AMD Ryzen 7 9700X)"
    return problem


def regression_policy(value: str) -> tuple[str, str]:
    """(policy, problem): an unset variable is report-only; anything else unknown fails."""
    policy = value.strip() if isinstance(value, str) else ""
    policy = policy or "report-only"
    problem = ""
    if policy not in REGRESSION_POLICIES:
        problem = (f"regression policy {policy!r} is not implemented; only report-only exists until "
                   "regression thresholds are qualified (#2752)")
    return policy, problem


def range_label(commits: object, first_parent: object) -> str:
    """A main baseline's relation to the head, from the authorized range size, or '' when unknown.

    commits counts the first-parent main commits the comparison spans: 1 is
    the head alone against its first parent.
    """
    label = ""
    if commits == "1":
        label = "first parent"
    elif isinstance(commits, str) and DECIMAL.fullmatch(commits) and isinstance(first_parent, str) and \
            SHA.fullmatch(first_parent):
        label = (f"range of {commits} first-parent main commits: the nearest earlier main commit with a valid "
                 f"measurement, because first parent `{first_parent}` has none; the result covers the whole range "
                 "and does not isolate one commit")
    return label


def classify(summary: object, binaries: object) -> list[str]:
    """Reasons the lab summary is not a valid core measurement; empty when valid."""
    reasons: list[str] = []
    if not isinstance(summary, dict):
        summary = {}
        reasons.append("summary.json is missing or not an object")
    if not isinstance(binaries, dict):
        binaries = {}
    if summary and summary.get("schema") != LAB_SCHEMA:
        reasons.append(f"lab schema {summary.get('schema')!r} is not {LAB_SCHEMA}")
    for role in ("baseline", "candidate"):
        variant = summary.get(role) if isinstance(summary.get(role), dict) else {}
        recorded = binaries.get(role) if isinstance(binaries.get(role), dict) else {}
        digest = recorded.get("sha256")
        if not (isinstance(digest, str) and SHA256.fullmatch(digest)) or variant.get("sha256") != digest:
            reasons.append(f"{role} binary hash does not match the built binary")
        if variant.get("failed") != 0 or type(variant.get("runs")) is not int or variant.get("runs", 0) < 1:
            reasons.append(f"{role} has failed or missing timed runs")
        if variant.get("deterministic") is not True:
            reasons.append(f"{role} output was not byte-identical across its own runs")
    plan = summary.get("plan") if isinstance(summary.get("plan"), dict) else {}
    pairs = plan.get("complete_pairs")
    if type(pairs) is not int or pairs < MIN_PAIRS:
        reasons.append(f"{pairs!r} complete pairs; at least {MIN_PAIRS} are required")
    verdict = summary.get("verdict") if isinstance(summary.get("verdict"), dict) else {}
    if verdict.get("metric") != "wall" or verdict.get("outcome") not in MEASURED_OUTCOMES:
        reasons.append(f"wall-time verdict {verdict.get('outcome')!r} is not a complete measurement")
    for key in ("ratio", "ci_low", "ci_high"):
        if not isinstance(verdict.get(key), (int, float)) or isinstance(verdict.get(key), bool):
            reasons.append(f"wall-time verdict has no numeric {key}")
    return reasons


def number(value: object, form: str) -> str:
    return form % value if isinstance(value, (int, float)) and not isinstance(value, bool) else "NA"


def render(receipt: dict, summary: object, conclusion: str, notes: list[str]) -> str:
    """Readable per-candidate report: identities, verdict, core metrics and timings."""
    identity = receipt.get("identity", {}) if isinstance(receipt, dict) else {}
    timings = receipt.get("timings", {}) if isinstance(receipt, dict) else {}
    summary = summary if isinstance(summary, dict) else {}
    verdict = summary.get("verdict") if isinstance(summary.get("verdict"), dict) else {}
    mode = receipt.get("mode") if isinstance(receipt, dict) and receipt.get("mode") in MODES else "main"
    lines = [
        f"**{check_name(mode)}: {conclusion}** (performance policy: report-only; a slow result does not block)",
        "",
        verdict.get("text") or "No wall-time verdict.",
        "",
        "| Identity | Value |",
        "| --- | --- |",
    ]
    for key in IDENTITY_KEYS:
        lines.append(f"| {key} | `{identity.get(key, 'NA')}` |")
    coverage = receipt.get("coverage") if isinstance(receipt, dict) else None
    label = range_label(coverage.get("range"), coverage.get("first_parent")) if isinstance(coverage, dict) else ""
    if mode == "main" and label:
        lines += ["", f"Baseline: {label}."]
    profile = receipt.get("profile", {}) if isinstance(receipt, dict) else {}
    host = receipt.get("host", {}) if isinstance(receipt, dict) else {}
    lines += ["", f"Profile `{profile.get('name', 'NA')}`: {profile.get('workload', 'NA')}.",
              f"Observed host: `{host.get('cpu_model', 'NA') if isinstance(host, dict) else 'NA'}`.", ""]
    metrics = summary.get("metrics") if isinstance(summary.get("metrics"), dict) else {}
    if metrics:
        lines += ["| Metric | A median | B median | B/A | 95% CI | Outcome |", "| --- | --- | --- | --- | --- | --- |"]
        for key in ("wall", "task_clock", "instructions", "cycles", "branch_misses", "page_faults", "peak_rss"):
            row = metrics.get(key) if isinstance(metrics.get(key), dict) else {}
            lines.append("| %s | %s | %s | %s | [%s, %s] | %s |" % (
                key, number(row.get("a_median"), "%.6g"), number(row.get("b_median"), "%.6g"),
                number(row.get("ratio"), "%.4f"), number(row.get("ci_low"), "%.4f"),
                number(row.get("ci_high"), "%.4f"), row.get("outcome", "NA")))
        lines.append("")
    timings = timings if isinstance(timings, dict) else {}
    builds = timings.get("build_seconds") if isinstance(timings.get("build_seconds"), dict) else {}
    lines.append("Host time: builds %s s, measurement %s s, total %s s; queue delay before the host job %s s." % (
        " + ".join(number(builds.get(key), "%.0f") for key in ("baseline", "candidate", "closure")),
        number(timings.get("measurement_seconds"), "%.0f"), number(timings.get("total_seconds"), "%.0f"),
        number(timings.get("queue_delay_seconds"), "%.0f")))
    reasons = receipt.get("reasons") if isinstance(receipt, dict) else None
    warnings = summary.get("warnings")
    for item in (*notes, *(reasons if isinstance(reasons, list) else ()),
                 *(warnings if isinstance(warnings, list) else ())):
        lines.append(f"- {item}")
    return "\n".join(lines)


def dumps(value: object) -> str:
    return json.dumps(value, sort_keys=True, indent=2)
