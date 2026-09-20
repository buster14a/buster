#!/usr/bin/env python3
"""Regression tests for the read-only merge-conflict preflight."""

from __future__ import annotations

import ast
import copy
import importlib.util
import json
from pathlib import Path
import statistics
import subprocess
import sys
import tempfile
import time
import unittest


REPO_ROOT = Path(__file__).resolve().parents[1]
TOOL_PATH = Path(__file__).with_name("merge_conflict_preflight.py")
WORKFLOW_PATH = REPO_ROOT / ".github" / "workflows" / "merge-conflict-preflight.yml"
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
        workflow = WORKFLOW_PATH.read_text(encoding="utf-8")
        self.assertIn("pull_request:\n", workflow)
        self.assertIn("pull_request_target:\n", workflow)
        self.assertIn("push:\n    branches: [main]", workflow)
        self.assertIn("merge_group:\n    types: [checks_requested]", workflow)
        self.assertIn("github.event_name == 'pull_request'", workflow)
        self.assertIn("github.event_name != 'pull_request'", workflow)
        self.assertIn("pull-requests: read\n      statuses: write", workflow)
        self.assertIn("ref: ${{ github.event.repository.default_branch }}", workflow)
        self.assertIn("persist-credentials: false", workflow)
        self.assertIn("python3 -B tools/merge_conflict_preflight_test.py -v", workflow)
        self.assertNotIn("github.event.pull_request.head", workflow)
        self.assertNotIn("github.head_ref", workflow)

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


if __name__ == "__main__":
    unittest.main()
