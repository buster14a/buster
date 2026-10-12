#!/usr/bin/env python3
"""Tests for informative native hosted timing (#2761): analysis, issue publisher and workflow pins.

The workflow tests pin the report-only contract: a regression warns and never
fails, missing or partial evidence still fails, and only a separate trusted
main job holds `issues: write` and publishes through native_timing.py.
"""

from __future__ import annotations

import contextlib
import copy
import io
import json
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import native_timing  # noqa: E402
from performance_inventory_test import job_blocks  # noqa: E402

ROOT = Path(__file__).resolve().parents[2]
WORKFLOWS = ROOT / ".github" / "workflows"
BASE, HEAD = "a" * 40, "b" * 40
RUN_URL = "https://github.com/buster14a/buster/actions/runs/7/attempts/1"


def guard_case(name: str, wall: tuple[float, float] = (1.01, 0.99), wall_regression: tuple[bool, bool] = (False, False),
               decision: str = "no substantial regression detected") -> dict:
    tests = [{"metric": "wall_seconds", "round": index, "median_ratio": wall[index], "regression": wall_regression[index]}
             for index in (0, 1)]
    tests += [{"metric": "peak_rss_bytes", "round": index, "median_ratio": 1.0, "regression": False} for index in (0, 1)]
    return {"name": name, "tests": tests, "decision": decision}


def guard_summary(*cases: dict) -> dict:
    return {"schema": 1, "guard_enabled": True, "comparisons": list(cases), "valid": True,
            "confirmed_regressions": sum(case["decision"] == "regression" for case in cases),
            "inconclusive_cases": sum(case["decision"] == "inconclusive" for case in cases)}


REGRESSING = guard_case("macros/fast", (1.30, 1.20), (True, True), "regression")


class MacosTest(unittest.TestCase):
    def test_clean_run(self):
        report = native_timing.analyze_macos(guard_summary(guard_case("macros/fast")), 0, BASE, HEAD)
        self.assertEqual(report["verdict"], "no regression")
        self.assertEqual(native_timing.annotations(report), [])

    def test_confirmed_regression_is_reported_with_its_ratio(self):
        report = native_timing.analyze_macos(guard_summary(guard_case("ok"), REGRESSING), 1, BASE, HEAD)
        self.assertEqual(report["verdict"], "regression")
        cell = report["cells"][1]
        self.assertEqual((cell["cell"], cell["metric"]), ("macros/fast", "wall_seconds"))
        self.assertAlmostEqual(cell["ratio"], (1.30 * 1.20) ** 0.5)
        warning, = native_timing.annotations(report)
        self.assertTrue(warning.startswith("::warning title=Native macOS timing regression (informative)::macros/fast"))
        self.assertIn("not Zen 5 evidence", native_timing.render(report))

    def test_inconclusive_warns_without_regression(self):
        case = guard_case("macros/fast", (1.2, 1.0), (True, False), "inconclusive")
        report = native_timing.analyze_macos(guard_summary(case), 0, BASE, HEAD)
        self.assertEqual(report["verdict"], "inconclusive")
        self.assertIn("inconclusive", native_timing.annotations(report)[0])

    def test_incomplete_or_failed_evidence_fails(self):
        summary = guard_summary(REGRESSING)
        broken = copy.deepcopy(summary)
        broken["comparisons"][0]["tests"].pop()
        disagreeing = copy.deepcopy(summary)
        disagreeing["comparisons"][0]["decision"] = "no substantial regression detected"
        for label, value, exit_code in (
            ("invalid bundle", dict(summary, valid=False), 1),
            ("guard disabled", dict(summary, guard_enabled=False), 1),
            ("no comparisons", dict(summary, comparisons=[]), 0),
            ("missing round", broken, 1),
            ("decision contradicts rounds", disagreeing, 1),
            ("totals disagree", dict(summary, confirmed_regressions=0), 1),
            ("crash with a regression report", summary, 2),
            ("success exit with a regression", summary, 0),
            ("failure exit without a regression", guard_summary(guard_case("ok")), 1),
            ("not a summary", [], 0),
        ):
            with self.subTest(label), self.assertRaises(native_timing.EvidenceError):
                native_timing.analyze_macos(value, exit_code, BASE, HEAD)
        with self.assertRaises(native_timing.EvidenceError):
            native_timing.analyze_macos(guard_summary(guard_case("ok")), 0, "main", HEAD)

    def test_demonstration_forces_the_warning_path(self):
        report = native_timing.analyze_macos(guard_summary(guard_case("a", (1.0, 1.0)), guard_case("b", (1.1, 1.0))),
                                             0, BASE, HEAD, demonstrate=True)
        self.assertEqual((report["verdict"], report["demonstration"]), ("regression", True))
        self.assertEqual([cell["cell"] for cell in report["cells"] if cell["regression"]], ["b"])
        self.assertTrue(native_timing.annotations(report)[0].startswith("::warning title=DEMONSTRATION "))
        self.assertIn("DEMONSTRATION", native_timing.render(report))


class WindowsTest(unittest.TestCase):
    def evidence(self, root: Path, durations: dict[int, tuple[float, float]], utf16: bool = True,
                 override: dict | None = None) -> None:
        override = override or {}
        for trial, (base, head) in durations.items():
            pair = root / f"issue-708-pair-{trial}-7-1"
            for arm, sha, seconds in (("baseline", BASE, base), ("candidate", HEAD, head)):
                result = {"schema": "ISSUE_708_PAIRED_RESULT_V1", "source_sha": sha, "expected_sha": sha, "arm": arm,
                          "trial": trial, "duration_seconds": seconds, "exit_code": 0, "summary_exit_code": 0}
                result.update(override.get((trial, arm), {}))
                path = pair / arm / "buster-ci" / "benchmark-result.json"
                path.parent.mkdir(parents=True, exist_ok=True)
                text = json.dumps(result, indent=2)
                path.write_bytes(("﻿" + text).encode("utf-16-le") if utf16 else text.encode("utf-8"))
            (pair / "pair-validation.txt").write_text("Both arms succeeded.\n", encoding="utf-8")

    def analyze(self, durations, demonstrate=False, override=None) -> dict:
        with tempfile.TemporaryDirectory() as directory:
            self.evidence(Path(directory), durations, override=override)
            return native_timing.analyze_windows(Path(directory), BASE, HEAD, "7", "1", 3, demonstrate)

    def test_majority_of_trials_crossing_is_a_regression(self):
        report = self.analyze({1: (600.0, 720.0), 2: (600.0, 700.0), 3: (600.0, 610.0)})
        self.assertEqual(report["verdict"], "regression")
        self.assertEqual([cell["regression"] for cell in report["cells"]], [True, True, False])
        self.assertAlmostEqual(report["cells"][0]["ratio"], 1.2)
        self.assertIn("test_all_combinations_ci trial 1", native_timing.annotations(report)[0])

    def test_minority_is_inconclusive_and_margin_is_strict(self):
        self.assertEqual(self.analyze({1: (600.0, 720.0), 2: (600.0, 600.0), 3: (600.0, 690.0)})["verdict"], "inconclusive")
        self.assertEqual(self.analyze({1: (600.0, 600.0), 2: (600.0, 590.0), 3: (600.0, 605.0)})["verdict"], "no regression")

    def test_utf8_results_are_read_too(self):
        with tempfile.TemporaryDirectory() as directory:
            self.evidence(Path(directory), {1: (1.0, 1.0), 2: (1.0, 1.0), 3: (1.0, 1.0)}, utf16=False)
            report = native_timing.analyze_windows(Path(directory), BASE, HEAD, "7", "1", 3)
        self.assertEqual(report["verdict"], "no regression")

    def test_demonstration_doubles_the_candidate(self):
        report = self.analyze({1: (600.0, 600.0), 2: (600.0, 600.0), 3: (600.0, 600.0)}, demonstrate=True)
        self.assertEqual((report["verdict"], report["demonstration"]), ("regression", True))

    def test_incomplete_or_failed_evidence_fails(self):
        good = {1: (600.0, 600.0), 2: (600.0, 600.0), 3: (600.0, 600.0)}
        for label, durations, override in (
            ("missing trial", {1: (1.0, 1.0), 2: (1.0, 1.0)}, {}),
            ("failed matrix", good, {(2, "candidate"): {"exit_code": 1}}),
            ("failed coverage summary", good, {(1, "baseline"): {"summary_exit_code": 1}}),
            ("stale revision", good, {(3, "candidate"): {"source_sha": BASE}}),
            ("swapped arm", good, {(3, "candidate"): {"arm": "baseline"}}),
            ("wrong trial", good, {(3, "baseline"): {"trial": 1}}),
            ("no duration", good, {(1, "candidate"): {"duration_seconds": None}}),
            ("zero duration", good, {(1, "baseline"): {"duration_seconds": 0}}),
            ("other schema", good, {(1, "baseline"): {"schema": "OTHER"}}),
        ):
            with self.subTest(label), self.assertRaises(native_timing.EvidenceError):
                self.analyze(durations, override=override)
        with tempfile.TemporaryDirectory() as directory:
            self.evidence(Path(directory), good)
            (Path(directory) / "issue-708-pair-2-7-1" / "pair-validation.txt").unlink()
            with self.assertRaises(native_timing.EvidenceError):
                native_timing.analyze_windows(Path(directory), BASE, HEAD, "7", "1", 3)


class FakeApi:
    def __init__(self, issues: list[dict]):
        self.issues, self.calls = issues, []

    def __call__(self, method, url, token, payload=None):
        self.calls.append((method, url, payload))
        result = {"number": 99}
        if method == "GET":
            result = self.issues
        return result


class PublishTest(unittest.TestCase):
    def report(self, **change) -> dict:
        return dict(native_timing.analyze_macos(guard_summary(REGRESSING), 1, BASE, HEAD), **change)

    def test_opens_one_issue_per_platform(self):
        api = FakeApi([])
        self.assertEqual(native_timing.publish_issue(self.report(), "buster14a/buster", RUN_URL, "t", api), "opened #99")
        method, url, payload = api.calls[-1]
        self.assertEqual((method, url), ("POST", "https://api.github.com/repos/buster14a/buster/issues"))
        self.assertEqual(payload["title"], "Native hosted timing regression: macOS (throughput-harness-macos.yml)")
        for text in ("<!-- native-hosted-timing platform=macos -->", RUN_URL, HEAD, "macros/fast", "1.2490"):
            self.assertIn(text, payload["body"])
        self.assertIn("creator=github-actions%5Bbot%5D", api.calls[0][1])

    def test_comments_on_the_existing_issue_only(self):
        title = native_timing.issue_title("macos")
        marker = native_timing.issue_marker("macos")
        bot = {"login": "github-actions[bot]"}
        api = FakeApi([
            {"number": 5, "title": title, "body": marker, "user": {"login": "someone"}},
            {"number": 6, "title": title, "body": "no marker", "user": bot},
            {"number": 7, "title": native_timing.issue_title("windows"), "body": marker, "user": bot},
            {"number": 8, "title": title, "body": marker, "user": bot, "pull_request": {}},
            {"number": 12, "title": title, "body": marker, "user": bot},
            {"number": 11, "title": title, "body": marker, "user": bot},
        ])
        self.assertEqual(native_timing.publish_issue(self.report(), "buster14a/buster", RUN_URL, "t", api), "updated #11")
        self.assertEqual(api.calls[-1][:2], ("POST", "https://api.github.com/repos/buster14a/buster/issues/11/comments"))

    def test_refusals(self):
        api = FakeApi([])
        clean = native_timing.analyze_macos(guard_summary(guard_case("ok")), 0, BASE, HEAD)
        self.assertEqual(native_timing.publish_issue(clean, "buster14a/buster", RUN_URL, "t", api), "none")
        self.assertEqual(api.calls, [])
        for label, report, repository, url, token in (
            ("demonstration", self.report(demonstration=True), "buster14a/buster", RUN_URL, "t"),
            ("no token", self.report(), "buster14a/buster", RUN_URL, ""),
            ("bad repository", self.report(), "buster14a", RUN_URL, "t"),
            ("run without attempt", self.report(), "buster14a/buster", RUN_URL.rsplit("/attempts", 1)[0], "t"),
        ):
            with self.subTest(label), self.assertRaises(native_timing.EvidenceError):
                native_timing.publish_issue(report, repository, url, token, api)
        self.assertEqual(api.calls, [])


class CommandTest(unittest.TestCase):
    def run_macos(self, summary: object, exit_code: str, *extra: str) -> tuple[int, str, Path]:
        directory = Path(tempfile.mkdtemp())
        (directory / "summary.json").write_text(json.dumps(summary), encoding="utf-8")
        (directory / "exit.txt").write_text(exit_code + "\n", encoding="utf-8")
        output = io.StringIO()
        with contextlib.redirect_stdout(output):
            status = native_timing.main(["macos", "--summary", str(directory / "summary.json"), "--driver-exit",
                                         str(directory / "exit.txt"), "--baseline", BASE, "--candidate", HEAD,
                                         "--step-summary", str(directory / "step.md"),
                                         "--github-output", str(directory / "output.txt"), *extra])
        return status, output.getvalue(), directory

    def test_regression_exits_zero_with_a_warning(self):
        status, output, directory = self.run_macos(guard_summary(REGRESSING), "1", "--annotate")
        self.assertEqual(status, 0)
        self.assertIn("::warning title=Native macOS timing regression (informative)::", output)
        self.assertIn("verdict=regression\n", (directory / "output.txt").read_text(encoding="utf-8"))
        self.assertIn("**regression**", (directory / "step.md").read_text(encoding="utf-8"))

    def test_incomplete_evidence_exits_two(self):
        status, output, _ = self.run_macos(dict(guard_summary(REGRESSING), valid=False), "1", "--annotate")
        self.assertEqual(status, 2)
        self.assertIn("::error title=Native macOS timing incomplete::", output)
        status, _, directory = self.run_macos(guard_summary(REGRESSING), "1")
        (directory / "summary.json").unlink()
        with contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(native_timing.main(["macos", "--summary", str(directory / "summary.json"), "--driver-exit",
                                                 str(directory / "exit.txt"), "--baseline", BASE, "--candidate", HEAD]), 2)

    def test_demonstration_never_publishes(self):
        status, output, _ = self.run_macos(guard_summary(guard_case("ok")), "0", "--demonstrate", "--publish-issue",
                                           "--repository", "buster14a/buster", "--run-url", RUN_URL)
        self.assertEqual(status, 2)
        self.assertIn("a demonstration never files or updates an issue", output)


class WorkflowTest(unittest.TestCase):
    """Pin the report-only timing steps and the trusted alert jobs."""

    ALERT_PERMISSIONS = ["    permissions:", "      contents: read", "      actions: read", "      issues: write"]

    def jobs(self, name: str) -> tuple[str, dict[str, list[str]]]:
        text = (WORKFLOWS / name).read_text(encoding="utf-8")
        return text, job_blocks(text)

    def check_alert(self, jobs: dict[str, list[str]], platform: str, measured: str, events: str) -> None:
        writers = [name for name, lines in jobs.items() if any("issues: write" in line or "write-all" in line for line in lines)]
        self.assertEqual(writers, ["alert"])
        alert = jobs["alert"]
        condition, = [line for line in alert if line.startswith("    if:")]
        for term in (f"needs.{measured}.outputs.verdict == 'regression'", "github.repository == 'buster14a/buster'",
                     "github.ref == 'refs/heads/main'", events, "inputs.demonstrate_regression != true"):
            self.assertIn(term, condition)
        self.assertNotIn("pull_request", condition)
        start = alert.index("    permissions:")
        self.assertEqual(alert[start:start + 4], self.ALERT_PERMISSIONS)
        self.assertNotIn("      contents: write", alert)
        self.assertIn("          ref: ${{ github.sha }}", alert)
        self.assertIn("          persist-credentials: false", alert)
        body = "\n".join(alert)
        self.assertIn(f"tools/bench_direct/native_timing.py {platform}", body)
        self.assertIn("--publish-issue", body)
        self.assertIn('gh run download "$GITHUB_RUN_ID"', body)
        self.assertNotIn("--annotate", body)

    def test_macos_timing_is_report_only_and_never_on_pull_requests(self):
        text, jobs = self.jobs("throughput-harness-macos.yml")
        timing = jobs["timing"]
        condition, = [line for line in timing if line.startswith("    if:")]
        self.assertIn("(github.event_name == 'schedule' || github.event_name == 'workflow_dispatch')", condition)
        self.assertNotIn("pull_request", condition)
        self.assertIn("    runs-on: macos-26", timing)
        self.assertFalse(any("permissions:" in line for line in timing))
        self.assertIn("permissions:\n  contents: read\n", text)
        body = "\n".join(timing)
        # The guard's exit 1 is captured, never propagated; the report step decides.
        self.assertIn("          env -u GITHUB_ACTIONS ./build/build bench_throughput_ci ../baseline || status=$?", body)
        self.assertNotIn("continue-on-error", body)
        self.assertIn("        if: ${{ !cancelled() && steps.compare.outcome == 'success' }}", timing)
        self.assertIn("python3 -B candidate/tools/bench_direct/native_timing.py macos", body)
        self.assertIn("--annotate", body)
        self.assertIn('--step-summary "$GITHUB_STEP_SUMMARY"', body)
        self.assertNotIn("|| true", body)
        self.check_alert(jobs, "macos", "timing",
                         "(github.event_name == 'schedule' || github.event_name == 'workflow_dispatch')")

    def test_windows_report_is_report_only_and_alerts_from_main_dispatch(self):
        text, jobs = self.jobs("issue-708-benchmark.yml")
        self.assertIn("    runs-on: windows-2025", jobs["benchmark"])
        self.assertFalse(any("issues:" in line for line in jobs["benchmark"]))
        report = jobs["report"]
        self.assertIn("    if: ${{ !cancelled() && needs.benchmark.result == 'success' }}", report)
        start = report.index("    permissions:")
        self.assertEqual(report[start:start + 3], ["    permissions:", "      contents: read", "      actions: read"])
        body = "\n".join(report)
        self.assertIn("python3 -B tools/bench_direct/native_timing.py windows", body)
        self.assertIn("--trials 3", body)
        self.assertIn("--annotate", body)
        self.assertNotIn("continue-on-error", body)
        self.assertNotIn("|| true", body)
        self.assertIn("        trial: [1, 2, 3]", jobs["benchmark"])
        self.check_alert(jobs, "windows", "report", "github.event_name == 'workflow_dispatch'")

    def test_macos_harness_build_keeps_cpu_auto_off_macos_only(self):
        build = (ROOT / "build.c").read_text(encoding="utf-8")
        start = build.index("BUSTER_GLOBAL_LOCAL ProcessResult bench_throughput_ci_add(")
        body = build[start:build.index("bench_throughput_add(arena", start)]
        self.assertIn('#if BUSTER_MACOS\n', body)
        self.assertIn('#else\n                S8("--cpu"), S8("auto"),\n#endif\n', body)


if __name__ == "__main__":
    unittest.main()
