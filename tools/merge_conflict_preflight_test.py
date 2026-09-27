#!/usr/bin/env python3
"""Regression tests for the read-only merge-conflict preflight."""

from __future__ import annotations

import ast
import copy
import importlib.util
import io
import json
from pathlib import Path
import socket
import statistics
import subprocess
import sys
import tempfile
import time
import unittest
from unittest import mock
import urllib.error


REPO_ROOT = Path(__file__).resolve().parents[1]
TOOL_PATH = Path(__file__).with_name("merge_conflict_preflight.py")
WORKFLOW_PATH = REPO_ROOT / ".github" / "workflows" / "merge-conflict-preflight.yml"
REGRESSION_WORKFLOW_PATH = (REPO_ROOT / ".github" / "workflows" /
                            "merge-conflict-preflight-regression.yml")
GUIDANCE_PATH = REPO_ROOT / "docs" / "agents" / "workflow.md"
INTEGRATION_PATH = Path(__file__).with_name("native_retirement_integration.py")
SPEC = importlib.util.spec_from_file_location("merge_conflict_preflight", TOOL_PATH)
assert SPEC is not None and SPEC.loader is not None
PREFLIGHT = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = PREFLIGHT
SPEC.loader.exec_module(PREFLIGHT)


def frozen_path_assignment(path: Path, name: str) -> frozenset[str]:
    tree = ast.parse(path.read_text(encoding="utf-8"), filename=str(path))
    for statement in tree.body:
        if not isinstance(statement, ast.Assign) or len(statement.targets) != 1:
            continue
        target = statement.targets[0]
        if not isinstance(target, ast.Name) or target.id != name:
            continue
        value = statement.value
        if (not isinstance(value, ast.Call) or not isinstance(value.func, ast.Name) or
                value.func.id != "frozenset" or len(value.args) != 1):
            raise AssertionError(f"{name} is not one literal frozenset in {path}")
        literal = ast.literal_eval(value.args[0])
        return frozenset(str(item) for item in literal)
    raise AssertionError(f"missing {name} in {path}")


class Repository:
    def __init__(self, root: Path) -> None:
        self.root = root
        self.git("init", "-q", "-b", "main")
        self.git("config", "user.email", "preflight@example.invalid")
        self.git("config", "user.name", "Preflight Test")

    def git(self, *arguments: str, check: bool = True) -> str:
        process = subprocess.run(
            ["git", "-C", str(self.root), *arguments],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
        )
        if check and process.returncode != 0:
            raise AssertionError(
                f"git {' '.join(arguments)} failed ({process.returncode}):\n{process.stdout}\n{process.stderr}"
            )
        return process.stdout.strip()

    def write(self, path: str, content: str) -> None:
        destination = self.root / path
        destination.parent.mkdir(parents=True, exist_ok=True)
        destination.write_text(content, encoding="utf-8")

    def remove(self, path: str) -> None:
        (self.root / path).unlink()

    def commit(self, message: str) -> str:
        self.git("add", "-A")
        self.git("commit", "-qm", message)
        return self.git("rev-parse", "HEAD")

    def branch(self, name: str, start: str) -> None:
        self.git("switch", "-qc", name, start)

    def switch(self, name: str) -> None:
        self.git("switch", "-q", name)


class MergeConflictPreflightTest(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory(prefix="buster-merge-preflight-")
        self.root = Path(self.temporary.name)
        self.repo = Repository(self.root)

    def tearDown(self) -> None:
        self.temporary.cleanup()

    def initial(self, files: dict[str, str]) -> str:
        for path, content in files.items():
            self.repo.write(path, content)
        return self.repo.commit("base")

    def analyze(self, main: str, head: str, previous=None) -> dict:
        return PREFLIGHT.analyze(self.root, main, head, previous)

    @staticmethod
    def stable_report(report: dict) -> dict:
        value = copy.deepcopy(report)
        value["cost"]["elapsed_ms"] = 0
        return value

    def test_historical_old_base_collision_is_named_immediately(self) -> None:
        base = self.initial({"src/buster/tests/compiler/driver/driver_test.c": "base\n"})
        self.repo.branch("landed-first", base)
        self.repo.write("src/buster/tests/compiler/driver/driver_test.c", "first PR\n")
        main = self.repo.commit("land first sibling")
        self.repo.branch("old-base-second", base)
        self.repo.write("src/buster/tests/compiler/driver/driver_test.c", "second PR\n")
        head = self.repo.commit("second sibling from old base")

        report = self.analyze(main, head)

        self.assertFalse(report["merge"]["clean"])
        self.assertEqual(report["outcome"]["number"], 2)
        self.assertEqual(
            report["merge"]["conflicting_paths"],
            ["src/buster/tests/compiler/driver/driver_test.c"],
        )
        self.assertEqual(report["merge"]["path_details"][0]["classifications"], ["modify/modify"])
        self.assertTrue(report["merge"]["path_details"][0]["genuine_source_overlap"])

    def test_clean_siblings_from_one_base_report_exact_combined_tree(self) -> None:
        base = self.initial({"src/first.c": "base one\n", "src/second.c": "base two\n"})
        self.repo.branch("first", base)
        self.repo.write("src/first.c", "first changed\n")
        main = self.repo.commit("land first")
        self.repo.branch("second", base)
        self.repo.write("src/second.c", "second changed\n")
        head = self.repo.commit("second sibling")

        report = self.analyze(main, head)

        self.assertTrue(report["merge"]["clean"])
        self.assertEqual(report["main"]["sha"], main)
        self.assertEqual(report["head"]["sha"], head)
        self.assertEqual([entry["sha"] for entry in report["merge_bases"]], [base])
        self.assertRegex(report["main"]["tree"], r"^[0-9a-f]{40,64}$")
        self.assertRegex(report["head"]["tree"], r"^[0-9a-f]{40,64}$")
        self.assertRegex(report["merge_bases"][0]["tree"], r"^[0-9a-f]{40,64}$")
        self.assertTrue(report["merge"]["candidate_is_stale_against_current_main"])
        self.assertEqual(report["outcome"]["number"], 3)
        self.assertRegex(report["merge"]["combined_tree"], r"^[0-9a-f]{40,64}$")
        self.assertEqual(report["merge"]["conflicting_paths"], [])

    def test_same_source_add_add_is_classified(self) -> None:
        base = self.initial({"README": "base\n"})
        self.repo.branch("main-add", base)
        self.repo.write("src/new.c", "main\n")
        main = self.repo.commit("main adds source")
        self.repo.branch("head-add", base)
        self.repo.write("src/new.c", "head\n")
        head = self.repo.commit("head adds source")

        report = self.analyze(main, head)

        self.assertEqual(report["outcome"]["number"], 2)
        self.assertEqual(report["merge"]["path_details"][0]["classifications"], ["add/add"])

    def test_modify_delete_is_classified(self) -> None:
        base = self.initial({"src/removed.c": "base\n"})
        self.repo.branch("main-modifies", base)
        self.repo.write("src/removed.c", "main modified\n")
        main = self.repo.commit("modify")
        self.repo.branch("head-deletes", base)
        self.repo.remove("src/removed.c")
        head = self.repo.commit("delete")

        report = self.analyze(main, head)

        self.assertEqual(report["outcome"]["number"], 2)
        self.assertIn("modify/delete", report["merge"]["path_details"][0]["classifications"])

    def test_rename_delete_is_classified_when_git_reports_it(self) -> None:
        base = self.initial({"src/original.c": "line one\nline two\nline three\n"})
        self.repo.branch("main-renames", base)
        self.repo.git("mv", "src/original.c", "src/renamed.c")
        self.repo.write("src/renamed.c", "line one\nline two changed\nline three\n")
        main = self.repo.commit("rename and modify")
        self.repo.branch("head-deletes", base)
        self.repo.remove("src/original.c")
        head = self.repo.commit("delete original")

        report = self.analyze(main, head)
        classifications = {
            classification
            for path in report["merge"]["path_details"]
            for classification in path["classifications"]
        }

        self.assertFalse(report["merge"]["clean"])
        self.assertIn("rename/delete", classifications)
        self.assertIn("src/renamed.c", report["merge"]["conflicting_paths"])

    def test_generated_retirement_artifact_is_a_workflow_violation_even_when_merge_is_clean(self) -> None:
        generated = "docs/native-retirement-repository-sources-v1.json"
        base = self.initial({generated: "{}\n", "src/feature.c": "base\n"})
        self.repo.branch("main-doc", base)
        self.repo.write("README", "main movement\n")
        main = self.repo.commit("move main")
        self.repo.branch("ordinary-feature", base)
        self.repo.write("src/feature.c", "feature\n")
        self.repo.write(generated, '{"hand":"edited"}\n')
        head = self.repo.commit("feature incorrectly carries generated state")

        report = self.analyze(main, head)

        self.assertTrue(report["merge"]["clean"])
        self.assertEqual(report["outcome"]["number"], 1)
        self.assertTrue(report["outcome"]["blocking"])
        self.assertEqual(
            report["candidate_changes"]["generated_or_integration_owned_retirement_paths"],
            [generated],
        )
        self.assertIn("Do not hand-resolve hashes", report["outcome"]["action"])

    def test_policy_schema_conflict_uses_trusted_transition_classification(self) -> None:
        policy = "docs/native-retirement-dependencies-v1.json"
        base = self.initial({policy: '{"version":1}\n'})
        self.repo.branch("main-policy", base)
        self.repo.write(policy, '{"version":2,"main":true}\n')
        main = self.repo.commit("main policy")
        self.repo.branch("head-policy", base)
        self.repo.write(policy, '{"version":2,"head":true}\n')
        head = self.repo.commit("head policy")

        report = self.analyze(main, head)

        self.assertEqual(report["outcome"]["number"], 4)
        self.assertEqual(report["overlap"]["retirement_policy_schema_or_trust_paths"], [policy])
        self.assertTrue(report["merge"]["path_details"][0]["retirement_policy_or_schema"])

    def test_trust_path_conflict_uses_trusted_transition_classification(self) -> None:
        trust = "tools/native_retirement_rebind.py"
        base = self.initial({trust: "base\n"})
        self.repo.branch("main-trust", base)
        self.repo.write(trust, "main authority\n")
        main = self.repo.commit("main authority")
        self.repo.branch("head-trust", base)
        self.repo.write(trust, "candidate authority\n")
        head = self.repo.commit("candidate authority")

        report = self.analyze(main, head)

        self.assertEqual(report["outcome"]["number"], 4)
        self.assertEqual(report["overlap"]["retirement_policy_schema_or_trust_paths"], [trust])
        self.assertTrue(report["merge"]["path_details"][0]["retirement_trust_path"])

    def test_retirement_path_sets_match_the_trusted_integration_classifier(self) -> None:
        if not INTEGRATION_PATH.exists():
            self.skipTest("standalone fixture checkout omits native_retirement_integration.py")
        self.assertEqual(
            PREFLIGHT.GENERATED_RETIREMENT_PATHS,
            frozen_path_assignment(INTEGRATION_PATH, "GENERATED_PATHS"),
        )
        self.assertEqual(
            PREFLIGHT.RETIREMENT_TRUST_PATHS,
            frozen_path_assignment(INTEGRATION_PATH, "TRUST_IMPLEMENTATION_PATHS"),
        )
        self.assertEqual(
            PREFLIGHT.RETIREMENT_POLICY_SCHEMA_PATHS,
            frozen_path_assignment(INTEGRATION_PATH, "POLICY_SCHEMA_PATHS"),
        )

    def test_workflow_separates_untrusted_tests_from_trusted_status_publication(self) -> None:
        trusted = WORKFLOW_PATH.read_text(encoding="utf-8")
        regression = REGRESSION_WORKFLOW_PATH.read_text(encoding="utf-8")
        self.assertIn("workflow_run:\n", trusted)
        self.assertIn("workflows: [Merge conflict preflight regression]", trusted)
        self.assertIn("push:\n    branches: [main]", trusted)
        self.assertIn("merge_group:\n    types: [checks_requested]", trusted)
        self.assertIn("pull-requests: read\n      statuses: write", trusted)
        self.assertIn("ref: ${{ github.event.repository.default_branch }}", trusted)
        self.assertIn("persist-credentials: false", trusted)
        self.assertNotIn("pull_request_target", trusted)
        self.assertNotIn("python3 -B tools/merge_conflict_preflight_test.py -v", trusted)

        self.assertIn("pull_request:\n", regression)
        self.assertIn("python3 -B tools/merge_conflict_preflight_test.py -v", regression)
        self.assertIn("persist-credentials: false", regression)
        self.assertNotIn("statuses: write", regression)
        self.assertNotIn("GITHUB_TOKEN", regression)
        self.assertNotIn("tools/merge_conflict_preflight.py github-event", regression)

    def test_workflow_run_payload_requires_one_pull_request(self) -> None:
        event = {
            "workflow_run": {
                "event": "pull_request",
                "pull_requests": [{"number": 922}],
            },
        }
        self.assertEqual(PREFLIGHT._workflow_run_pull_number(event), 922)
        invalid = (
            {},
            {"workflow_run": {"event": "push", "pull_requests": [{"number": 922}]}},
            {"workflow_run": {"event": "pull_request", "pull_requests": []}},
            {"workflow_run": {"event": "pull_request", "pull_requests": [{"number": 0}]}},
            {"workflow_run": {"event": "pull_request", "pull_requests": [
                {"number": 1}, {"number": 2},
            ]}},
        )
        for payload in invalid:
            with self.subTest(payload=payload), self.assertRaises(PREFLIGHT.PreflightError):
                PREFLIGHT._workflow_run_pull_number(payload)

    def test_workflow_guidance_prescribes_each_classification_response(self) -> None:
        guidance = GUIDANCE_PATH.read_text(encoding="utf-8")
        self.assertIn("1. **Generated/integration-owned workflow violation.**", guidance)
        self.assertIn("2. **Genuine source overlap.**", guidance)
        self.assertIn("3. **Clean but stale.**", guidance)
        self.assertIn("4. **Policy/schema/trust-boundary overlap.**", guidance)
        self.assertIn("Do not rebase solely", guidance)
        self.assertIn("does not change refs, the index, the worktree or either input", guidance)
        self.assertIn("does not choose `ours`, `theirs`, a union driver", guidance)

    def test_main_movement_invalidates_the_previous_exact_result(self) -> None:
        base = self.initial({"src/main.c": "base\n", "src/head.c": "base\n"})
        self.repo.branch("candidate", base)
        self.repo.write("src/head.c", "candidate\n")
        head = self.repo.commit("candidate")
        first = self.analyze(base, head)

        self.repo.switch("main")
        self.repo.write("src/main.c", "main advanced\n")
        advanced_main = self.repo.commit("advance main")
        previous = PREFLIGHT.PreviousResult(
            main=first["main"]["sha"],
            head=first["head"]["sha"],
            outcome=first["outcome"]["number"],
        )
        second = self.analyze(advanced_main, head, previous)

        state = second["previous_authoritative_result"]
        self.assertTrue(state["available"])
        self.assertTrue(state["same_head"])
        self.assertTrue(state["main_advanced_since_last_authoritative_result"])
        self.assertEqual(state["advance_kind"], "fast-forward")
        self.assertTrue(second["merge"]["clean"])
        self.assertTrue(second["merge"]["candidate_is_stale_against_current_main"])
        self.assertNotEqual(first["main"]["sha"], second["main"]["sha"])

    def test_result_is_deterministic_and_does_not_mutate_refs_index_or_worktree(self) -> None:
        base = self.initial({"src/a.c": "base\n", "src/b.c": "base\n"})
        self.repo.branch("main-change", base)
        self.repo.write("src/a.c", "main\n")
        main = self.repo.commit("main")
        self.repo.branch("head-change", base)
        self.repo.write("src/b.c", "head\n")
        head = self.repo.commit("head")
        before_refs = self.repo.git("show-ref")
        before_status = self.repo.git("status", "--porcelain=v1", "--untracked-files=all")
        index = self.root / ".git" / "index"
        before_index = index.read_bytes()
        before_head = self.repo.git("symbolic-ref", "HEAD")

        first = self.analyze(main, head)
        second = self.analyze(main, head)

        self.assertEqual(self.stable_report(first), self.stable_report(second))
        self.assertEqual(self.repo.git("show-ref"), before_refs)
        self.assertEqual(self.repo.git("status", "--porcelain=v1", "--untracked-files=all"), before_status)
        self.assertEqual(index.read_bytes(), before_index)
        self.assertEqual(self.repo.git("symbolic-ref", "HEAD"), before_head)

    def test_plumbing_cost_is_bounded_and_never_builds_the_compiler(self) -> None:
        base = self.initial({f"src/file-{index}.c": f"base {index}\n" for index in range(64)})
        self.repo.branch("main-many", base)
        for index in range(0, 64, 2):
            self.repo.write(f"src/file-{index}.c", f"main {index}\n")
        main = self.repo.commit("main half")
        self.repo.branch("head-many", base)
        for index in range(1, 64, 2):
            self.repo.write(f"src/file-{index}.c", f"head {index}\n")
        head = self.repo.commit("head half")

        samples = []
        report = None
        for _ in range(7):
            started = time.monotonic_ns()
            report = self.analyze(main, head)
            samples.append((time.monotonic_ns() - started) / 1_000_000.0)
        assert report is not None
        median_ms = statistics.median(samples)
        print(f"merge-conflict-preflight-cost median_ms={median_ms:.3f} samples={json.dumps(samples)}")

        self.assertTrue(report["merge"]["clean"])
        self.assertFalse(report["cost"]["configured_or_built_compiler"])
        self.assertIn("merge-tree", report["cost"]["method"])
        self.assertLess(median_ms, 2000.0)

    def test_status_description_round_trips_exact_main_and_head(self) -> None:
        main = "1" * 40
        head = "2" * 40
        description = f"v1 m={main} h={head} o=3 c=clean"
        match = PREFLIGHT.STATUS_DESCRIPTION.fullmatch(description)
        self.assertIsNotNone(match)
        assert match is not None
        self.assertEqual(match.group("main"), main)
        self.assertEqual(match.group("head"), head)
        self.assertLessEqual(len(description), 140)


class GitHubTransportTests(unittest.TestCase):
    def setUp(self) -> None:
        self.api = PREFLIGHT.GitHubApi("buster14a/buster", "test", "https://api.example.invalid")

    @staticmethod
    def http_error(code: int, headers=None, message: str = "Unexpected error"):
        return urllib.error.HTTPError(
            "https://api.example.invalid/test", code, "test", headers or {},
            io.BytesIO(json.dumps({"message": message}).encode("utf-8")))

    @staticmethod
    def response(body: bytes = b'{"ok": true}'):
        response = mock.MagicMock()
        response.__enter__.return_value.read.return_value = body
        return response

    def test_get_500_recovers_but_repeated_500_exhausts_three_attempts(self):
        with (mock.patch.object(PREFLIGHT.urllib.request, "urlopen",
                                side_effect=[self.http_error(500), self.response()]) as urlopen,
              mock.patch.object(PREFLIGHT.time, "sleep") as sleep,
              mock.patch.object(PREFLIGHT.sys, "stderr", new_callable=io.StringIO) as log):
            self.assertEqual(self.api._request("GET", "/test"), {"ok": True})
            self.assertEqual(urlopen.call_count, 2)
            sleep.assert_called_once_with(1.0)
            self.assertIn("recovered GitHub API GET /test after 2 attempts", log.getvalue())
        with (mock.patch.object(PREFLIGHT.urllib.request, "urlopen",
                                side_effect=[self.http_error(500) for _ in range(3)]) as urlopen,
              mock.patch.object(PREFLIGHT.time, "sleep") as sleep):
            with self.assertRaises(PREFLIGHT.ApiRequestError) as caught:
                self.api._request("GET", "/test")
            self.assertEqual(caught.exception.attempts, 3)
            self.assertTrue(caught.exception.retryable)
            self.assertTrue(caught.exception.retry_exhausted)
            self.assertFalse(caught.exception.systemic)
            self.assertIn("after 3 attempt(s)", str(caught.exception))
            self.assertEqual(urlopen.call_count, 3)
            self.assertEqual([call.args[0] for call in sleep.call_args_list], [1.0, 2.0])

    def test_selected_timeout_and_temporary_dns_retry_but_permanent_dns_does_not(self):
        transient = (urllib.error.URLError(socket.timeout()),
                     urllib.error.URLError(socket.gaierror(socket.EAI_AGAIN, "try again")))
        for error in transient:
            with (self.subTest(error=error),
                  mock.patch.object(PREFLIGHT.urllib.request, "urlopen",
                                    side_effect=[error, self.response()]) as urlopen,
                  mock.patch.object(PREFLIGHT.time, "sleep")):
                self.assertEqual(self.api._request("GET", "/test"), {"ok": True})
                self.assertEqual(urlopen.call_count, 2)
        with (mock.patch.object(PREFLIGHT.urllib.request, "urlopen",
                                side_effect=urllib.error.URLError(
                                    socket.gaierror(socket.EAI_NONAME, "unknown host"))) as urlopen,
              mock.patch.object(PREFLIGHT.time, "sleep") as sleep):
            with self.assertRaises(PREFLIGHT.ApiRequestError) as caught:
                self.api._request("GET", "/test")
            self.assertFalse(caught.exception.retryable)
            self.assertEqual(urlopen.call_count, 1)
            sleep.assert_not_called()

    def test_rate_limit_honors_guidance_and_stops_without_safe_delay(self):
        with (mock.patch.object(PREFLIGHT.urllib.request, "urlopen",
                                side_effect=[self.http_error(429, {"Retry-After": "3"}),
                                             self.response()]) as urlopen,
              mock.patch.object(PREFLIGHT.time, "sleep") as sleep):
            self.assertEqual(self.api._request("GET", "/test"), {"ok": True})
            self.assertEqual(urlopen.call_count, 2)
            sleep.assert_called_once_with(3.0)
        with (mock.patch.object(PREFLIGHT.urllib.request, "urlopen",
                                side_effect=[self.http_error(403, {
                                    "Retry-After": "1", "X-RateLimit-Remaining": "0",
                                    "X-RateLimit-Reset": "1010"}),
                                             self.response()]) as urlopen,
              mock.patch.object(PREFLIGHT.time, "sleep") as sleep,
              mock.patch.object(PREFLIGHT.time, "time", return_value=1000)):
            self.assertEqual(self.api._request("GET", "/test"), {"ok": True})
            self.assertEqual(urlopen.call_count, 2)
            sleep.assert_called_once_with(10.0)
        for headers in ({}, {"Retry-After": "120"}):
            with (self.subTest(headers=headers),
                  mock.patch.object(PREFLIGHT.urllib.request, "urlopen",
                                    side_effect=self.http_error(429, headers)) as urlopen,
                  mock.patch.object(PREFLIGHT.time, "sleep") as sleep):
                with self.assertRaises(PREFLIGHT.ApiRequestError) as caught:
                    self.api._request("GET", "/test")
                self.assertTrue(caught.exception.systemic)
                self.assertEqual(urlopen.call_count, 1)
                sleep.assert_not_called()

    def test_permissions_invalid_json_and_post_are_never_retried(self):
        for method, error in (("GET", self.http_error(403, message="Resource not accessible")),
                              ("GET", self.http_error(422)),
                              ("POST", self.http_error(500)),
                              ("POST", urllib.error.URLError(socket.timeout()))):
            with (self.subTest(method=method, error=error),
                  mock.patch.object(PREFLIGHT.urllib.request, "urlopen",
                                    side_effect=error) as urlopen,
                  mock.patch.object(PREFLIGHT.time, "sleep") as sleep):
                with self.assertRaises(PREFLIGHT.ApiRequestError) as caught:
                    self.api._request(method, "/test", {"state": "success"} if method == "POST" else None)
                self.assertEqual(caught.exception.attempts, 1)
                self.assertFalse(caught.exception.retryable)
                self.assertEqual(urlopen.call_count, 1)
                sleep.assert_not_called()
        with (mock.patch.object(PREFLIGHT.urllib.request, "urlopen",
                                return_value=self.response(b"{bad json")) as urlopen,
              mock.patch.object(PREFLIGHT.time, "sleep") as sleep):
            with self.assertRaisesRegex(PREFLIGHT.PreflightError, "invalid JSON"):
                self.api._request("GET", "/test")
            self.assertEqual(urlopen.call_count, 1)
            sleep.assert_not_called()

    def test_retry_wait_cannot_consume_the_refresh_budget(self):
        self.api.deadline = 10
        with (mock.patch.object(PREFLIGHT.time, "monotonic", return_value=8),
              mock.patch.object(PREFLIGHT.urllib.request, "urlopen",
                                side_effect=self.http_error(500, {"Retry-After": "3"})) as urlopen,
              mock.patch.object(PREFLIGHT.time, "sleep") as sleep):
            with self.assertRaises(PREFLIGHT.ApiRequestError) as caught:
                self.api._request("GET", "/test")
            self.assertEqual(caught.exception.attempts, 1)
            self.assertEqual(urlopen.call_count, 1)
            sleep.assert_not_called()


class PushRefreshTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory(prefix="buster-merge-preflight-sweep-")
        self.root = Path(self.temporary.name)
        self.repo = Repository(self.root)
        self.repo.write("src/base.c", "base\n")
        self.repo.write("src/head.c", "base\n")
        base = self.repo.commit("base")
        self.repo.branch("candidate", base)
        self.repo.write("src/head.c", "candidate\n")
        self.head = self.repo.commit("candidate")
        self.report = PREFLIGHT.analyze(self.root, base, self.head)
        self.output = self.root / "reports"
        self.summary = self.root / "summary.md"
        self.event = {"repository": {"default_branch": "main"}}
        self.api = mock.Mock()
        self.api.open_pull_requests.return_value = [
            {"number": number, "head": {"sha": self.head}, "base": {"ref": "main"}}
            for number in (1, 2, 3)
        ]

    def tearDown(self) -> None:
        self.temporary.cleanup()

    def refresh(self) -> tuple[int, dict]:
        outcome = PREFLIGHT._github_push_event(
            self.root, self.api, self.event, self.output, self.summary,
            PREFLIGHT.STATUS_CONTEXT)
        return outcome, json.loads((self.output / "refresh.json").read_text())

    def test_failed_middle_pull_preserves_evidence_and_refreshes_later_pull(self):
        self.api.publish_status.return_value = None
        with mock.patch.object(PREFLIGHT, "_analyze_stable_pull", side_effect=[
            (self.report, self.head),
            PREFLIGHT.ApiRequestError("GET /pulls/2 HTTP 500", 3, True, False),
            (self.report, self.head),
        ]) as analyze:
            incomplete, refresh = self.refresh()
        self.assertEqual(incomplete, 2)
        self.assertEqual(analyze.call_count, 3)
        self.assertEqual(len(refresh["completed"]), 2)
        self.assertEqual([row["pull_request"] for row in refresh["completed"]], [1, 3])
        self.assertEqual(len(refresh["failed"]), 1)
        self.assertEqual(refresh["failed"][0]["error"]["attempts"], 3)
        self.assertTrue(refresh["failed"][0]["error"]["retry_exhausted"])
        self.assertEqual(refresh["not_attempted"], [])
        self.assertFalse(refresh["coverage_complete"])
        self.assertEqual(self.api.publish_status.call_count, 2)
        self.assertTrue((self.output / f"pr-1-{self.head}.json").is_file())
        self.assertTrue((self.output / f"pr-3-{self.head}.json").is_file())
        error = json.loads((self.output / "pr-2-error.json").read_text())
        self.assertFalse(error["authoritative_for_exact_identities"])
        self.assertEqual(error["status_publication"], "none")
        self.assertIn("completed 2, failed 1, not attempted 0", self.summary.read_text())

    def test_systemic_limit_stops_following_pulls_without_publishing_success(self):
        with mock.patch.object(PREFLIGHT, "_analyze_stable_pull", side_effect=[
            (self.report, self.head),
            PREFLIGHT.ApiRequestError("HTTP 429 rate limit", 1, True, True),
        ]) as analyze:
            incomplete, refresh = self.refresh()
        self.assertEqual(incomplete, 2)
        self.assertEqual(analyze.call_count, 2)
        self.assertEqual(refresh["not_attempted"], [3])
        self.assertEqual(self.api.publish_status.call_count, 1)
        self.assertIn("not attempted 1", self.summary.read_text())

    def test_budget_before_next_pull_accounts_for_remaining_work(self):
        with (mock.patch.object(PREFLIGHT.time, "monotonic", side_effect=[0, 241]),
              mock.patch.object(PREFLIGHT, "_analyze_stable_pull") as analyze):
            incomplete, refresh = self.refresh()
        self.assertEqual(incomplete, 2)
        self.assertEqual(refresh["not_attempted"], [1, 2, 3])
        analyze.assert_not_called()

    def test_unavailable_inventory_reports_unknown_coverage(self):
        self.api.open_pull_requests.side_effect = PREFLIGHT.ApiRequestError(
            "GET /pulls HTTP 500", 3, True, False)
        incomplete, refresh = self.refresh()
        self.assertEqual(incomplete, 2)
        self.assertFalse(refresh["inventory_complete"])
        self.assertIsNone(refresh["listed_pull_requests"])
        self.assertIn("not attempted unknown", self.summary.read_text())
        self.assertTrue((self.output / "inventory-error.json").is_file())
        self.api.publish_status.assert_not_called()

    def test_ambiguous_post_is_recorded_without_a_retry_or_clean_claim(self):
        self.api.publish_status.side_effect = [
            None, PREFLIGHT.ApiRequestError("POST /statuses timed out", 1, False, False),
            None,
        ]
        with mock.patch.object(PREFLIGHT, "_analyze_stable_pull",
                               return_value=(self.report, self.head)):
            status, refresh = self.refresh()
        self.assertEqual(status, 2)
        self.assertEqual(self.api.publish_status.call_count, 3)
        self.assertEqual([row["pull_request"] for row in refresh["completed"]], [1, 3])
        self.assertEqual(refresh["failed"][0]["status_publication"], "unknown")
        self.assertTrue((self.output / f"pr-2-{self.head}.json").is_file())

    def test_complete_refresh_remains_green_when_one_pr_has_a_content_conflict(self):
        blocking_report = copy.deepcopy(self.report)
        blocking_report["outcome"]["blocking"] = True
        with mock.patch.object(PREFLIGHT, "_analyze_stable_pull", side_effect=[
            (blocking_report, self.head), (self.report, self.head), (self.report, self.head),
        ]):
            status, refresh = self.refresh()
        self.assertEqual(status, 0)
        self.assertTrue(refresh["coverage_complete"])
        self.assertEqual(refresh["blocking_count"], 1)
        self.assertEqual(self.api.publish_status.call_count, 3)

    def test_recovered_read_still_rejects_moved_main_before_publication(self):
        self.repo.switch("main")
        self.repo.write("src/base.c", "advanced main\n")
        advanced = self.repo.commit("advanced main")
        pull = {"number": 1, "head": {"sha": self.head}, "base": {"ref": "main"}}
        self.api.pull_request.return_value = pull
        self.api.previous_status.return_value = None
        with mock.patch.object(PREFLIGHT, "_fetch_ref", side_effect=[
            self.report["main"]["sha"], self.head, advanced,
            advanced, self.head, advanced,
        ]) as fetch:
            report, head = PREFLIGHT._analyze_stable_pull(
                self.root, self.api, 1, PREFLIGHT.STATUS_CONTEXT)
        self.assertEqual(fetch.call_count, 6)
        self.assertEqual(head, self.head)
        self.assertEqual(report["main"]["sha"], advanced)
        self.assertNotEqual(report["main"]["sha"], self.report["main"]["sha"])


if __name__ == "__main__":
    unittest.main()
