#!/usr/bin/env python3
"""Deterministic queue-admission fixtures; no API writes or live queue claims."""

import json
from pathlib import Path
import subprocess
import tempfile
import unittest
from types import SimpleNamespace
from unittest.mock import patch

import merge_queue_admission as gate

ROOT = Path(__file__).resolve().parents[1]


def candidate():
    return {"repository": "buster14a/buster", "base": "a" * 40, "head": "b" * 40,
            "head_ref": "refs/heads/gh-readonly-queue/main/pr-1-example"}


def live_rules():
    data = json.loads((ROOT / ".github/main-merge-queue.ruleset.json").read_text())
    data.update(id=gate.RULESET_ID, source_type="Repository", source="buster14a/buster")
    # The actual read-only Actions response does not reveal bypass actors.
    del data["bypass_actors"]
    return data


def results():
    expected = candidate()
    rows, jobs = [], {}
    for index, (filename, context) in enumerate(gate.CHECKS.items(), 1):
        run = {"id": index, "run_attempt": 1, "path": ".github/workflows/" + filename,
               "head_sha": expected["head"], "event": "merge_group", "status": "completed",
               "conclusion": "success", "repository": {"full_name": expected["repository"]},
               "head_branch": expected["head_ref"].removeprefix("refs/heads/")}
        rows.append(run)
        jobs[(index, 1)] = [{"id": 100 + index, "run_id": index, "run_attempt": 1,
                             "name": context, "head_sha": expected["head"],
                             "status": "completed", "conclusion": "success"}]
    return rows, jobs


class ResultsTests(unittest.TestCase):
    def evaluate(self, rows, jobs):
        return gate.check_results(gate.latest_runs(rows, candidate()), jobs, candidate())

    def test_all_six_exact_results(self):
        evidence, pending = self.evaluate(*results())
        self.assertEqual(len(evidence), 6)
        self.assertEqual(pending, [])

    def test_every_required_job_failure_skip_cancel_neutral_and_missing(self):
        for index in range(1, 7):
            for conclusion in ("failure", "cancelled", "skipped", "neutral", "timed_out", None):
                with self.subTest(index=index, conclusion=conclusion):
                    rows, jobs = results()
                    jobs[(index, 1)][0]["conclusion"] = conclusion
                    with self.assertRaises(gate.AdmissionError):
                        self.evaluate(rows, jobs)
            rows, jobs = results()
            del jobs[(index, 1)]
            with self.assertRaises(gate.AdmissionError):
                self.evaluate(rows, jobs)

    def test_wrong_head_event_repository_or_ref_never_authorizes(self):
        for key, value in (("head_sha", "c" * 40), ("event", "pull_request"),
                           ("repository", {"full_name": "other/repo"}), ("head_branch", "main")):
            with self.subTest(key=key):
                rows, jobs = results()
                rows[0][key] = value
                with self.assertRaises(gate.AdmissionError):
                    self.evaluate(rows, jobs)

    def test_different_workflow_with_same_job_name_is_not_evidence(self):
        rows, jobs = results()
        rows[0]["path"] = ".github/workflows/untrusted.yml"
        self.assertTrue(self.evaluate(rows, jobs)[1])

    def test_newer_rerun_hides_old_success(self):
        rows, jobs = results()
        newer = dict(rows[0], run_attempt=2, status="in_progress", conclusion=None)
        rows.append(newer)
        self.assertTrue(self.evaluate(rows, jobs)[1])
        newer.update(status="completed", conclusion="cancelled")
        with self.assertRaises(gate.AdmissionError):
            self.evaluate(rows, jobs)

    def test_new_run_hides_old_success(self):
        rows, jobs = results()
        rows.append(dict(rows[0], id=1000, status="queued", conclusion=None))
        self.assertTrue(self.evaluate(rows, jobs)[1])

    def test_job_attempt_and_head_are_independently_bound(self):
        for key, value in (("run_id", 900), ("run_attempt", 2), ("head_sha", "c" * 40)):
            rows, jobs = results()
            jobs[(1, 1)][0][key] = value
            with self.assertRaises(gate.AdmissionError):
                self.evaluate(rows, jobs)

    def test_duplicate_required_job_rejected(self):
        rows, jobs = results()
        jobs[(1, 1)].append(dict(jobs[(1, 1)][0], id=999))
        with self.assertRaises(gate.AdmissionError):
            self.evaluate(rows, jobs)

    def test_missing_workflow_waits_instead_of_green(self):
        rows, jobs = results()
        evidence, pending = self.evaluate(rows[:-1], jobs)
        self.assertEqual(len(evidence), 5)
        self.assertEqual(len(pending), 1)

    def test_optional_job_failure_does_not_add_a_required_gate(self):
        rows, jobs = results()
        rows[3]["conclusion"] = "failure"
        self.assertEqual(self.evaluate(rows, jobs)[1], [])

    def test_cancellation_cannot_reuse_a_completed_job(self):
        rows, jobs = results()
        rows[0]["conclusion"] = "cancelled"
        with self.assertRaises(gate.AdmissionError):
            self.evaluate(rows, jobs)

    def test_main_or_group_advance_invalidates_result(self):
        expected = candidate()
        gate.check_current(expected, expected["base"], expected["head"])
        for main, head in (("c" * 40, expected["head"]), (expected["base"], "c" * 40)):
            with self.assertRaises(gate.AdmissionError):
                gate.check_current(expected, main, head)

    def test_pagination_refuses_partial_api_evidence(self):
        api = gate.GitHub("buster14a/buster", "fixture-token")
        with patch.object(api, "get", return_value={"total_count": 2, "jobs": [{}]}):
            with self.assertRaises(gate.AdmissionError):
                api.pages("unused", "jobs")


class RulesTests(unittest.TestCase):
    def ruleset(self):
        return json.loads((ROOT / ".github/main-merge-queue.ruleset.json").read_text())

    def test_desired_ruleset(self):
        gate.validate_ruleset(self.ruleset())
        self.assertEqual(gate.QUEUE["max_entries_to_build"], 20)
        self.assertEqual(self.ruleset()["rules"][-1]["parameters"], gate.QUEUE)

    def test_readonly_omission_is_not_an_administrator_audit(self):
        data = live_rules()
        gate.validate_ruleset(data, read_only_response=True)
        with self.assertRaisesRegex(gate.AdmissionError, "inventory is hidden"):
            gate.validate_ruleset(data)
        self.assertNotIn("bypass_actors", data)

    def test_visible_bypass_and_malformed_inventory_fail_in_both_modes(self):
        for inventory in (None, {}, "", False, [{"actor_type": "OrganizationAdmin"}]):
            for read_only in (False, True):
                with self.subTest(inventory=inventory, read_only=read_only):
                    data = self.ruleset()
                    data["bypass_actors"] = inventory
                    with self.assertRaisesRegex(gate.AdmissionError, "standing bypasses"):
                        gate.validate_ruleset(data, read_only_response=read_only)

    def test_reader_bypass_authority_is_rejected(self):
        for authority in ("always", "pull_requests_only", None, ""):
            data = live_rules()
            data["current_user_can_bypass"] = authority
            with self.assertRaisesRegex(gate.AdmissionError, "bypass authority"):
                gate.validate_ruleset(data, read_only_response=True)

    def test_live_ruleset_identity_and_visibility(self):
        api = gate.GitHub("buster14a/buster", "fixture-token")
        for hidden in (False, True):
            data = live_rules()
            if not hidden:
                data["bypass_actors"] = []
            with patch.object(api, "get", return_value=data):
                report = gate.live_ruleset(api, "buster14a/buster")
                self.assertEqual(report["bypass_inventory"],
                                 "hidden" if hidden else "verified-empty")
        for key, value in (("id", 1), ("id", str(gate.RULESET_ID)),
                           ("source_type", "Organization"), ("source", "other/repo")):
            data = live_rules()
            data[key] = value
            with patch.object(api, "get", return_value=data):
                with self.assertRaisesRegex(gate.AdmissionError, "identity mismatch"):
                    gate.live_ruleset(api, "buster14a/buster")

    def test_hidden_inventory_does_not_relax_visible_protections(self):
        for change in ("strict", "missing", "app", "disabled", "scope", "queue"):
            data = live_rules()
            checks = data["rules"][3]["parameters"]
            if change == "strict":
                checks["strict_required_status_checks_policy"] = True
            elif change == "missing":
                checks["required_status_checks"].pop()
            elif change == "app":
                checks["required_status_checks"][0]["integration_id"] = 1
            elif change == "disabled":
                data["enforcement"] = "disabled"
            elif change == "scope":
                data["conditions"]["ref_name"]["include"] = ["~ALL"]
            else:
                data["rules"][-1]["parameters"]["max_entries_to_build"] = 2
            with self.subTest(change=change), self.assertRaises(gate.AdmissionError):
                gate.validate_ruleset(data, read_only_response=True)

    def test_each_required_check_remains_independently_required(self):
        for context in (*gate.CHECKS.values(), gate.RETIREMENT_CONTEXT, gate.CONTEXT):
            with self.subTest(context=context):
                data = self.ruleset()
                parameters = data["rules"][3]["parameters"]
                parameters["required_status_checks"] = [
                    row for row in parameters["required_status_checks"]
                    if row["context"] != context
                ]
                with self.assertRaises(gate.AdmissionError):
                    gate.validate_ruleset(data)

    def test_build_concurrency_is_exactly_twenty(self):
        for value in (1, 2, 4, 5, 6, 7, 10, 19, 21):
            data = self.ruleset()
            data["rules"][-1]["parameters"]["max_entries_to_build"] = value
            with self.subTest(value=value), self.assertRaisesRegex(
                    gate.AdmissionError, f"max_entries_to_build: expected 20, got {value}"):
                gate.validate_ruleset(data)

    def test_merge_batches_rewrites_or_headgreen_are_rejected(self):
        for key, value in (("max_entries_to_merge", 2),
                           ("grouping_strategy", "HEADGREEN"), ("merge_method", "SQUASH"),
                           ("merge_method", "REBASE")):
            data = self.ruleset()
            data["rules"][-1]["parameters"][key] = value
            with self.assertRaises(gate.AdmissionError):
                gate.validate_ruleset(data)

    def test_no_strict_updates_missing_checks_wrong_app_or_bypass(self):
        for change in ("strict", "missing", "app", "bypass", "disabled", "scope"):
            data = self.ruleset()
            checks = data["rules"][3]["parameters"]
            if change == "strict":
                checks["strict_required_status_checks_policy"] = True
            elif change == "missing":
                checks["required_status_checks"].pop()
            elif change == "app":
                checks["required_status_checks"][0]["integration_id"] = 1
            elif change == "bypass":
                data["bypass_actors"] = [{"actor_type": "OrganizationAdmin"}]
            elif change == "disabled":
                data["enforcement"] = "disabled"
            else:
                data["conditions"]["ref_name"]["include"] = ["~ALL"]
            with self.assertRaises(gate.AdmissionError):
                gate.validate_ruleset(data)

    def test_workflow_is_read_only_and_uses_trusted_base(self):
        text = (ROOT / ".github/workflows/merge-queue-admission.yml").read_text()
        self.assertNotIn(": write", text)
        self.assertNotIn("pull_request_target:", text)
        self.assertNotIn("secrets.", text)
        self.assertIn("github.event.merge_group.base_sha || github.sha", text)
        self.assertIn("persist-credentials: false", text)
        self.assertIn("github.event_name == 'push' && github.run_id", text)
        self.assertIn("check-group", text)


class CombinedTreeTests(unittest.TestCase):
    def test_two_siblings_require_new_combined_tree_and_conflicts_are_not_resolved(self):
        with tempfile.TemporaryDirectory() as temporary:
            repo = Path(temporary)
            gate.git(repo, "init", "-b", "main")
            gate.git(repo, "config", "user.name", "Queue fixture")
            gate.git(repo, "config", "user.email", "queue@example.invalid")
            for name in ("a.c", "b.c"):
                (repo / name).write_text("original\n")
            gate.git(repo, "add", ".")
            gate.git(repo, "commit", "-m", "base")
            base = gate.git(repo, "rev-parse", "HEAD")
            heads = []
            for name, branch in (("a.c", "first"), ("b.c", "second")):
                gate.git(repo, "checkout", "-b", branch, base)
                (repo / name).write_text(branch + "\n")
                gate.git(repo, "commit", "-am", branch)
                heads.append(gate.git(repo, "rev-parse", "HEAD"))
            old_tree = gate.git(repo, "merge-tree", "--write-tree", base, heads[1])
            new_tree = gate.git(repo, "merge-tree", "--write-tree", heads[0], heads[1])
            self.assertNotEqual(old_tree, new_tree)
            combined = gate.git(repo, "commit-tree", new_tree, "-p", heads[0], "-p", heads[1], "-m", "group")
            gate.git(repo, "checkout", "--detach", heads[0])
            event = {"action": "checks_requested", "repository": {"full_name": "buster14a/buster"},
                     "merge_group": {"base_ref": "refs/heads/main", "base_sha": heads[0],
                                     "head_sha": combined, "head_ref": candidate()["head_ref"]}}
            report = gate.identity(event, "buster14a/buster", combined, repo)
            self.assertEqual(report["final_tree"], new_tree)
            gate.check_current(report, heads[0], combined)
            with self.assertRaises(gate.AdmissionError):
                gate.check_current(dict(report, base=base), heads[0], combined)
            gate.git(repo, "checkout", "-b", "conflict", base)
            (repo / "a.c").write_text("conflicting change\n")
            gate.git(repo, "commit", "-am", "conflict")
            conflict = gate.git(repo, "rev-parse", "HEAD")
            attempt = subprocess.run(["git", "-C", str(repo), "merge-tree", "--write-tree", heads[0], conflict],
                                     capture_output=True, text=True, check=False)
            self.assertNotEqual(attempt.returncode, 0)
            self.assertIn("a.c", attempt.stdout)
            self.assertEqual(gate.git(repo, "rev-parse", "HEAD"), conflict)
            self.assertEqual((repo / "a.c").read_text(), "conflicting change\n")


class OrchestrationTests(unittest.TestCase):
    def test_missing_retirement_gate_cannot_be_skipped(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            event = root / "event.json"
            event.write_text("{}")
            arguments = SimpleNamespace(event=event, repository="buster14a/buster", sha="b" * 40,
                                        repo_root=root, wait_seconds=0)
            rules = live_rules()
            with patch.object(gate, "identity", return_value=candidate()), \
                    patch.object(gate, "live_identity"), patch.object(gate, "GitHub") as api:
                api.return_value.get.return_value = rules
                with self.assertRaisesRegex(gate.AdmissionError, "must land"):
                    gate.run_gate(arguments)

    def test_native_denial_or_pending_cannot_become_green(self):
        for returncode, report in ((1, {}), (0, {"status": "pending"}),
                                   (0, {"status": "admitted", "base": "c" * 40, "head": "b" * 40})):
            with tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary)
                (root / "tools").mkdir()
                (root / "tools/native_retirement_merge_gate.py").touch()
                event = root / "event.json"
                event.write_text("{}")
                arguments = SimpleNamespace(event=event, repository="buster14a/buster", sha="b" * 40,
                                            repo_root=root, wait_seconds=0)
                rules = live_rules()
                result = SimpleNamespace(returncode=returncode, stdout=json.dumps(report), stderr="denied")
                with patch.object(gate, "identity", return_value=candidate()), \
                        patch.object(gate, "live_identity"), patch.object(gate, "GitHub") as api, \
                        patch.object(gate.subprocess, "run", return_value=result), \
                        patch.object(gate, "collect") as collect:
                    api.return_value.get.return_value = rules
                    with self.assertRaises(gate.AdmissionError):
                        gate.run_gate(arguments)
                    collect.assert_not_called()

    def test_readonly_success_is_double_collected_and_identity_bound(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / "tools").mkdir()
            (root / "tools/native_retirement_merge_gate.py").touch()
            event = root / "event.json"
            event.write_text("{}")
            arguments = SimpleNamespace(event=event, repository="buster14a/buster", sha="b" * 40,
                                        repo_root=root, wait_seconds=0)
            rules = live_rules()
            native = {"status": "admitted", "base": "a" * 40, "head": "b" * 40}
            result = SimpleNamespace(returncode=0, stdout=json.dumps(native), stderr="")
            evidence = [{"run_id": 1, "run_attempt": 1}]
            with patch.object(gate, "identity", return_value=candidate()), \
                    patch.object(gate, "live_identity") as live, patch.object(gate, "GitHub") as api, \
                    patch.object(gate.subprocess, "run", return_value=result) as native_run, \
                    patch.object(gate, "collect", return_value=(evidence, [])) as collect:
                api.return_value.get.return_value = rules
                report = gate.run_gate(arguments)
                self.assertEqual(report["status"], "admitted")
                self.assertEqual(report["ruleset_reads"], [
                    {"id": gate.RULESET_ID, "bypass_inventory": "hidden"}] * 2)
                self.assertEqual(report["head"], "b" * 40)
                self.assertEqual(collect.call_count, 2)
                self.assertEqual(live.call_count, 3)
                self.assertEqual(api.return_value.get.call_count, 2)
                self.assertEqual(native_run.call_count, 2)
                self.assertNotIn("--allow-pending", native_run.call_args.args[0])
                self.assertIn("--repository", native_run.call_args.args[0])

    def test_publication_change_during_ci_rejects_admission(self):
        with tempfile.TemporaryDirectory() as temporary:
            event = Path(temporary) / "event.json"
            event.write_text("{}")
            arguments = SimpleNamespace(event=event, repository="buster14a/buster", sha="b" * 40,
                                        repo_root=Path(temporary), wait_seconds=0)
            rules = live_rules()
            with patch.object(gate, "identity", return_value=candidate()), \
                    patch.object(gate, "live_identity"), patch.object(gate, "GitHub") as api, \
                    patch.object(gate, "collect", return_value=([{"run_id": 1}], [])), \
                    patch.object(gate, "retirement_admission", side_effect=[
                        {"attestation_id": 1}, {"attestation_id": 2}]):
                api.return_value.get.return_value = rules
                with self.assertRaisesRegex(gate.AdmissionError, "publication changed"):
                    gate.run_gate(arguments)

    def test_ruleset_change_during_ci_rejects_admission(self):
        for change, reason in (("enforcement", "must be active"),
                               ("build concurrency", "max_entries_to_build: expected 20, got 1")):
            with self.subTest(change=change), tempfile.TemporaryDirectory() as temporary:
                event = Path(temporary) / "event.json"
                event.write_text("{}")
                arguments = SimpleNamespace(event=event, repository="buster14a/buster", sha="b" * 40,
                                            repo_root=Path(temporary), wait_seconds=0)
                changed = live_rules()
                if change == "enforcement":
                    changed["enforcement"] = "disabled"
                else:
                    changed["rules"][-1]["parameters"]["max_entries_to_build"] = 1
                with patch.object(gate, "identity", return_value=candidate()), \
                        patch.object(gate, "live_identity"), patch.object(gate, "GitHub") as api, \
                        patch.object(gate, "collect", return_value=([{"run_id": 1}], [])), \
                        patch.object(gate, "retirement_admission", return_value={"attestation_id": 1}):
                    api.return_value.get.side_effect = [live_rules(), changed]
                    with self.assertRaisesRegex(gate.AdmissionError, reason):
                        gate.run_gate(arguments)

    def test_workflow_inventory_rejects_removed_group_trigger_and_paths_filter(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            directory = root / ".github/workflows"
            directory.mkdir(parents=True)
            for filename, context in gate.CHECKS.items():
                (directory / filename).write_text(
                    "name: fixture\non:\n  pull_request:\n  merge_group:\n"
                    "    types: [checks_requested]\npermissions:\n  contents: read\njobs:\n"
                    "  gate:\n    name: " + context + "\n")
            gate.audit_workflows(root)
            path = directory / "ci.yml"
            original = path.read_text()
            for bad in (original.replace("  merge_group:\n", "  unused:\n"),
                        original.replace("  pull_request:\n", "  pull_request:\n    paths: ['src/**']\n")):
                path.write_text(bad)
                with self.assertRaises(gate.AdmissionError):
                    gate.audit_workflows(root)


if __name__ == "__main__":
    unittest.main()
