#!/usr/bin/env python3
"""Tests for native-retirement merge admission and stale-head invalidation."""

from __future__ import annotations

import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import textwrap
import unittest


MODULE_PATH = Path(__file__).with_name("native_retirement_merge_gate.py")
SPEC = importlib.util.spec_from_file_location("native_retirement_merge_gate", MODULE_PATH)
gate = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
sys.modules[SPEC.name] = gate
SPEC.loader.exec_module(gate)


def git(repo: Path, *arguments: str, input_text: str | None = None) -> str:
    result = subprocess.run(
        ["git", "-c", "core.hooksPath=/dev/null", "-C", os.fspath(repo), *arguments],
        input=input_text, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
    )
    if result.returncode != 0:
        raise AssertionError(
            "git failed: " + " ".join(arguments) + "\n" +
            (result.stderr or result.stdout)
        )
    return result.stdout.strip()


class Repository:
    def __init__(self, root: Path):
        self.repo = root / "repo"
        git(root, "init", "-b", "main", os.fspath(self.repo))
        git(self.repo, "config", "user.name", "Test User")
        git(self.repo, "config", "user.email", "test@example.invalid")
        files = {
            "src/buster/lib/value.c": "int value = 1;\n",
            "README.md": "base\n",
            "docs/native-retirement-repository-sources-v1.json": json.dumps({
                "schema": "buster-native-retirement-repository-sources-v1",
                "records": [{
                    "source": "src/buster/lib/value.c",
                    "bytes": 15,
                    "sha256": "1" * 64,
                }],
            }, indent=2) + "\n",
            "tools/native_retirement_dependency_binding.generated.h": "#define OLD 1\n",
        }
        for relative, content in files.items():
            path = self.repo / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(content)
        git(self.repo, "add", ".")
        git(self.repo, "commit", "-m", "base")
        self.base = git(self.repo, "rev-parse", "HEAD")

    def branch(self, name: str, changes: dict[str, str]) -> str:
        git(self.repo, "checkout", "-B", name, self.base)
        for relative, content in changes.items():
            path = self.repo / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(content)
        git(self.repo, "add", ".")
        git(self.repo, "commit", "-m", name)
        return git(self.repo, "rev-parse", "HEAD")

    def integration(self, candidate: str, kind: str = "ordinary") -> tuple[str, str]:
        git(self.repo, "checkout", "-B", "integrated", self.base)
        git(self.repo, "merge", "--no-commit", "--no-ff", candidate)
        snapshot = self.repo / "docs/native-retirement-repository-sources-v1.json"
        header = self.repo / "tools/native_retirement_dependency_binding.generated.h"
        snapshot.write_text(snapshot.read_text().replace('"1" * 64', '"2" * 64'))
        # Ensure the fixture actually differs without depending on the textual
        # form of the JSON serializer above.
        snapshot.write_text(snapshot.read_text() + "\n")
        header.write_text("#define NEW 1\n")
        git(self.repo, "add", os.fspath(snapshot.relative_to(self.repo)),
            os.fspath(header.relative_to(self.repo)))
        final_tree = git(self.repo, "write-tree")
        evidence = "e" * 64
        message = (
            "trusted integration\n\n"
            f"{gate.TRAILER_EVIDENCE}: {evidence}\n"
            f"{gate.TRAILER_BASE}: {self.base}\n"
            f"{gate.TRAILER_CANDIDATE}: {candidate}\n"
            f"{gate.TRAILER_FINAL_TREE}: {final_tree}\n"
            f"{gate.TRAILER_KIND}: {kind}\n"
        )
        commit = git(
            self.repo, "commit-tree", final_tree, "-p", self.base, "-p", candidate,
            input_text=message,
        )
        git(self.repo, "reset", "--hard", commit)
        return commit, evidence


def status_file(root: Path, head: str, evidence: str,
                creator: str = "github-actions[bot]", state: str = "success") -> Path:
    path = root / "status.json"
    path.write_text(json.dumps({
        "sha": head,
        "statuses": [{
            "id": 42,
            "context": gate.STATUS_CONTEXT,
            "state": state,
            "description": "evidence " + evidence[:16],
            "target_url": "https://github.com/buster14a/buster/actions/runs/1",
            "creator": {"login": creator},
        }],
    }))
    return path


class AdmissionTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.repository = Repository(self.root)

    def test_unrelated_pull_request_keeps_ordinary_merge_path(self):
        head = self.repository.branch("docs", {"README.md": "changed\n"})
        report = gate.check_pull_request(
            self.repository.repo, self.repository.base, head,
            self.repository.base, None, False,
        )
        self.assertEqual(report["mode"], "ordinary")

    def test_bound_source_is_pending_for_ephemeral_validation_but_blocked_for_merge(self):
        head = self.repository.branch(
            "bound", {"src/buster/lib/value.c": "int value = 2;\n"}
        )
        pending = gate.check_pull_request(
            self.repository.repo, self.repository.base, head,
            self.repository.base, None, True,
        )
        self.assertEqual(pending["mode"], "pending-integration")
        with self.assertRaisesRegex(gate.AdmissionError, "ordinary merge is blocked"):
            gate.check_pull_request(
                self.repository.repo, self.repository.base, head,
                self.repository.base, None, False,
            )

    def test_manual_generated_edit_fails_even_in_pending_mode(self):
        head = self.repository.branch("generated", {
            "tools/native_retirement_dependency_binding.generated.h": "#define BAD 1\n",
        })
        with self.assertRaisesRegex(gate.integration.IntegrationError, "integration-owned"):
            gate.check_pull_request(
                self.repository.repo, self.repository.base, head,
                self.repository.base, None, True,
            )

    def test_exact_attested_integration_head_is_admitted(self):
        candidate = self.repository.branch(
            "bound", {"src/buster/lib/value.c": "int value = 3;\n"}
        )
        head, evidence = self.repository.integration(candidate)
        report = gate.check_pull_request(
            self.repository.repo, self.repository.base, head,
            self.repository.base, status_file(self.root, head, evidence), False,
        )
        self.assertEqual(report["mode"], "trusted-integration")
        self.assertEqual(report["candidate"], candidate)

    def test_stale_main_rejects_previously_attested_head(self):
        candidate = self.repository.branch(
            "bound", {"src/buster/lib/value.c": "int value = 4;\n"}
        )
        head, evidence = self.repository.integration(candidate)
        stale_main = "a" * 40
        with self.assertRaisesRegex(gate.AdmissionError, "Main advanced|main advanced"):
            gate.check_pull_request(
                self.repository.repo, self.repository.base, head, stale_main,
                status_file(self.root, head, evidence), False,
            )

    def test_attestation_must_be_success_from_github_actions(self):
        candidate = self.repository.branch(
            "bound", {"src/buster/lib/value.c": "int value = 5;\n"}
        )
        head, evidence = self.repository.integration(candidate)
        with self.assertRaisesRegex(gate.AdmissionError, "trusted GitHub Actions"):
            gate.check_pull_request(
                self.repository.repo, self.repository.base, head,
                self.repository.base,
                status_file(self.root, head, evidence, creator="someone"),
                False,
            )

    def test_workflows_fetch_creator_bearing_status_rows(self):
        root = Path(__file__).resolve().parents[1]
        for relative, head in (
            (".github/workflows/api-migration-policy.yml", "HEAD_SHA"),
            (".github/workflows/native-retirement-rebind.yml", "CANDIDATE_HEAD"),
        ):
            with self.subTest(workflow=relative):
                content = (root / relative).read_text()
                self.assertIn(f"statuses/${head}", content)
                self.assertNotIn(f"commits/${head}/status", content)

    def test_sensitive_merge_group_fails_closed(self):
        head = self.repository.branch(
            "bound", {"src/buster/lib/value.c": "int value = 6;\n"}
        )
        with self.assertRaisesRegex(gate.AdmissionError, "merge groups"):
            gate.check_event(
                self.repository.repo, self.repository.base, head,
                self.repository.base, None, "merge_group", False,
            )


class FakeGitHub:
    repository = "buster14a/buster"

    def __init__(self, pulls: list[dict], commits: dict[str, dict]):
        self.pulls = pulls
        self.commits = commits
        self.posts = []

    def all(self, path: str, **query):
        if path != "pulls" or query != {"state": "open", "base": "main"}:
            raise AssertionError((path, query))
        return list(self.pulls)

    def request(self, path: str, *, method: str = "GET", body=None, **query):
        if path.startswith("commits/") and method == "GET":
            return self.commits[path.removeprefix("commits/")]
        if path == "check-runs" and method == "POST":
            self.posts.append(body)
            return {"id": len(self.posts)}
        raise AssertionError((path, method, body, query))


class InvalidationTests(unittest.TestCase):
    def test_main_push_marks_only_stale_integration_heads_failed(self):
        old = "1" * 40
        new = "2" * 40
        stale_head = "3" * 40
        current_head = "4" * 40
        ordinary_head = "5" * 40

        def message(base: str) -> str:
            return (
                f"{gate.TRAILER_EVIDENCE}: {'e' * 64}\n"
                f"{gate.TRAILER_BASE}: {base}\n"
                f"{gate.TRAILER_CANDIDATE}: {'6' * 40}\n"
                f"{gate.TRAILER_FINAL_TREE}: {'7' * 40}\n"
                f"{gate.TRAILER_KIND}: ordinary\n"
            )

        def pull(number: int, head: str) -> dict:
            return {
                "number": number,
                "head": {
                    "sha": head,
                    "repo": {"full_name": "buster14a/buster"},
                },
                "base": {"repo": {"full_name": "buster14a/buster"}},
            }

        api = FakeGitHub(
            [pull(1, stale_head), pull(2, current_head), pull(3, ordinary_head)],
            {
                stale_head: {"commit": {"message": message(old)}},
                current_head: {"commit": {"message": message(new)}},
                ordinary_head: {"commit": {"message": "ordinary"}},
            },
        )
        report = gate.invalidate_stale(
            api, new, "https://github.com/buster14a/buster/actions/runs/2"
        )
        self.assertEqual([row["pull_request"] for row in report["pull_requests"]], [1])
        self.assertEqual(len(api.posts), 1)
        self.assertEqual(api.posts[0]["name"], gate.REQUIRED_CHECK_NAME)
        self.assertEqual(api.posts[0]["conclusion"], "failure")
        self.assertEqual(api.posts[0]["head_sha"], stale_head)


class WorkflowPolicyTests(unittest.TestCase):
    root = Path(__file__).resolve().parents[1]

    def test_admission_and_compatibility_are_independent_required_checks(self):
        workflow = (self.root / ".github/workflows/api-migration-policy.yml").read_text()
        policy, admission = workflow.split("  native-retirement-admission:\n", 1)
        self.assertEqual(gate.REQUIRED_CHECK_NAME, "Native retirement merge admission")
        self.assertIn("    name: API migration policy\n", policy)
        self.assertIn("    name: Native retirement merge admission\n", admission)
        self.assertIn("tools/api_migration_audit.py", policy)
        self.assertNotIn("tools/native_retirement_merge_gate.py", policy)
        self.assertIn("tools/native_retirement_merge_gate.py", admission)
        self.assertNotIn("tools/api_migration_audit.py", admission)
        self.assertIn("github.event.merge_group.base_sha", admission)

    def test_both_required_jobs_fail_instead_of_skipping_when_ci_is_disabled(self):
        workflow = (self.root / ".github/workflows/api-migration-policy.yml").read_text()
        policy, admission = workflow.split("  native-retirement-admission:\n", 1)
        for job in (policy.split("  policy:\n", 1)[1], admission):
            self.assertNotIn("vars.GH_ACTIONS_CI_ENABLED", job.split("    steps:\n", 1)[0])
            guard = job.split("      - name: Require CI admission to be enabled\n", 1)[1]
            script = textwrap.dedent(guard.split("        run: |\n", 1)[1].split("      - name:", 1)[0])
            for enabled in ("", "false", "true"):
                with self.subTest(job=job.splitlines()[0], enabled=enabled):
                    result = subprocess.run(
                        ["bash", "-e", "-c", script],
                        env={**os.environ, "CI_ENABLED": enabled},
                        capture_output=True, text=True,
                    )
                    self.assertEqual(result.returncode, 0 if enabled == "true" else 1)

    def test_paginated_status_wrapper_preserves_creator_evidence(self):
        for name in ("api-migration-policy.yml", "native-retirement-rebind.yml"):
            workflow = (self.root / ".github/workflows" / name).read_text()
            self.assertIn("gh api --paginate --slurp", workflow)
            script = textwrap.dedent(workflow.split("<<'PY_STATUS'\n", 1)[1].split("          PY_STATUS\n", 1)[0])
            with self.subTest(workflow=name), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                pages = root / "pages.json"
                output = root / "status.json"
                trusted = {"context": gate.STATUS_CONTEXT, "creator": {"login": "github-actions[bot]"}}
                pages.write_text(json.dumps([[{"id": index} for index in range(100)], [trusted]]))
                result = subprocess.run(
                    [sys.executable, "-c", script, "a" * 40, str(pages), str(output)],
                    capture_output=True, text=True,
                )
                self.assertEqual(result.returncode, 0, result.stderr)
                status = json.loads(output.read_text())
                self.assertEqual(status["sha"], "a" * 40)
                self.assertEqual(len(status["statuses"]), 101)
                self.assertEqual(status["statuses"][-1], trusted)

    def test_publisher_askpass_emits_literal_credentials_and_one_newline(self):
        workflow = (self.root / ".github/workflows/native-retirement-integration.yml").read_text()
        script = textwrap.dedent(workflow.split('cat > "$askpass" <<\'SH\'\n', 1)[1].split("          SH\n", 1)[0])
        for prompt, expected in (("Username", "x-access-token\n"), ("Password", "test-token\n")):
            result = subprocess.run(
                ["sh", "-c", script, "askpass", prompt],
                env={**os.environ, "GITHUB_TOKEN": "test-token"},
                capture_output=True, text=True,
            )
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(result.stdout, expected)


if __name__ == "__main__":
    unittest.main()
