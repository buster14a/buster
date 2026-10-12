#!/usr/bin/env python3
"""Informative native macOS/Windows timing on GitHub-hosted runners (#2761).

Zen 5 measurements do not establish macOS or Windows performance, and the
approved 9700X cannot run either. The maintainer's disposition (2026-10-11):
native timing runs on the standard hosted runners and is informative only.
A slow result never fails a check, but it must stay visible: a `::warning::`
annotation, a step-summary line, and one GitHub tracking issue per platform.
These results are never Zen 5 evidence and never a performance-validation
claim; `docs/performance-validation-v1.json` records them as
`native-informative` on route `hosted-native`.

Each platform reuses its workflow's existing paired output; no new statistic:

    macos    throughput-harness-macos.yml job `timing`: the existing
             `bench_throughput_ci` guard (summary.json). A case is a
             regression only when the guard confirmed it in both rounds;
             the reported ratio is the geometric mean of the two rounds'
             median paired ratios, as in summary.md.
    windows  issue-708-benchmark.yml job `benchmark`: each trial times the
             baseline and candidate matrix on one VM. A trial crosses when
             the candidate exceeds the baseline by the throughput guard's
             wall-time margins (15% and 2 ms); a majority of trials crossing
             is a regression, and a minority is inconclusive.

Evidence that is missing, malformed, partial or failed is a real failure
(exit 2): only a complete measurement can be reported. Exit 0 covers every
complete verdict, a regression included. `--publish-issue` opens or comments
on the platform's tracking issue; the workflows run it only in a separate
job on a trusted `main` schedule or dispatch run, never for pull-request code
and never for a `--demonstrate` run.

Map: analyze_macos, analyze_windows, render, publish_issue, main.
"""

from __future__ import annotations

import argparse
import json
import math
import os
import re
import sys
import urllib.parse
import urllib.request
from pathlib import Path

SCHEMA = "buster-native-hosted-timing-v1"
PLATFORMS = {
    "macos": {"name": "macOS", "workflow": "throughput-harness-macos.yml", "job": "timing"},
    "windows": {"name": "Windows", "workflow": "issue-708-benchmark.yml", "job": "benchmark"},
}
# The throughput guard's practical wall-time margins (tools/throughput/README.md).
WALL_RELATIVE_MARGIN = 0.15
WALL_ABSOLUTE_SECONDS = 0.002
GUARD_METRICS = ("wall_seconds", "peak_rss_bytes")
GUARD_DECISIONS = ("regression", "inconclusive", "no substantial regression detected")
WINDOWS_RESULT_SCHEMA = "ISSUE_708_PAIRED_RESULT_V1"
SHA = re.compile(r"[0-9a-f]{40}")
CELL = re.compile(r"[A-Za-z0-9_.:+/-]{1,127}")
RUN_URL = re.compile(r"https://github\.com/[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+/actions/runs/[1-9][0-9]*/attempts/[1-9][0-9]*")
REPOSITORY = re.compile(r"[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+")
ISSUE_AUTHOR = "github-actions[bot]"
ISSUE_PAGES = 20
API_TIMEOUT_SECONDS = 30
NOTICE = ("Informative only: GitHub-hosted runner timing, not Zen 5 evidence and never a performance-validation "
          "claim or a failed check (#2761).")


class EvidenceError(ValueError):
    """Incomplete or invalid measurement: the run fails instead of reporting."""


def _finite(value: object) -> bool:
    return isinstance(value, (int, float)) and not isinstance(value, bool) and math.isfinite(value)


def _sha(value: str, label: str) -> str:
    if not isinstance(value, str) or not SHA.fullmatch(value):
        raise EvidenceError(f"{label} must be a full lowercase commit SHA")
    return value


def _report(platform: str, baseline: str, candidate: str, cells: list[dict], demonstration: bool) -> dict:
    regressions = [cell for cell in cells if cell["regression"]]
    inconclusive = [cell for cell in cells if cell.get("inconclusive")]
    verdict = "regression" if regressions else "inconclusive" if inconclusive else "no regression"
    return {"schema": SCHEMA, "platform": platform, "workflow": PLATFORMS[platform]["workflow"],
            "baseline": baseline, "candidate": candidate, "verdict": verdict,
            "demonstration": demonstration, "cells": cells}


def analyze_macos(summary: object, driver_exit: int, baseline: str, candidate: str, demonstrate: bool = False) -> dict:
    """Classify a `bench_throughput_ci` summary.json and the driver's exit status."""
    baseline, candidate = _sha(baseline, "baseline"), _sha(candidate, "candidate")
    comparisons = summary.get("comparisons") if isinstance(summary, dict) else None
    if not isinstance(summary, dict) or summary.get("valid") is not True or summary.get("guard_enabled") is not True or \
            not isinstance(comparisons, list) or not comparisons:
        raise EvidenceError("summary.json is missing, invalid, unguarded or empty; no timing verdict")
    cells = []
    for comparison in comparisons:
        name = comparison.get("name") if isinstance(comparison, dict) else None
        tests = comparison.get("tests") if isinstance(comparison, dict) else None
        decision = comparison.get("decision") if isinstance(comparison, dict) else None
        if not isinstance(name, str) or not CELL.fullmatch(name) or decision not in GUARD_DECISIONS or not isinstance(tests, list):
            raise EvidenceError("summary.json has a malformed comparison")
        rounds = {}
        for test in tests:
            key = (test.get("metric"), test.get("round")) if isinstance(test, dict) else None
            if key is None or key[0] not in GUARD_METRICS or key[1] not in (0, 1) or key in rounds or \
                    not _finite(test.get("median_ratio")) or test["median_ratio"] <= 0 or \
                    not isinstance(test.get("regression"), bool):
                raise EvidenceError(f"{name}: malformed guard test")
            rounds[key] = test
        if len(rounds) != len(GUARD_METRICS) * 2:
            raise EvidenceError(f"{name}: both rounds of both guarded metrics are required")
        confirmed = [metric for metric in GUARD_METRICS if rounds[(metric, 0)]["regression"] and rounds[(metric, 1)]["regression"]]
        if (decision == "regression") != bool(confirmed):
            raise EvidenceError(f"{name}: decision {decision!r} disagrees with its per-round results")
        metric = confirmed[0] if confirmed else "wall_seconds"
        ratio = math.sqrt(rounds[(metric, 0)]["median_ratio"] * rounds[(metric, 1)]["median_ratio"])
        cells.append({"cell": name, "metric": metric, "ratio": ratio, "regression": bool(confirmed),
                      "inconclusive": decision == "inconclusive"})
    counted = sum(cell["regression"] for cell in cells)
    if summary.get("confirmed_regressions") != counted or \
            summary.get("inconclusive_cases") != sum(cell["inconclusive"] for cell in cells):
        raise EvidenceError("summary.json totals disagree with its comparisons")
    # The guard exits 1 exactly for a complete report with confirmed
    # regressions; any other nonzero status is a real failure.
    if driver_exit != (1 if counted else 0):
        raise EvidenceError(f"bench_throughput_ci exited {driver_exit} with {counted} confirmed regressions")
    if demonstrate:
        forced = max(cells, key=lambda cell: cell["ratio"] if cell["metric"] == "wall_seconds" else 0.0)
        forced.update(regression=True, inconclusive=False, metric="wall_seconds", demonstration=True)
    return _report("macos", baseline, candidate, cells, demonstrate)


def _read_json(path: Path) -> object:
    """Windows PowerShell's Tee-Object writes UTF-16 with a byte-order mark."""
    data = path.read_bytes()
    text = data.decode("utf-16") if data[:2] in (b"\xff\xfe", b"\xfe\xff") else data.decode("utf-8-sig")
    return json.loads(text)


def analyze_windows(evidence: Path, baseline: str, candidate: str, run_id: str, attempt: str,
                    trials: int, demonstrate: bool = False) -> dict:
    """Pair each trial's baseline and candidate matrix durations from the retained artifacts."""
    baseline, candidate = _sha(baseline, "baseline"), _sha(candidate, "candidate")
    if not re.fullmatch(r"[1-9][0-9]*", run_id or "") or not re.fullmatch(r"[1-9][0-9]*", attempt or "") or trials < 1:
        raise EvidenceError("run id, attempt and trial count are required")
    cells = []
    for trial in range(1, trials + 1):
        root = evidence / f"issue-708-pair-{trial}-{run_id}-{attempt}"
        if not (root / "pair-validation.txt").is_file():
            raise EvidenceError(f"trial {trial}: no pair validation; the pair did not complete")
        durations = {}
        for arm, expected in (("baseline", baseline), ("candidate", candidate)):
            try:
                result = _read_json(root / arm / "buster-ci" / "benchmark-result.json")
            except (OSError, ValueError, UnicodeError) as error:
                raise EvidenceError(f"trial {trial} {arm}: unreadable result: {error}") from None
            if not isinstance(result, dict) or result.get("schema") != WINDOWS_RESULT_SCHEMA or result.get("arm") != arm or \
                    result.get("trial") != trial or result.get("source_sha") != expected or result.get("expected_sha") != expected:
                raise EvidenceError(f"trial {trial} {arm}: result does not identify this arm, trial and revision")
            if result.get("exit_code") != 0 or result.get("summary_exit_code") != 0:
                raise EvidenceError(f"trial {trial} {arm}: the matrix or its coverage summary failed")
            if not _finite(result.get("duration_seconds")) or result["duration_seconds"] <= 0:
                raise EvidenceError(f"trial {trial} {arm}: no measured duration")
            durations[arm] = float(result["duration_seconds"])
        if demonstrate:
            durations["candidate"] *= 2.0
        ratio = durations["candidate"] / durations["baseline"]
        crossed = ratio > 1.0 + WALL_RELATIVE_MARGIN and durations["candidate"] - durations["baseline"] > WALL_ABSOLUTE_SECONDS
        cells.append({"cell": f"test_all_combinations_ci trial {trial}", "metric": "wall_seconds", "ratio": ratio,
                      "baseline_seconds": durations["baseline"], "candidate_seconds": durations["candidate"],
                      "crossed": crossed, "regression": False, "inconclusive": False})
    crossed = sum(cell["crossed"] for cell in cells)
    for cell in cells:
        cell["regression"] = cell["crossed"] and 2 * crossed > trials
        cell["inconclusive"] = cell["crossed"] and 2 * crossed <= trials
    return _report("windows", baseline, candidate, cells, demonstrate)


def render(report: dict, run_url: str = "") -> str:
    """Markdown for the step summary and the tracking issue."""
    platform = PLATFORMS[report["platform"]]
    lines = [f"Native {platform['name']} hosted timing (`{platform['workflow']}`): **{report['verdict']}**"
             + (" (DEMONSTRATION: a forced regression, not a measurement)" if report["demonstration"] else "") + ".", ""]
    if run_url:
        lines.append(f"Run: {run_url}")
    lines += [f"Commit: `{report['candidate']}` against baseline `{report['baseline']}`.", "",
              "| Cell | Metric | Candidate/baseline | Regression |", "| --- | --- | ---: | --- |"]
    for cell in report["cells"]:
        state = "yes" if cell["regression"] else "inconclusive" if cell["inconclusive"] else "no"
        lines.append(f"| {cell['cell']} | {cell['metric']} | {cell['ratio']:.4f} | {state} |")
    lines += ["", NOTICE, ""]
    return "\n".join(lines)


def annotations(report: dict) -> list[str]:
    platform = PLATFORMS[report["platform"]]["name"]
    prefix = "DEMONSTRATION " if report["demonstration"] else ""
    result = []
    for cell in report["cells"]:
        if cell["regression"]:
            result.append(f"::warning title={prefix}Native {platform} timing regression (informative)::"
                          f"{cell['cell']} {cell['metric']} candidate/baseline {cell['ratio']:.4f} on a GitHub-hosted runner; "
                          "not Zen 5 evidence and never a gate.")
    if report["verdict"] == "inconclusive":
        result.append(f"::warning title=Native {platform} timing inconclusive (informative)::"
                      "Some cells crossed the margin without confirmation; no equivalence claim.")
    return result


def _api(method: str, url: str, token: str, payload: dict | None = None) -> object:
    headers = {"Accept": "application/vnd.github+json", "X-GitHub-Api-Version": "2022-11-28",
               "User-Agent": "buster-native-timing", "Authorization": "Bearer " + token}
    data = None
    if payload is not None:
        headers["Content-Type"] = "application/json"
        data = json.dumps(payload).encode("utf-8")
    request = urllib.request.Request(url, method=method, data=data, headers=headers)
    with urllib.request.urlopen(request, timeout=API_TIMEOUT_SECONDS) as response:
        result = json.load(response)
    return result


def issue_title(platform: str) -> str:
    return f"Native hosted timing regression: {PLATFORMS[platform]['name']} ({PLATFORMS[platform]['workflow']})"


def issue_marker(platform: str) -> str:
    return f"<!-- native-hosted-timing platform={platform} -->"


def publish_issue(report: dict, repository: str, run_url: str, token: str, api=_api) -> str:
    """Open the platform's tracking issue, or comment on the open one."""
    if report["demonstration"]:
        raise EvidenceError("a demonstration never files or updates an issue")
    if not REPOSITORY.fullmatch(repository or "") or not RUN_URL.fullmatch(run_url or ""):
        raise EvidenceError("publishing needs an owner/name repository and an exact run attempt URL")
    if not token:
        raise EvidenceError("publishing the tracking issue requires GH_TOKEN")
    action = "none"
    if report["verdict"] == "regression":
        base = f"https://api.github.com/repos/{repository}/issues"
        title, marker = issue_title(report["platform"]), issue_marker(report["platform"])
        existing = []
        page, ended = 1, False
        while not ended:
            if page > ISSUE_PAGES:
                raise EvidenceError("too many open issues to find the tracking issue")
            query = urllib.parse.urlencode({"state": "open", "creator": ISSUE_AUTHOR, "per_page": 100, "page": page})
            batch = api("GET", f"{base}?{query}", token)
            if not isinstance(batch, list):
                raise EvidenceError("malformed issue listing")
            # The author is rechecked so an ignored filter cannot adopt another issue.
            existing += [issue for issue in batch if "pull_request" not in issue and issue.get("title") == title and
                         (issue.get("user") or {}).get("login") == ISSUE_AUTHOR and marker in (issue.get("body") or "")]
            ended = len(batch) < 100
            page += 1
        text = render(report, run_url)
        if existing:
            number = min(issue["number"] for issue in existing)
            api("POST", f"{base}/{number}/comments", token, {"body": text})
            action = f"updated #{number}"
        else:
            created = api("POST", base, token, {"title": title, "body": (
                f"{marker}\nOne tracking issue per platform: each later regression on a trusted `main` run adds a "
                "comment. Inspect the linked run's artifacts; close this issue once the regression is explained "
                "or fixed. See docs/agents/benchmarking.md#native-hosted-timing.\n\n" + text)})
            action = f"opened #{created['number']}"
    return action


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    sub = parser.add_subparsers(dest="platform", required=True)
    for name in PLATFORMS:
        command = sub.add_parser(name)
        command.add_argument("--baseline", required=True)
        command.add_argument("--candidate", required=True)
        command.add_argument("--demonstrate", action="store_true",
                             help="force a regression through the alert path; never files an issue")
        command.add_argument("--annotate", action="store_true", help="print ::warning:: annotations")
        command.add_argument("--step-summary", help="append the Markdown report here")
        command.add_argument("--github-output", help="append verdict=... here")
        command.add_argument("--report", help="write the JSON report here")
        command.add_argument("--publish-issue", action="store_true")
        command.add_argument("--repository", default="")
        command.add_argument("--run-url", default="")
        if name == "macos":
            command.add_argument("--summary", required=True, type=Path)
            command.add_argument("--driver-exit", required=True, type=Path)
        else:
            command.add_argument("--evidence", required=True, type=Path)
            command.add_argument("--run-id", required=True)
            command.add_argument("--attempt", required=True)
            command.add_argument("--trials", required=True, type=int)
    args = parser.parse_args(argv)
    status = 0
    try:
        if args.platform == "macos":
            try:
                summary = json.loads(args.summary.read_text(encoding="utf-8"))
                driver_exit = int(args.driver_exit.read_text(encoding="utf-8").strip())
            except (OSError, ValueError) as error:
                raise EvidenceError(f"no complete comparison: {error}") from None
            report = analyze_macos(summary, driver_exit, args.baseline, args.candidate, args.demonstrate)
        else:
            report = analyze_windows(args.evidence, args.baseline, args.candidate, args.run_id, args.attempt,
                                     args.trials, args.demonstrate)
        text = render(report, args.run_url if RUN_URL.fullmatch(args.run_url) else "")
        if args.publish_issue:
            action = publish_issue(report, args.repository, args.run_url, os.getenv("GH_TOKEN", ""))
            text += f"\nTracking issue: {action}\n"
        elif args.annotate:
            for line in annotations(report):
                print(line)
        print(text)
        if args.step_summary:
            with open(args.step_summary, "a", encoding="utf-8") as stream:
                stream.write(text)
        if args.github_output:
            with open(args.github_output, "a", encoding="utf-8") as stream:
                stream.write(f"verdict={report['verdict']}\n")
        if args.report:
            Path(args.report).write_text(json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    except EvidenceError as error:
        print(f"::error title=Native {PLATFORMS[args.platform]['name']} timing incomplete::{error}")
        status = 2
    return status


if __name__ == "__main__":
    sys.exit(main())
