#!/usr/bin/env python3
"""Tests for the native-retirement feature/writer ownership cutover."""

from __future__ import annotations

import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import textwrap
import unittest
from unittest import mock


MODULE_PATH = Path(__file__).with_name("native_retirement_integration.py")
SPEC = importlib.util.spec_from_file_location("native_retirement_integration", MODULE_PATH)
integration = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
sys.modules[SPEC.name] = integration
SPEC.loader.exec_module(integration)


def git(repo: Path, *arguments: str, input_text: str | None = None) -> str:
    command = ["git", "-c", "core.hooksPath=/dev/null", "-C", os.fspath(repo), *arguments]
    result = subprocess.run(command, input=input_text, text=True, stdout=subprocess.PIPE,
                            stderr=subprocess.PIPE)
    if result.returncode != 0:
        raise AssertionError("git failed: " + " ".join(arguments) + "\n" + result.stderr)
    return result.stdout.strip()


class Repository:
    def __init__(self, root: Path):
        self.root = root
        self.remote = root / "remote.git"
        self.repo = root / "repo"
        git(root, "init", "--bare", os.fspath(self.remote))
        git(root, "init", "-b", "main", os.fspath(self.repo))
        git(self.repo, "config", "user.name", "Test User")
        git(self.repo, "config", "user.email", "test@example.invalid")
        git(self.repo, "remote", "add", "origin", os.fspath(self.remote))
        self._write_initial_tree()
        git(self.repo, "add", ".")
        git(self.repo, "commit", "-m", "base")
        git(self.repo, "push", "origin", "main")
        self.base = git(self.repo, "rev-parse", "HEAD")
        git(self.remote, "symbolic-ref", "HEAD", "refs/heads/main")

    def _write_initial_tree(self):
        paths = {
            "src/buster/lib/value.c": "int value = 1;\n",
            "docs/native-retirement-repository-sources-v1.json": "{}\n",
            "tools/native_retirement_dependency_binding.generated.h": "#define OLD 1\n",
        }
        for relative, content in paths.items():
            path = self.repo / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(content)
        self._write_fake_trusted_tools()

    def _write_fake_trusted_tools(self):
        tools = self.repo / "tools"
        tools.mkdir(parents=True, exist_ok=True)
        for name in integration.TRUSTED_FILE_PATHS:
            path = self.repo / name
            path.parent.mkdir(parents=True, exist_ok=True)
            if path.name != "native_retirement_rebind.py":
                path.write_text("# trusted test fixture: " + name + "\n")
        script = tools / "native_retirement_rebind.py"
        script.write_text(textwrap.dedent("""\
            #!/usr/bin/env python3
            import argparse, hashlib, json
            from pathlib import Path
            parser = argparse.ArgumentParser()
            parser.add_argument("mode", choices=("check", "refresh"))
            parser.add_argument("--repo-root", type=Path, required=True)
            args = parser.parse_args()
            source = (args.repo_root / "src/buster/lib/value.c").read_bytes()
            digest = hashlib.sha256(source).hexdigest()
            snapshot = (json.dumps({"source": "src/buster/lib/value.c", "sha256": digest},
                                   sort_keys=True) + "\\n").encode()
            header = ("#define VALUE_SHA256 \\\"" + digest + "\\\"\\n").encode()
            targets = {
                "docs/native-retirement-repository-sources-v1.json": snapshot,
                "tools/native_retirement_dependency_binding.generated.h": header,
            }
            stale = any(not (args.repo_root / path).is_file() or
                        (args.repo_root / path).read_bytes() != data
                        for path, data in targets.items())
            if args.mode == "refresh" and stale:
                for path, data in targets.items():
                    target = args.repo_root / path
                    target.parent.mkdir(parents=True, exist_ok=True)
                    target.write_bytes(data)
                stale = False
                status = "refreshed"
            else:
                status = "stale" if stale else "current"
            report = {
                "status": status,
                "identities": {
                    "policy_sha256": "1" * 64,
                    "receipt_sha256": "2" * 64,
                    "project_sha256": digest,
                    "ledger_sha256": "3" * 64,
                },
                "snapshot_sha256": hashlib.sha256(snapshot).hexdigest(),
                "resolved_descriptor_sha256": "4" * 64,
            }
            print(json.dumps(report, sort_keys=True))
            raise SystemExit(2 if args.mode == "check" and stale else 0)
        """))

    def branch(self, name: str, changes: dict[str, str]) -> str:
        git(self.repo, "checkout", "-B", name, self.base)
        for relative, content in changes.items():
            path = self.repo / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(content)
        git(self.repo, "add", ".")
        git(self.repo, "commit", "-m", name)
        head = git(self.repo, "rev-parse", "HEAD")
        git(self.repo, "push", "-f", "origin", name)
        return head

    def pull_ref(self, number: int, head: str):
        git(self.repo, "push", "--force", "origin",
            head + ":refs/pull/" + str(number) + "/head")

    def advance_main(self, text: str = "main moved\n") -> str:
        git(self.repo, "checkout", "-B", "main", self.base)
        (self.repo / "README.move").write_text(text)
        git(self.repo, "add", "README.move")
        git(self.repo, "commit", "-m", "move main")
        commit = git(self.repo, "rev-parse", "HEAD")
        git(self.repo, "push", "origin", "main")
        return commit


class ClassificationTests(unittest.TestCase):
    def test_ordinary_feature_and_siblings_do_not_own_generated_state(self):
        first = integration.classify_paths(["src/buster/lib/a.c", "tests/a.c"])
        second = integration.classify_paths(["src/buster/lib/b.c"])
        self.assertEqual(first.kind, "ordinary")
        self.assertEqual(second.kind, "ordinary")
        self.assertFalse(set(first.changed_paths) & integration.GENERATED_PATHS)
        self.assertFalse(set(second.changed_paths) & integration.GENERATED_PATHS)

    def test_manual_generated_edit_is_actionable_failure(self):
        report = integration.classify_paths([
            "src/buster/lib/value.c",
            "tools/native_retirement_dependency_binding.generated.h",
        ])
        with self.assertRaisesRegex(integration.IntegrationError, "integration-owned"):
            integration.enforce_classification(report, None, True)

    def test_authorized_bootstrap_and_policy_routes_are_distinct(self):
        bootstrap = integration.classify_paths(["tools/native_retirement_rebind.py"])
        policy = integration.classify_paths(["docs/native-retirement-dependencies-v1.json"])
        self.assertEqual(bootstrap.kind, "bootstrap")
        self.assertEqual(policy.kind, "policy")
        with self.assertRaisesRegex(integration.IntegrationError, "requires"):
            integration.enforce_classification(bootstrap, "bootstrap", False)
        integration.enforce_classification(bootstrap, "bootstrap", True)
        integration.enforce_classification(policy, "policy", True)

    def test_candidate_cannot_change_its_authority_and_policy_together(self):
        report = integration.classify_paths([
            "tools/native_retirement_rebind.py",
            "docs/native-retirement-dependencies-v1.json",
        ])
        self.assertEqual(report.kind, "split-required")
        with self.assertRaisesRegex(integration.IntegrationError, "split"):
            integration.enforce_classification(report, None, True)

    def test_noncanonical_paths_fail_closed(self):
        for path in ("../policy", "/absolute", "a/../b"):
            with self.subTest(path=path), self.assertRaises(integration.IntegrationError):
                integration.classify_paths([path])


class FakeGitHub:
    repository = "buster14a/buster"

    def __init__(self, head: str, reviews=()):
        self.head = head
        self.base = "b" * 40
        self.reviews = list(reviews)
        self.permissions = {
            "dispatcher": "admin",
            "author": "admin",
            "reviewer": "maintain",
        }

    def request(self, path: str):
        if path == "git/ref/heads/main":
            return {"object": {"sha": self.base}}
        if path != "pulls/864":
            raise AssertionError(path)
        return {
            "state": "open",
            "draft": False,
            "base": {"ref": "main", "repo": {"full_name": self.repository}},
            "head": {"sha": self.head, "repo": {"full_name": self.repository}},
            "user": {"login": "author"},
        }

    def all(self, path: str):
        if path != "pulls/864/reviews":
            raise AssertionError(path)
        return list(self.reviews)

    def permission(self, login: str) -> str:
        return self.permissions[login]


class AuthorizationTests(unittest.TestCase):
    def test_github_permission_uses_exact_role_name_for_maintainers(self):
        api = object.__new__(integration.GitHub)
        api.request = lambda _path: {"permission": "write", "role_name": "maintain"}
        self.assertEqual(api.permission("reviewer"), "maintain")

    def test_ordinary_dispatch_requires_exact_open_same_repository_head(self):
        head = "a" * 40
        report = integration.authorize(FakeGitHub(head), 864, head, "ordinary",
                                       "dispatcher")
        self.assertEqual(report["head"], head)
        with self.assertRaisesRegex(integration.IntegrationError, "head SHA changed"):
            integration.authorize(FakeGitHub(head), 864, "b" * 40, "ordinary",
                                  "dispatcher")

    def test_bootstrap_approval_must_be_independent_and_on_exact_head(self):
        head = "c" * 40
        stale = FakeGitHub(head, [{
            "id": 1, "state": "APPROVED", "commit_id": "d" * 40,
            "user": {"login": "reviewer"},
        }])
        with self.assertRaisesRegex(integration.IntegrationError, "exact immutable"):
            integration.authorize(stale, 864, head, "bootstrap", "dispatcher")
        current = FakeGitHub(head, [
            {"id": 1, "state": "APPROVED", "commit_id": "d" * 40,
             "user": {"login": "reviewer"}},
            {"id": 2, "state": "APPROVED", "commit_id": head,
             "user": {"login": "reviewer"}},
            {"id": 3, "state": "APPROVED", "commit_id": head,
             "user": {"login": "author"}},
        ])
        report = integration.authorize(current, 864, head, "bootstrap",
                                       "dispatcher")
        self.assertEqual(report["maintainer_approvals"], ["reviewer"])

    def solo_context(self):
        return {
            "configured_login": "author",
            "expected_base": "b" * 40,
            "event_name": "workflow_dispatch",
            "workflow_ref": "buster14a/buster/.github/workflows/native-retirement-integration.yml@refs/heads/main",
            "workflow_sha": "b" * 40,
            "triggering_actor": "author",
            "run_attempt": "1",
            "run_id": "123",
        }

    def test_solo_admin_can_authorize_own_exact_candidate_without_claiming_review(self):
        for kind in ("bootstrap", "policy"):
            with self.subTest(kind=kind):
                head = "a" * 40
                report = integration.authorize(
                    FakeGitHub(head), 864, head, kind, "author",
                    authorization_mode="solo-maintainer", solo_context=self.solo_context(),
                )
                self.assertEqual(report["head"], head)
                self.assertEqual(report["maintainer_approvals"], [])
                self.assertEqual(report["authorization"]["mode"], "solo-maintainer")
                self.assertEqual(report["authorization"]["base"], "b" * 40)
                self.assertEqual(report["authorization"]["run_id"], "123")

    def test_solo_configuration_never_implicitly_replaces_independent_review(self):
        with self.assertRaisesRegex(integration.IntegrationError, "approving maintainer"):
            integration.authorize(FakeGitHub("a" * 40), 864, "a" * 40,
                                  "bootstrap", "author", solo_context=self.solo_context())

    def test_solo_rejects_missing_opt_in_wrong_actor_ref_reruns_and_bad_identities(self):
        cases = (
            ("configured_login", ""), ("configured_login", "dispatcher"),
            ("event_name", "pull_request"), ("event_name", "pull_request_target"),
            ("workflow_ref", "buster14a/buster/.github/workflows/native-retirement-integration.yml@refs/heads/feature"),
            ("workflow_ref", "elsewhere/repo/.github/workflows/native-retirement-integration.yml@refs/heads/main"),
            ("triggering_actor", "dispatcher"), ("triggering_actor", ""),
            ("run_attempt", "2"), ("run_attempt", ""),
            ("expected_base", "main"), ("expected_base", ""),
            ("workflow_sha", "c" * 40), ("workflow_sha", ""),
            ("run_id", ""), ("run_id", "0"), ("run_id", "12x"),
        )
        for field, value in cases:
            with self.subTest(field=field, value=value):
                context = dict(self.solo_context(), **{field: value})
                with self.assertRaises(integration.IntegrationError):
                    integration.authorize(
                        FakeGitHub("a" * 40), 864, "a" * 40, "bootstrap", "author",
                        authorization_mode="solo-maintainer", solo_context=context,
                    )

    def test_solo_rechecks_current_main_head_and_admin_permission_before_publication(self):
        for change in ("main", "head", "permission"):
            with self.subTest(change=change):
                api = FakeGitHub("a" * 40)
                arguments = dict(authorization_mode="solo-maintainer", solo_context=self.solo_context())
                integration.authorize(api, 864, "a" * 40, "bootstrap", "author", **arguments)
                if change == "main":
                    api.base = "c" * 40
                elif change == "head":
                    api.head = "c" * 40
                else:
                    api.permissions["author"] = "maintain"
                with self.assertRaises(integration.IntegrationError):
                    integration.authorize(api, 864, "a" * 40, "bootstrap", "author", **arguments)

    def test_solo_rejects_missing_context_and_ordinary_or_unknown_modes(self):
        for kind, mode, context in (
            ("bootstrap", "solo-maintainer", None),
            ("ordinary", "solo-maintainer", self.solo_context()),
            ("unknown", "solo-maintainer", self.solo_context()),
            ("bootstrap", "unknown", self.solo_context()),
        ):
            with self.subTest(kind=kind, mode=mode, context=context):
                with self.assertRaises(integration.IntegrationError):
                    integration.authorize(
                        FakeGitHub("a" * 40), 864, "a" * 40, kind, "author",
                        authorization_mode=mode, solo_context=context,
                    )

    def test_solo_cli_uses_github_runtime_context(self):
        context = self.solo_context()
        environment = {
            "GITHUB_REPOSITORY": "buster14a/buster", "GH_TOKEN": "fixture",
            "GITHUB_ACTOR": "author",
            "NATIVE_RETIREMENT_SOLO_MAINTAINER": context["configured_login"],
            "GITHUB_EVENT_NAME": context["event_name"],
            "GITHUB_WORKFLOW_REF": context["workflow_ref"],
            "GITHUB_WORKFLOW_SHA": context["workflow_sha"],
            "GITHUB_TRIGGERING_ACTOR": context["triggering_actor"],
            "GITHUB_RUN_ATTEMPT": context["run_attempt"],
            "GITHUB_RUN_ID": context["run_id"],
        }
        args = ["authorize", "--pull-request", "864", "--expected-head", "a" * 40,
                "--transition-kind", "bootstrap", "--authorization-mode", "solo-maintainer",
                "--expected-base", context["expected_base"]]
        with mock.patch.dict(os.environ, environment, clear=True), \
                mock.patch.object(integration, "GitHub", return_value=FakeGitHub("a" * 40)), \
                mock.patch("builtins.print") as output:
            self.assertEqual(integration.main(args), 0)
            report = json.loads(output.call_args.args[0])
            self.assertEqual(report["authorization"]["mode"], "solo-maintainer")

    def test_workflow_reauthorizes_same_explicit_policy_and_retains_both_records(self):
        root = Path(__file__).resolve().parents[1]
        workflow = (root / ".github/workflows/native-retirement-integration.yml").read_text()
        self.assertIn("default: independent-review", workflow)
        self.assertIn("${{ vars.NATIVE_RETIREMENT_SOLO_MAINTAINER }}", workflow)
        prepare, publish = workflow.split("  publish:\n", 1)
        for job in (prepare, publish):
            self.assertIn('--authorization-mode "$AUTHORIZATION_MODE"', job)
            self.assertIn('--expected-base "$EXPECTED_BASE"', job)
            self.assertIn('tee "$RUNNER_TEMP/native-retirement/authorization.json"', job)
            self.assertIn("${{ runner.temp }}/native-retirement/authorization.json", job)


class IntegrationTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.repository = Repository(self.root)

    def prepare(self, head: str, name: str, base: str | None = None,
                kind: str = "ordinary", authorized: bool = False):
        repo = self.repository.repo
        base = self.repository.base if base is None else base
        trusted = self.root / (name + "-trusted")
        worktree = self.root / name
        integration._git(repo, "worktree", "add", "--detach", os.fspath(trusted), base)
        self.addCleanup(integration._git, repo, "worktree", "remove", "--force",
                        os.fspath(trusted), check=False)
        self.addCleanup(integration._git, repo, "worktree", "remove", "--force",
                        os.fspath(worktree), check=False)
        staged = integration.stage(repo, worktree, base, head, kind, authorized)
        evidence = integration.finalize(repo, worktree, trusted, base, staged)
        return worktree, staged, evidence

    def test_ephemeral_and_writer_reconstruction_are_exactly_equal(self):
        head = self.repository.branch("feature", {"src/buster/lib/value.c": "int value = 2;\n"})
        first_tree, _first_stage, first = self.prepare(head, "ephemeral")
        second_tree, _second_stage, second = self.prepare(head, "writer")
        integration.compare_evidence(first, second)
        self.assertEqual(first["final_tree"], second["final_tree"])
        self.assertEqual(first["generated"], second["generated"])
        self.assertEqual(first["next_trusted"], second["next_trusted"])

    def test_idempotent_retry_of_immutable_combined_tree(self):
        head = self.repository.branch("retry", {"src/buster/lib/value.c": "int value = 3;\n"})
        _first_tree, _first_stage, first = self.prepare(head, "retry-one")
        _second_tree, _second_stage, second = self.prepare(head, "retry-two")
        self.assertEqual(integration.evidence_digest(first), integration.evidence_digest(second))

    def test_second_refresh_worktree_mutation_fails_idempotence(self):
        head = self.repository.branch("non-idempotent", {
            "src/buster/lib/value.c": "int value = 31;\n"})
        repo = self.repository.repo
        trusted = self.root / "non-idempotent-trusted"
        worktree = self.root / "non-idempotent-tree"
        integration._git(repo, "worktree", "add", "--detach", os.fspath(trusted),
                         self.repository.base)
        self.addCleanup(integration._git, repo, "worktree", "remove", "--force",
                        os.fspath(trusted), check=False)
        self.addCleanup(integration._git, repo, "worktree", "remove", "--force",
                        os.fspath(worktree), check=False)
        staged = integration.stage(repo, worktree, self.repository.base, head,
                                   "ordinary", False)
        original = integration._run_rebinder
        calls = []

        def changing_rebinder(trusted_root, candidate_root, mode):
            report = original(trusted_root, candidate_root, mode)
            calls.append(mode)
            if calls == ["refresh", "check", "refresh"]:
                target = candidate_root / next(iter(sorted(integration.GENERATED_PATHS)))
                target.write_bytes(target.read_bytes() + b"mutated after current report\n")
            return report

        with mock.patch.object(integration, "_run_rebinder", side_effect=changing_rebinder):
            with self.assertRaisesRegex(integration.IntegrationError, "byte-identical"):
                integration.finalize(repo, worktree, trusted, self.repository.base, staged)

    def test_authorized_bootstrap_uses_old_authority_and_records_next_authority(self):
        base_tool = (self.repository.repo / "tools/native_retirement_rebind.py").read_text()
        head = self.repository.branch("bootstrap", {
            "tools/native_retirement_rebind.py": base_tool + "# compatible next authority\n",
        })
        worktree, staged, evidence = self.prepare(
            head, "bootstrap-tree", kind="bootstrap", authorized=True)
        self.assertEqual(staged["classification"]["kind"], "bootstrap")
        trusted_digest = evidence["trusted"]["files"]["tools/native_retirement_rebind.py"]
        next_digest = evidence["next_trusted"]["files"]["tools/native_retirement_rebind.py"]
        self.assertNotEqual(trusted_digest, next_digest)
        result = subprocess.run(
            [sys.executable, os.fspath(worktree / "tools/native_retirement_rebind.py"),
             "check", "--repo-root", os.fspath(worktree)],
            text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
        )
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_authorized_policy_transition_is_distinct_from_bootstrap(self):
        head = self.repository.branch("policy", {
            "docs/native-retirement-dependencies-v1.json": '{"policy": "reviewed"}\n',
        })
        _worktree, staged, evidence = self.prepare(
            head, "policy-tree", kind="policy", authorized=True)
        self.assertEqual(staged["classification"]["kind"], "policy")
        self.assertEqual(evidence["trusted"]["files"], evidence["next_trusted"]["files"])

    def test_two_sibling_candidates_refresh_only_their_combined_tree(self):
        first_head = self.repository.branch("sibling-a", {"src/buster/lib/value.c": "int value = 4;\n"})
        second_head = self.repository.branch("sibling-b", {"src/buster/lib/value.c": "int value = 5;\n"})
        _first_tree, _first_stage, first = self.prepare(first_head, "sibling-one")
        _second_tree, _second_stage, second = self.prepare(second_head, "sibling-two")
        self.assertNotEqual(first["final_tree"], second["final_tree"])
        self.assertNotEqual(first["generated"], second["generated"])
        self.assertEqual(first["classification"]["kind"], "ordinary")
        self.assertEqual(second["classification"]["kind"], "ordinary")

    def test_second_sibling_is_reconstructed_from_main_containing_the_first(self):
        first_head = self.repository.branch(
            "first-lands", {"src/buster/lib/value.c": "int value = 10;\n"})
        second_head = self.repository.branch(
            "second-sibling", {"README.second": "second sibling\n"})
        self.repository.pull_ref(20, first_head)
        _first_tree, _first_stage, first = self.prepare(first_head, "first-integration")
        published = integration.publish(self.repository.repo, "origin", "main", 20, first,
                                        "Integrate first sibling")

        _old_tree, _old_stage, old_second = self.prepare(
            second_head, "second-old-base", self.repository.base)
        _new_tree, _new_stage, new_second = self.prepare(
            second_head, "second-new-base", published["commit"])
        self.assertEqual(new_second["base"]["sha"], published["commit"])
        self.assertNotEqual(old_second["generated"], new_second["generated"])
        self.assertEqual(new_second["classification"]["kind"], "ordinary")

    def test_candidate_head_movement_discards_prepared_publication(self):
        head = self.repository.branch("head-moved", {
            "src/buster/lib/value.c": "int value = 11;\n"})
        self.repository.pull_ref(21, head)
        _worktree, _staged, evidence = self.prepare(head, "head-moved-tree")
        replacement = self.repository.branch("head-replacement", {
            "src/buster/lib/value.c": "int value = 12;\n"})
        self.repository.pull_ref(21, replacement)
        with self.assertRaises(integration.StaleHead):
            integration.publish(self.repository.repo, "origin", "main", 21, evidence,
                                "Integrate stale head")
        self.assertEqual(integration._remote_ref(
            self.repository.repo, "origin", "refs/heads/main"), self.repository.base)

    def test_main_movement_discards_prepared_publication(self):
        head = self.repository.branch("stale", {"src/buster/lib/value.c": "int value = 6;\n"})
        self.repository.pull_ref(17, head)
        _worktree, _staged, evidence = self.prepare(head, "stale-tree")
        moved = self.repository.advance_main()
        with self.assertRaises(integration.StaleMain):
            integration.publish(self.repository.repo, "origin", "main", 17, evidence,
                                "Integrate stale candidate")
        self.assertEqual(integration._remote_ref(self.repository.repo, "origin", "refs/heads/main"), moved)

    def test_failed_push_leaves_main_unchanged(self):
        head = self.repository.branch("rejected", {"src/buster/lib/value.c": "int value = 7;\n"})
        self.repository.pull_ref(18, head)
        _worktree, _staged, evidence = self.prepare(head, "rejected-tree")
        hook = self.repository.remote / "hooks/pre-receive"
        hook.write_text("#!/bin/sh\nexit 1\n")
        hook.chmod(0o755)
        before = integration._remote_ref(self.repository.repo, "origin", "refs/heads/main")
        with self.assertRaises(integration.IntegrationError):
            integration.publish(self.repository.repo, "origin", "main", 18, evidence,
                                "Integrate rejected candidate")
        self.assertEqual(integration._remote_ref(self.repository.repo, "origin", "refs/heads/main"), before)

    def test_single_atomic_publication_contains_candidate_and_generated_state(self):
        head = self.repository.branch("publish", {"src/buster/lib/value.c": "int value = 8;\n"})
        self.repository.pull_ref(19, head)
        _worktree, _staged, evidence = self.prepare(head, "publish-tree")
        result = integration.publish(self.repository.repo, "origin", "main", 19, evidence,
                                     "Integrate pull request #19")
        self.assertEqual(result["tree"], evidence["final_tree"])
        self.assertEqual(integration._remote_ref(self.repository.repo, "origin", "refs/heads/main"),
                         result["commit"])
        parents = git(self.repository.repo, "show", "-s", "--format=%P", result["commit"]).split()
        self.assertEqual(parents, [self.repository.base, head])

    def test_trusted_path_rename_is_classified_by_its_deleted_source(self):
        repo = self.repository.repo
        git(repo, "checkout", "-B", "rename-trust", self.repository.base)
        git(repo, "mv", "tools/native_retirement_rebind.py", "tools/renamed_rebinder.py")
        git(repo, "commit", "-m", "rename trusted rebinder")
        head = git(repo, "rev-parse", "HEAD")
        report = integration.classify_candidate(repo, self.repository.base, head)
        self.assertEqual(report.kind, "bootstrap")
        self.assertIn("tools/native_retirement_rebind.py", report.implementation_paths)

    def test_main_move_in_push_race_is_reported_as_stale(self):
        head = self.repository.branch("push-race", {
            "src/buster/lib/value.c": "int value = 13;\n"})
        self.repository.pull_ref(22, head)
        _worktree, _staged, evidence = self.prepare(head, "push-race-tree")
        original_git = integration._git
        moved = []

        def racing_git(repo, *arguments, **keywords):
            if arguments and arguments[0] == "push" and not moved:
                moved.append(self.repository.advance_main("push race\n"))
            return original_git(repo, *arguments, **keywords)

        with mock.patch.object(integration, "_git", side_effect=racing_git):
            with self.assertRaises(integration.StaleMain):
                integration.publish(self.repository.repo, "origin", "main", 22, evidence,
                                    "Integrate racing candidate")
        self.assertEqual(integration._remote_ref(
            self.repository.repo, "origin", "refs/heads/main"), moved[0])

    def test_cancel_before_push_has_no_partial_state(self):
        head = self.repository.branch("cancel", {"src/buster/lib/value.c": "int value = 9;\n"})
        _worktree, _staged, _evidence = self.prepare(head, "cancel-tree")
        self.assertEqual(integration._remote_ref(self.repository.repo, "origin", "refs/heads/main"),
                         self.repository.base)


class WorkflowPolicyTests(unittest.TestCase):
    def setUp(self):
        self.root = Path(__file__).resolve().parents[1]
        self.workflows = self.root / ".github/workflows"

    def test_single_writer_is_default_branch_manual_and_serialized(self):
        text = (self.workflows / "native-retirement-integration.yml").read_text()
        event_block = text.split("on:\n", 1)[1].split("\npermissions:", 1)[0]
        self.assertIn("  workflow_dispatch:", event_block)
        self.assertNotRegex(event_block, r"(?m)^  pull_request(?:_target)?:")
        self.assertNotRegex(event_block, r"(?m)^  push:")
        self.assertNotIn("pull_request_target", text)
        self.assertIn("group: native-retirement-integration-writer", text)
        self.assertIn("cancel-in-progress: false", text)
        self.assertIn("github.ref == 'refs/heads/main'", text)
        self.assertEqual(text.count("contents: write"), 1)
        self.assertIn("environment: native-retirement-integration", text)
        self.assertNotIn("continue-on-error", text)

    def test_pr_publication_attests_before_leased_head_update(self):
        text = (self.workflows / "native-retirement-integration.yml").read_text()
        publish = text.split("\n  publish:\n", 1)[1]
        self.assertIn("statuses: write", publish)
        self.assertIn('test "$head_repo" = "$GITHUB_REPOSITORY"', publish)
        self.assertIn('commit-tree "$final_tree" -p "$base" -p "$head"', publish)
        self.assertIn('--force-with-lease="refs/heads/$head_ref:$head"', publish)
        self.assertIn('test "$main_remote" = "$base"', publish)
        self.assertIn('test "$pull_remote" = "$head"', publish)
        self.assertLess(publish.index("Native retirement trusted integration"),
                        publish.index('--force-with-lease="refs/heads/$head_ref:$head"'))
        self.assertNotIn('"$commit:refs/heads/main"', publish)
        self.assertNotIn('native_retirement_integration.py publish', publish)

    def test_write_job_never_executes_candidate_tests(self):
        text = (self.workflows / "native-retirement-integration.yml").read_text()
        publish = text.split("\n  publish:\n", 1)[1]
        self.assertIn("contents: write", publish)
        self.assertIn("without executing it", publish)
        self.assertIn("trusted/tools/native_retirement_integration.py finalize", publish)
        self.assertIn("--force-with-lease", (self.root / "tools/native_retirement_integration.py").read_text())
        self.assertNotIn("native_retirement_integration_test.py", publish)
        self.assertNotIn("native_retirement_rebind_test.py", publish)
        self.assertNotIn("native_retirement_contract_test.py", publish)
        self.assertNotIn("working-directory: integration", publish)
        self.assertIn("persist-credentials: false", publish)

    def test_feature_and_contract_jobs_use_ephemeral_trusted_generation(self):
        rebind = (self.workflows / "native-retirement-rebind.yml").read_text()
        contract = (self.workflows / "native-retirement-contract.yml").read_text()
        self.assertIn("Reject feature-owned generated state", rebind)
        self.assertIn("trusted/tools/native_retirement_rebind.py refresh", rebind)
        self.assertIn("candidate/tools/native_retirement_rebind.py check", rebind)
        self.assertIn("integration-owned generated artifacts",
                      (self.root / "tools/native_retirement_integration.py").read_text())
        self.assertIn(".native-retirement-trusted/tools/native_retirement_rebind.py refresh", contract)
        self.assertIn("tools/native_retirement_rebind.py check", contract)
        writer = (self.workflows / "native-retirement-integration.yml").read_text()
        self.assertIn("Prove the candidate authority accepts the trusted final state", writer)


if __name__ == "__main__":
    unittest.main()
