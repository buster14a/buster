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
from unittest.mock import patch


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


class GroupGitHub:
    repository = "buster14a/buster"

    def __init__(self, root, base, head, evidence):
        self.head = head
        self.statuses = json.loads(status_file(root, head, evidence).read_text())["statuses"]
        self.statuses[0]["created_at"] = "2026-09-22T00:01:00Z"
        self.pulls = [{"number": 1, "state": "open", "draft": False,
                       "head": {"sha": head, "repo": {"full_name": self.repository}},
                       "base": {"ref": "main", "repo": {"full_name": self.repository}}}]
        self.run = {"id": 1, "run_attempt": 1, "repository": {"full_name": self.repository},
                    "path": ".github/workflows/native-retirement-integration.yml",
                    "event": "workflow_dispatch", "head_branch": "main", "head_sha": base,
                    "status": "completed", "conclusion": "success",
                    "run_started_at": "2026-09-22T00:00:00Z",
                    "updated_at": "2026-09-22T00:02:00Z"}

    def all(self, path):
        if path == "statuses/" + self.head:
            return self.statuses
        if path == "commits/" + self.head + "/pulls":
            return self.pulls
        raise AssertionError(path)

    def request(self, path):
        if path != "actions/runs/1":
            raise AssertionError(path)
        return self.run


class MergeGroupTests(unittest.TestCase):
    setUp = AdmissionTests.setUp

    def group(self, member, base=None, tree=None):
        repo = self.repository.repo
        base = base or self.repository.base
        tree = tree or gate.clean_merge_tree(repo, base, member)
        return git(repo, "commit-tree", tree, "-p", base, "-p", member, "-m", "queue group")

    def prepared(self):
        source = self.repository.branch("bound", {"src/buster/lib/value.c": "int value = 7;\n"})
        head, evidence = self.repository.integration(source)
        api = GroupGitHub(self.root, self.repository.base, head, evidence)
        return head, self.group(head), api

    def check(self, group, api, base=None):
        base = base or self.repository.base
        return gate.check_event(self.repository.repo, base, group, base, None,
                                "merge_group", False, api)

    def test_exact_generated_tree_uses_existing_writer_evidence(self):
        head, group, api = self.prepared()
        result = self.check(group, api)
        self.assertNotEqual(group, head)
        self.assertEqual(result["mode"], "trusted-integration-merge-group")
        self.assertEqual(result["integration_head"], head)
        self.assertEqual(result["head"], group)
        self.assertEqual(result["final_tree"], gate.tree(self.repository.repo, head))
        self.assertEqual(result["publication"], {"run_id": 1, "run_attempt": 1})

    def test_unrelated_group_needs_no_writer_or_same_repository_pr(self):
        head = self.repository.branch("docs", {"README.md": "unrelated\n"})
        result = self.check(self.group(head), None)
        self.assertEqual(result["mode"], "ordinary-merge-group")

    def test_second_ordinary_group_waits_until_first_synthetic_group_lands(self):
        first = self.repository.branch("first", {"README.md": "first\n"})
        second = self.repository.branch("second", {"docs/second.md": "second\n"})
        first_group = self.group(first)
        second_group = self.group(second, base=first_group)
        with self.assertRaisesRegex(gate.AdmissionError, "group base does not equal live main"):
            gate.check_event(self.repository.repo, first_group, second_group,
                             self.repository.base, None, "merge_group", False)
        result = gate.check_event(self.repository.repo, first_group, second_group,
                                  first_group, None, "merge_group", False)
        self.assertEqual(result["mode"], "ordinary-merge-group")
        self.assertEqual(result["base"], first_group)
        self.assertEqual(result["head"], second_group)

    def test_old_sensitive_head_does_not_become_valid_after_predecessor_lands(self):
        sensitive, _, api = self.prepared()
        first = self.repository.branch("first", {"README.md": "first\n"})
        first_group = self.group(first)
        second_group = self.group(sensitive, base=first_group)
        with self.assertRaisesRegex(gate.AdmissionError, "not based on"):
            gate.check_event(self.repository.repo, first_group, second_group,
                             first_group, None, "merge_group", False, api)

    def test_sensitive_group_requires_live_status_even_with_allow_pending(self):
        _, group, api = self.prepared()
        with self.assertRaisesRegex(gate.AdmissionError, "live trusted"):
            gate.check_event(self.repository.repo, self.repository.base, group,
                             self.repository.base, None, "merge_group", True)
        api.statuses.clear()
        with self.assertRaisesRegex(gate.AdmissionError, "attestation"):
            self.check(group, api)

    def test_generated_only_group_does_not_take_ordinary_path(self):
        head = self.repository.branch("generated", {
            "tools/native_retirement_dependency_binding.generated.h": "#define NEW 2\n"})
        with self.assertRaisesRegex(gate.AdmissionError, "live trusted"):
            self.check(self.group(head), None)

    def test_group_cannot_change_any_byte_after_attestation(self):
        head, _, api = self.prepared()
        changed = self.repository.branch("other", {"README.md": "another tree\n"})
        group = self.group(head, tree=gate.tree(self.repository.repo, changed))
        with self.assertRaisesRegex(gate.AdmissionError, "combined tree"):
            self.check(group, api)

    def test_group_cannot_include_another_queued_candidate(self):
        head, _, api = self.prepared()
        sibling = self.repository.branch("sibling", {"README.md": "sibling\n"})
        group = self.group(head, base=sibling)
        with self.assertRaisesRegex(gate.AdmissionError, "current main as first parent"):
            self.check(group, api)

    def test_stale_group_and_stale_integration_require_reconstruction(self):
        head, group, api = self.prepared()
        new_main = self.repository.branch("advanced", {"README.md": "new main\n"})
        with self.assertRaisesRegex(gate.AdmissionError, "group base does not equal live main"):
            gate.check_event(self.repository.repo, self.repository.base, group,
                             new_main, None, "merge_group", False, api)
        rebuilt = self.group(head, base=new_main)
        with self.assertRaisesRegex(gate.AdmissionError, "not based on"):
            self.check(rebuilt, api, base=new_main)

    def test_failed_cancelled_pending_and_retried_writer_do_not_authorize(self):
        _, group, api = self.prepared()
        for updates in ({"conclusion": "failure"}, {"conclusion": "cancelled"},
                        {"conclusion": "skipped"}, {"status": "in_progress"},
                        {"run_attempt": 2, "run_started_at": "2026-09-22T00:01:30Z"}):
            with self.subTest(updates=updates), patch.dict(api.run, updates):
                with self.assertRaises(gate.AdmissionError):
                    self.check(group, api)

    def test_wrong_writer_identity_and_missing_timestamps_are_rejected(self):
        _, group, api = self.prepared()
        for key, value in (("repository", {"full_name": "elsewhere/repo"}),
                           ("path", ".github/workflows/ci.yml"), ("event", "pull_request"),
                           ("head_branch", "feature"), ("head_sha", "a" * 40),
                           ("run_started_at", None), ("updated_at", "invalid")):
            with self.subTest(key=key), patch.dict(api.run, {key: value}):
                with self.assertRaises(gate.AdmissionError):
                    self.check(group, api)

    def test_closed_fork_moved_draft_or_ambiguous_pr_is_not_current_publication(self):
        _, group, api = self.prepared()
        pull = api.pulls[0]
        for updates in ({"state": "closed"}, {"draft": True},
                        {"head": {"sha": api.head, "repo": {"full_name": "fork/buster"}}},
                        {"head": {"sha": "a" * 40, "repo": {"full_name": api.repository}}}):
            with self.subTest(updates=updates), patch.dict(pull, updates):
                with self.assertRaisesRegex(gate.AdmissionError, "one open"):
                    self.check(group, api)
        api.pulls.append(dict(pull, number=2))
        with self.assertRaisesRegex(gate.AdmissionError, "one open"):
            self.check(group, api)

    def test_latest_status_cannot_reuse_previous_success(self):
        _, group, api = self.prepared()
        for state in ("pending", "failure", "error"):
            api.statuses[:] = [api.statuses[0], dict(api.statuses[0], id=43, state=state)]
            with self.subTest(state=state), self.assertRaisesRegex(gate.AdmissionError, "latest"):
                self.check(group, api)

    def test_second_old_base_source_needs_new_writer_output_after_first_lands(self):
        first = self.repository.branch("first", {"src/buster/lib/value.c": "int value = 8;\n"})
        second = self.repository.branch("second", {"tools/native_retirement_rebind.py": "# bootstrap\n"})
        second_head, evidence = self.repository.integration(second, kind="bootstrap")
        api = GroupGitHub(self.root, self.repository.base, second_head, evidence)
        self.check(self.group(second_head), api)
        first_head, _ = self.repository.integration(first)
        with self.assertRaisesRegex(gate.AdmissionError, "not based on"):
            self.check(self.group(second_head, base=first_head), api, base=first_head)
        # Model a fresh authorized writer preparation from the original source.
        # This Git fixture does not claim a live dispatch or SDK reconstruction.
        recovered = gate.source_candidate(self.repository.repo, first_head, second_head, api)
        self.assertEqual(recovered["head"], second_head)
        self.assertEqual(recovered["source_head"], second)
        self.repository.base = first_head
        refreshed, new_evidence = self.repository.integration(recovered["source_head"], kind="bootstrap")
        fresh_api = GroupGitHub(self.root, first_head, refreshed, new_evidence)
        result = self.check(self.group(refreshed), fresh_api)
        self.assertEqual(result["base"], first_head)
        self.assertNotEqual(result["final_tree"], gate.tree(self.repository.repo, second_head))

    def test_source_recovery_preserves_live_head_and_rejects_failed_publication(self):
        head, _, api = self.prepared()
        original = gate.commit(self.repository.repo, "HEAD")
        report = gate.source_candidate(self.repository.repo, self.repository.base, head, api)
        self.assertEqual(report["source_head"], gate.commit_parents(self.repository.repo, head)[1])
        self.assertEqual(gate.commit(self.repository.repo, "HEAD"), original)
        api.run["conclusion"] = "cancelled"
        with self.assertRaisesRegex(gate.AdmissionError, "successful"):
            gate.source_candidate(self.repository.repo, self.repository.base, head, api)

    def test_source_recovery_rejects_manual_generated_edits_and_real_conflicts(self):
        edited = self.repository.branch("generated", {
            "tools/native_retirement_dependency_binding.generated.h": "#define EDIT 1\n"})
        with self.assertRaisesRegex(gate.integration.IntegrationError, "integration-owned"):
            gate.source_candidate(self.repository.repo, self.repository.base, edited, None)
        head, _, api = self.prepared()
        conflict = self.repository.branch("conflict", {"src/buster/lib/value.c": "int value = 99;\n"})
        with self.assertRaisesRegex(gate.AdmissionError, "conflict-free"):
            gate.source_candidate(self.repository.repo, conflict, head, api)

    def test_writer_authorizes_live_head_but_regenerates_only_verified_source(self):
        workflow = (Path(__file__).resolve().parents[1] /
                    ".github/workflows/native-retirement-integration.yml").read_text()
        self.assertIn("tools/native_retirement_merge_gate.py source-candidate", workflow)
        self.assertIn('--source-head "$source_head"', workflow)
        self.assertEqual(workflow.count("--head '${{ needs.prepare.outputs.source_head }}'"), 2)
        self.assertIn("--head '${{ steps.identities.outputs.source_head }}'", workflow)
        self.assertIn('--expected-head "$head"', workflow)
        self.assertIn('--expected-head "$EXPECTED_HEAD"', workflow)
        self.assertIn('"Native-retirement-candidate: $source_head"', workflow)
        self.assertIn('commit-tree "$final_tree" -p "$base" -p "$source_head"', workflow)
        self.assertIn('--force-with-lease="refs/heads/$head_ref:$head"', workflow)
        self.assertIn("source-candidate.json", workflow)


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

    def test_stale_pr_event_base_uses_trusted_live_main_and_groups_keep_queued_base(self):
        for name, step in (("api-migration-policy.yml", "Enforce trusted native-retirement integration"),
                           ("native-retirement-rebind.yml", "Reject feature-owned generated state and classify trust transitions")):
            workflow = (self.root / ".github/workflows" / name).read_text()
            block = workflow.split("      - name: " + step + "\n", 1)[1].split("      - name:", 1)[0]
            script = textwrap.dedent(block.split("        run: |\n", 1)[1])
            for event in ("pull_request", "merge_group"):
                with self.subTest(workflow=name, event=event), tempfile.TemporaryDirectory() as directory:
                    root = Path(directory)
                    (root / "trusted/tools").mkdir(parents=True)
                    (root / "candidate").mkdir()
                    (root / "trusted/tools/native_retirement_merge_gate.py").write_text(
                        "import argparse,json,os\n"
                        "p=argparse.ArgumentParser(); p.add_argument('command',choices=['check'])\n"
                        "for key in ('repo-root','base','head','current-main','event','status-json'): p.add_argument('--'+key)\n"
                        "p.add_argument('--allow-pending',action='store_true')\n"
                        "if os.environ['EVENT_NAME']=='merge_group': p.add_argument('--repository',required=True)\n"
                        "args=p.parse_args()\n"
                        "if args.event=='pull_request':\n"
                        " assert args.status_json\n"
                        " assert args.base==os.environ['TRUSTED_MAIN_SHA']==args.current_main\n"
                        " assert args.base!=os.environ['STALE_EVENT_BASE_SHA']\n"
                        "else:\n"
                        " assert args.repository=='buster14a/buster'\n"
                        " assert args.base==os.environ['BASE_SHA']\n"
                        "print(json.dumps({'status':'admitted','mode':'trusted-integration'}))\n")
                    (root / "trusted/tools/merge_queue_admission.py").write_text(
                        "import json\nprint(json.dumps({'status':'base-landed'}))\n")
                    # The PR event still names the old main, while a verified
                    # writer has published a head based on the newer checkout.
                    prefix = (
                        'git() { if [[ "$*" == *"rev-parse HEAD"* ]]; then '
                        'printf "%s\\n" "$TRUSTED_MAIN_SHA"; else '
                        'printf "%s\\trefs/heads/main\\n" "$REMOTE_MAIN_SHA"; fi; }\n'
                        'gh() { printf "[[]]\\n"; }\n'
                    )
                    env = {**os.environ, "EVENT_NAME": event, "GITHUB_WORKSPACE": str(root),
                           "RUNNER_TEMP": str(root), "GITHUB_OUTPUT": str(root / "output"),
                           "GITHUB_REPOSITORY": "buster14a/buster", "BASE_SHA": "a" * 40,
                           "STALE_EVENT_BASE_SHA": "a" * 40,
                           "TRUSTED_REF": "a" * 40, "TRUSTED_MAIN_SHA": "c" * 40,
                           "REMOTE_MAIN_SHA": "c" * 40, "HEAD_SHA": "b" * 40,
                           "GITHUB_SHA": "b" * 40, "GITHUB_EVENT_PATH": str(root / "event.json"),
                           "CANDIDATE_HEAD": "b" * 40}
                    result = subprocess.run(["bash", "-e", "-o", "pipefail", "-c", prefix + script],
                        env=env, capture_output=True, text=True)
                    self.assertEqual(result.returncode, 0, result.stderr)
                    if event == "pull_request":
                        moved = subprocess.run(
                            ["bash", "-e", "-o", "pipefail", "-c", prefix + script],
                            env={**env, "REMOTE_MAIN_SHA": "d" * 40},
                            capture_output=True, text=True,
                        )
                        self.assertNotEqual(moved.returncode, 0)
                        self.assertIn("Main advanced after trusted PR policy checkout", moved.stderr)

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
