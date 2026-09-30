#!/usr/bin/env python3
"""Queue/runner-assignment tests for tools/github_ci_time.py (#1805) and the
macOS runner demand of auxiliary workflows (#1825)."""
from datetime import datetime, timezone
from pathlib import Path
import re
import textwrap
import unittest
from unittest import mock
import urllib.parse

import github_ci_time

ROOT = Path(__file__).resolve().parents[1]


class QueueTimingTests(unittest.TestCase):
    """#1805: runner assignment is measured from runner identity, not placeholder timestamps."""

    def job(self, identity, created, started=None, completed=None, runner=0, labels=("macos-26",), status="completed"):
        return {"id": identity, "run_id": 1, "run_attempt": 1, "head_sha": "a" * 40, "name": f"job {identity}",
                "status": status, "conclusion": "success" if status == "completed" and runner else None,
                "created_at": created, "started_at": started, "completed_at": completed,
                "labels": list(labels), "runner_id": runner, "runner_name": "", "runner_group_name": ""}

    def data(self, jobs, observed="2026-09-29T12:00:00Z"):
        run = {"id": 1, "path": ".github/workflows/ci.yml", "event": "merge_group", "status": "in_progress",
               "conclusion": None, "created_at": "2026-09-29T08:00:00Z", "updated_at": observed,
               "selected_by": ["created"], "observed_at": observed,
               "attempts": {"1": {"run_started_at": "2026-09-29T08:00:00Z"}}, "jobs": jobs}
        return {"kind": "queue", "repository": "o/r", "fetched_at": observed,
                "window": {"since": "2026-09-29T08:00:00Z", "until": "2026-09-29T12:00:00Z"}, "runs": [run]}

    def test_assigned_job_splits_dependency_assignment_and_execution(self):
        report = github_ci_time.queue_summarize(self.data([self.job(
            1, "2026-09-29T08:01:00Z", "2026-09-29T08:11:00Z", "2026-09-29T08:16:00Z", runner=7)]))
        group = report["groups"][0]
        self.assertEqual((group["labels"], group["event"], group["family"]), ("macos-26", "merge_group", "macos"))
        self.assertEqual(group["dependency_wait_seconds"]["p50"], 60)
        self.assertEqual(group["assignment_wait_seconds"]["p50"], 600)
        self.assertEqual(group["execution_seconds"]["p50"], 300)
        self.assertEqual(group["runner_seconds"], 300)

    def test_placeholder_start_of_unassigned_job_is_not_execution(self):
        queued = self.job(2, "2026-09-29T11:00:00Z", "2026-09-29T11:00:05Z", status="queued")
        group = github_ci_time.queue_summarize(self.data([queued]))["groups"][0]
        self.assertEqual(group["assignment_wait_seconds"], {"n": 0})
        self.assertEqual(group["execution_seconds"], {"n": 0})
        self.assertEqual(group["queued_at_observation"], 1)
        self.assertEqual(group["oldest_ready"]["job_id"], 2)
        self.assertEqual(group["oldest_ready"]["age_seconds"], 3600)

    def test_occupancy_plateau_while_jobs_wait(self):
        jobs = [self.job(1, "2026-09-29T09:30:00Z", "2026-09-29T09:30:00Z", "2026-09-29T10:30:00Z", runner=1),
                self.job(2, "2026-09-29T09:30:00Z", "2026-09-29T09:30:00Z", "2026-09-29T10:30:00Z", runner=2),
                self.job(3, "2026-09-29T09:40:00Z", "2026-09-29T10:30:00Z", "2026-09-29T10:40:00Z", runner=3),
                self.job(4, "2026-09-29T09:40:00Z", "2026-09-29T09:40:00Z", "2026-09-29T09:50:00Z",
                         labels=("ubuntu-26.04",), runner=4)]
        occupancy = github_ci_time.queue_summarize(self.data(jobs))["occupancy_by_family"]
        # Measurement starts after the 90-minute warm-up; job 3 waits 09:40-10:30 with two runners busy.
        self.assertEqual(occupancy["macos"]["measured_from"], "2026-09-29T09:30:00+00:00")
        self.assertEqual(occupancy["macos"]["occupied_while_waiting_seconds"], {"2": 3000.0})
        self.assertEqual(occupancy["macos"]["max_occupied_while_waiting"], 2)
        self.assertEqual(occupancy["ubuntu"]["seconds_with_waiting_jobs"], 0)

    def test_duplicate_job_attempt_is_rejected(self):
        job = self.job(1, "2026-09-29T08:01:00Z")
        with self.assertRaises(ValueError):
            github_ci_time.queue_summarize(self.data([job, dict(job)]))

    def test_historical_listing_refuses_partial_pages_but_live_listing_records_drift(self):
        pages = [{"total_count": 3, "items": [1, 2]}]
        with mock.patch.object(github_ci_time, "api_get", side_effect=lambda *args, **kwargs: pages[0]):
            with self.assertRaises(ValueError):
                github_ci_time._complete_pages("o/r", "x", "items", None, 1000, False)
            self.assertEqual(github_ci_time._complete_pages("o/r", "x", "items", None, 1000, True), ([1, 2], 3))
        with mock.patch.object(github_ci_time, "api_get", return_value={"total_count": 1001, "items": []}):
            with self.assertRaises(ValueError):
                github_ci_time._complete_pages("o/r", "x", "items", None, 1000, True)

    def test_created_windows_split_disjointly_under_search_limit(self):
        def reply(repository, path, token):
            left, right = urllib.parse.parse_qs(urllib.parse.urlsplit(path).query)["created"][0].split("..")
            span = (github_ci_time.timestamp(right) - github_ci_time.timestamp(left)).total_seconds()
            return {"total_count": int(span)}
        since = datetime(2026, 9, 29, 8, tzinfo=timezone.utc)
        until = datetime(2026, 9, 29, 9, tzinfo=timezone.utc)
        with mock.patch.object(github_ci_time, "api_get", side_effect=reply):
            windows = github_ci_time._created_windows("o/r", since, until, None)
        self.assertEqual(windows[0][0], since)
        self.assertEqual(windows[-1][1], until)
        for (_, right), (left, _) in zip(windows, windows[1:]):
            self.assertEqual((left - right).total_seconds(), 1)
        self.assertTrue(all((right - left).total_seconds() <= 1000 for left, right in windows))


class MacosRunnerDemandTests(unittest.TestCase):
    """#1825: auxiliary workflows request macOS runners only where they add coverage."""

    WORKFLOWS = ROOT / ".github/workflows"

    @staticmethod
    def unix_harness(text):
        step = text.split("      - name: Native harness tests (Unix)\n", 1)[1].split("\n      - ", 1)[0]
        return textwrap.dedent(step.split("        run: |\n", 1)[1])

    @staticmethod
    def path_pattern(glob):
        parts = re.split(r"(\*\*|\*)", glob)
        return re.compile("".join(".*" if part == "**" else "[^/]*" if part == "*" else re.escape(part)
                                  for part in parts) + r"\Z")

    @staticmethod
    def include_closure(roots):
        """Every existing file a root can include, ignoring preprocessor conditions."""
        pending = [ROOT / root for root in roots]
        closure = set()
        while pending:
            path = pending.pop()
            relative = path.relative_to(ROOT).as_posix()
            if relative in closure:
                continue
            closure.add(relative)
            text = path.read_text(encoding="utf-8", errors="replace")
            for kind, name in re.findall(r'(?m)^[ \t]*#[ \t]*include[ \t]*([<"])([^>"]+)[>"]', text):
                candidates = [ROOT / "src" / name] if kind == "<" else [path.parent / name, ROOT / name, ROOT / "src" / name]
                found = next((candidate for candidate in candidates if candidate.is_file()), None)
                if found is not None:
                    pending.append(found.resolve())
        return closure

    def test_throughput_verdict_never_waits_for_macos(self):
        text = (self.WORKFLOWS / "compiler-throughput.yml").read_text(encoding="utf-8")
        harness = text.split("\n  harness:\n", 1)[1].split("\n  regression-guard:\n", 1)[0]
        self.assertIn("        os: [ubuntu-26.04, windows-2025]\n", harness)
        self.assertNotIn("macos", text.replace("throughput-harness-macos.yml", ""))
        self.assertEqual(text.count("    needs: harness\n"), 2)

    def test_macos_harness_is_path_filtered_ready_and_identical(self):
        throughput = (self.WORKFLOWS / "compiler-throughput.yml").read_text(encoding="utf-8")
        text = (self.WORKFLOWS / "throughput-harness-macos.yml").read_text(encoding="utf-8")
        self.assertEqual(self.unix_harness(text), self.unix_harness(throughput))
        self.assertIn("    types: [opened, synchronize, reopened, ready_for_review]\n", text)
        self.assertIn("!github.event.pull_request.draft", text)
        self.assertIn("    runs-on: macos-26\n", text)
        self.assertNotIn("merge_group", text)
        globs = re.findall(r"^      - '([^']+)'$", text.split("    paths:\n", 1)[1].split("  workflow_dispatch:", 1)[0], re.M)
        patterns = [self.path_pattern(glob) for glob in globs]
        closure = self.include_closure(("build.c", "tools/throughput/tests.c", "tools/throughput/shared.c"))
        closure.add("tools/allocation_census.py")
        self.assertIn("tools/throughput/throughput.c", closure)
        self.assertIn("src/buster/lib/os.c", closure)
        uncovered = sorted(path for path in closure if not any(pattern.match(path) for pattern in patterns))
        self.assertEqual(uncovered, [])
        self.assertFalse(any(pattern.match("src/buster/lib/compiler/frontend/c/c_gen.c") for pattern in patterns))
        self.assertFalse(any(pattern.match("docs/ci-runner-queue.md") for pattern in patterns))

    def test_materializer_merge_groups_keep_only_the_linux_leg(self):
        text = (self.WORKFLOWS / "native-retirement-materializer.yml").read_text(encoding="utf-8")
        self.assertIn("  merge_group:\n    types: [checks_requested]\n", text)
        self.assertIn("        runner: ${{ fromJSON(github.event_name == 'merge_group' && '[\"ubuntu-26.04\"]' "
                      "|| '[\"ubuntu-26.04\", \"macos-26\"]') }}\n", text)


if __name__ == "__main__":
    unittest.main()
