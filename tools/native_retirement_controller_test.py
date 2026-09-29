#!/usr/bin/env python3
"""Network-free controller and workflow regressions for #1791.

Reuse the bounded fake GitHub API from the authorization suite; no network or
candidate execution occurs while exercising request lifecycle decisions.
"""
import copy
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest import mock

from native_retirement_automation_test import (API, BASE, HEAD, SOURCE, BOT,
                                              REPOSITORY, CLASSIFICATION, ROOT, a, c, i)


class ControllerTests(unittest.TestCase):
    def setUp(self):
        self.api = API()
        self.download = mock.patch.object(a, "download_archive", side_effect=lambda api, number: api.raw_archive)
        self.download.start()
        self.addCleanup(self.download.stop)

    def test_human_comments_cannot_masquerade_as_controller_claims(self):
        self.api.claim()
        comment = self.api.comments[0]
        comment["user"] = {**BOT, "id": 1}
        self.assertIsNone(c.parse_ledger(comment, REPOSITORY))

    def test_duplicate_claims_fail_closed(self):
        self.api.claim()
        self.api.comments.append({**self.api.comments[0], "id": 2})
        with self.assertRaises(a.AutomationError):
            c.ledger(self.api, 1791)

    def test_successful_dispatch_is_one_post_and_recorded(self):
        self.api.claim()
        result = c.dispatch(self.api, self.api.request_data)
        self.assertEqual(result["status"], "dispatched")
        self.assertEqual(len(self.api.posts), 1)
        self.assertEqual(c.ledger(self.api, 1791)[0]["state"], "dispatched")
        with self.assertRaises(a.AutomationError):
            c.dispatch(self.api, self.api.request_data)
        self.assertEqual(len(self.api.posts), 1)

    def test_moved_input_before_post_releases_only_as_superseded(self):
        self.api.claim()
        self.api.base = "e" * 40
        with self.assertRaises(a.AutomationMoved):
            c.dispatch(self.api, self.api.request_data)
        record = c.ledger(self.api, 1791)[0]
        self.assertEqual(record["state"], "superseded")
        self.assertEqual(self.api.posts, [])
        request = a.new_request(REPOSITORY, 1791, "e" * 40, HEAD, SOURCE, "ordinary",
                                self.api.policy_digest, 101)
        self.assertEqual(c.disposition([record], request), "eligible")

    def test_manual_writer_starting_after_plan_uses_existing_serialization(self):
        self.api.claim()
        self.api.active = [self.api.runs[200]]
        self.assertEqual(c.dispatch(self.api, self.api.request_data)["status"], "dispatched")
        self.assertEqual(len(self.api.posts), 1)

    def test_dispatch_supports_returned_run_identity(self):
        self.api.claim()
        self.api.dispatch_response = {"workflow_run_id": 200}
        c.dispatch(self.api, self.api.request_data)
        self.assertEqual(c.ledger(self.api, 1791)[0]["run_id"], 200)

    def test_ambiguous_post_is_not_retried_and_claim_survives(self):
        self.api.claim()
        self.api.dispatch_error = OSError("response lost")
        with self.assertRaises(OSError):
            c.dispatch(self.api, self.api.request_data)
        self.assertEqual(c.ledger(self.api, 1791)[0]["state"], "claimed")
        self.api.runs[100].update(status="completed", conclusion="failure")
        record = c.reconcile(self.api, c.ledger(self.api, 1791)[0])
        self.assertEqual(record["state"], "blocked")
        self.assertEqual(len(self.api.posts), 1)

    def test_lost_receipt_recovers_exact_key_existing_writer(self):
        record = self.api.claim()
        self.api.writer_visible = True
        self.api.runs[100].update(status="completed", conclusion="failure")
        result = c.reconcile(self.api, record)
        self.assertEqual(result["state"], "dispatched")
        self.assertEqual(result["run_id"], 200)
        self.assertEqual(self.api.posts, [])

    def test_cancellation_blocks_same_source_after_main_advances(self):
        record = self.api.claim()
        self.api.writer_visible = True
        self.api.runs[200].update(status="completed", conclusion="cancelled")
        record = c.reconcile(self.api, record)
        self.assertEqual(record["state"], "blocked")
        request = a.new_request(REPOSITORY, 1791, "e" * 40, HEAD, SOURCE, "ordinary",
                                self.api.policy_digest, 101)
        self.assertEqual(c.disposition([record], request), "failed-or-cancelled-source")

    def test_real_failure_cannot_claim_supersession_from_arbitrary_job(self):
        self.api.runs[200].update(status="completed", conclusion="failure")
        self.api.outcomes = [{"name": "Candidate test", "conclusion": "success",
                              "steps": [{"name": "Superseded request", "conclusion": "success"}]}]
        self.assertEqual(c.writer_outcome(self.api, self.api.runs[200]), "blocked")

    def test_only_trusted_supersession_allows_new_identity(self):
        record = self.api.claim()
        self.api.writer_visible = True
        self.api.runs[200].update(status="completed", conclusion="failure")
        self.api.outcomes = [{"name": c.OUTCOME_JOB, "conclusion": "success",
                              "steps": [{"name": "Superseded request", "conclusion": "success"}]}]
        record = c.reconcile(self.api, record)
        self.assertEqual(record["state"], "superseded")
        self.assertEqual(c.disposition([record], self.api.request_data), "already-requested")
        request = a.new_request(REPOSITORY, 1791, "e" * 40, HEAD, SOURCE, "ordinary",
                                self.api.policy_digest, 101)
        self.assertEqual(c.disposition([record], request), "eligible")

    def test_cancelled_run_with_old_superseded_marker_still_blocks(self):
        self.api.runs[200].update(status="completed", conclusion="cancelled")
        self.api.outcomes = [{"name": c.OUTCOME_JOB, "conclusion": "success",
                              "steps": [{"name": "Superseded request", "conclusion": "success"}]}]
        self.assertEqual(c.writer_outcome(self.api, self.api.runs[200]), "blocked")

    def test_future_retry_attempt_does_not_reuse_request_authority(self):
        record = self.api.claim()
        self.api.writer_visible = True
        self.api.runs[200]["run_attempt"] = 2
        with self.assertRaises(a.AutomationError):
            c.reconcile(self.api, record)

    def test_external_draft_closed_and_wrong_base_are_ineligible(self):
        self.assertTrue(c.eligible_pr(self.api.pr, REPOSITORY))
        for mutate in (lambda pr: pr.update(draft=True), lambda pr: pr.update(state="closed"),
                       lambda pr: pr["head"].update(repo=None), lambda pr: pr["base"].update(ref="feature")):
            pr = copy.deepcopy(self.api.pr)
            mutate(pr)
            self.assertFalse(c.eligible_pr(pr, REPOSITORY))

    def candidate(self):
        return {"requires_writer": True, "already_current": False, "previously_published": False,
                "head": HEAD, "source_head": SOURCE, "classification": "ordinary",
                "classification_record": CLASSIFICATION}

    def plan(self, candidate=None):
        with mock.patch.object(i, "_commit", return_value=BASE), \
                mock.patch.object(c, "resolve_candidate", return_value=candidate or self.candidate()):
            return c.plan(self.api, ROOT, BASE, 100)

    def test_plan_claims_before_dispatch_and_repeated_event_deduplicates(self):
        self.assertEqual(self.plan()["status"], "planned")
        self.assertEqual(self.plan()["status"], "idle")
        self.assertEqual(len(self.api.comments), 1)
        self.assertEqual(len(self.api.posts), 1)

    def test_writer_activity_defers_new_claims(self):
        self.api.active = [self.api.runs[200]]
        self.assertEqual(self.plan()["status"], "writer-active")
        self.assertEqual(self.api.comments, [])

    def test_failed_ci_never_schedules_a_new_test_attempt(self):
        self.api.ci_success = False
        self.assertEqual(self.plan()["status"], "idle")
        self.assertEqual(self.api.comments, [])

    def test_performance_failure_is_not_retried_even_with_green_build_ci(self):
        self.api.extra_checks = [{"name": "Compiler throughput", "head_sha": HEAD,
                                  "app": {"id": 15368}, "status": "completed", "conclusion": "failure"}]
        self.assertEqual(self.plan()["status"], "idle")
        self.assertEqual(self.api.comments, [])

    def test_admission_consumers_do_not_create_circular_prerequisites(self):
        self.api.extra_checks = [{"name": name, "head_sha": HEAD, "app": {"id": 15368},
                                  "status": "completed", "conclusion": "failure"}
                                 for name in ("Native retirement merge admission", "Main integration admission")]
        self.assertEqual(self.plan()["status"], "planned")

    def test_already_published_head_does_not_loop(self):
        candidate = {**self.candidate(), "already_current": True, "previously_published": True}
        self.assertEqual(self.plan(candidate)["status"], "idle")
        self.assertEqual(self.api.comments, [])

    def test_idle_stale_pr_does_not_fan_out_after_each_main_push(self):
        self.api.pr["auto_merge"] = None
        self.assertEqual(self.plan({**self.candidate(), "previously_published": True})["status"], "idle")

    def test_disabled_policy_makes_no_claims(self):
        self.api.policy["enabled"] = False
        self.assertEqual(self.plan()["status"], "disabled")
        self.assertEqual(self.api.comments, [])

    def test_writer_refused_candidates_are_blocked_not_fatal(self):
        # #1933: the real resolve_candidate -> gate.source_candidate path raises
        # the merge gate's IntegrationError; plan() must record it per PR.
        import native_retirement_merge_gate as gate
        self.assertIs(gate.integration, i)
        split_head, generated_head = "e" * 40, "f" * 40
        heads = {1791: HEAD, 1796: split_head, 1696: generated_head}
        ordinary_pr = self.api.pr
        pulls = [ordinary_pr]
        for number in (1796, 1696):
            pr = copy.deepcopy(ordinary_pr)
            pr["number"], pr["head"]["sha"] = number, heads[number]
            pulls.append(pr)
        classifications = {
            HEAD: i.Classification("ordinary", ("src/buster/lib/value.c",), (), (), ()),
            split_head: i.Classification(
                "split-required",
                ("docs/native-retirement-support-v1.tsv", "tools/native_retirement_contract.py"),
                (), ("tools/native_retirement_contract.py",), ("docs/native-retirement-support-v1.tsv",)),
            generated_head: i.Classification(
                "ordinary", ("tools/native_retirement_dependency_binding.generated.h",),
                ("tools/native_retirement_dependency_binding.generated.h",), (), ()),
        }
        fixture_all = self.api.all

        def all_pages(path, **query):
            if path == "pulls":
                return copy.deepcopy(pulls)
            if path in ("issues/1796/comments", "issues/1696/comments"):
                return []
            return fixture_all(path, **query)

        def commit(repo, revision):
            prefix = "refs/remotes/origin/native-retirement-controller-"
            if revision.startswith(prefix):
                return heads[int(revision[len(prefix):])]
            return BASE if revision == "HEAD" else revision

        self.api.all = all_pages
        with mock.patch.object(i, "_git"), \
                mock.patch.object(i, "_commit", side_effect=commit), \
                mock.patch.object(i, "classify_candidate",
                                  side_effect=lambda repo, base, head: classifications[head]), \
                mock.patch.object(gate, "integration_record", return_value=((), {})), \
                mock.patch.object(gate, "clean_merge_tree", return_value="a" * 40), \
                mock.patch.object(gate, "bound_sources", return_value=frozenset({"src/buster/lib/value.c"})):
            result = c.plan(self.api, ROOT, BASE, 100)
        self.assertEqual(result["status"], "planned")
        self.assertEqual(result["request"]["number"], 1791)
        statuses = {entry["number"]: entry for entry in result["observations"]}
        self.assertEqual(statuses[1791]["status"], "eligible")
        self.assertEqual(statuses[1796]["status"], "blocked")
        self.assertIn("split it into a backwards-compatible bootstrap", statuses[1796]["detail"])
        self.assertEqual(statuses[1696]["status"], "blocked")
        self.assertIn("generated artifacts were edited manually", statuses[1696]["detail"])
        self.assertEqual(len(self.api.comments), 1)


class WorkflowTests(unittest.TestCase):
    def test_writer_keeps_one_privileged_publisher_and_no_dispatch_authority(self):
        text = (ROOT / a.WRITER_PATH).read_text()
        self.assertEqual(text.count("contents: write"), 1)
        self.assertNotIn("actions: write", text)
        self.assertIn("group: native-retirement-integration-writer", text)
        validate = text.split("\n  validate:", 1)[1].split("\n  publish:", 1)[0]
        self.assertNotIn("secrets.", validate)
        self.assertNotIn("contents: write", validate)
        self.assertIn("--automation-publication", text)
        self.assertIn("Automatic integration requires NATIVE_RETIREMENT_PUBLICATION_TOKEN", text)

    def test_controller_has_no_candidate_execution_or_publication_credentials(self):
        text = (ROOT / a.CONTROLLER_PATH).read_text()
        self.assertIn("ref: ${{ github.workflow_sha }}", text)
        self.assertNotIn("pull_request_target:", text)
        self.assertNotIn("contents: write", text)
        self.assertNotIn("secrets.", text)
        self.assertNotIn("self-hosted", text)
        self.assertLess(text.index("Seal the trusted request"), text.index("Dispatch the existing single writer once"))

    def test_checked_in_policy_has_explicit_valid_activation_state(self):
        policy = a.decode((ROOT / a.POLICY_PATH).read_bytes())
        a.validate_policy(policy, REPOSITORY)
        self.assertIs(type(policy["enabled"]), bool)

    def test_actual_outcome_script_never_labels_validation_failure_superseded(self):
        text = (ROOT / a.WRITER_PATH).read_text().split("  automation_outcome:", 1)[1]
        script = text.split("python3 - <<'PY'\n", 1)[1].split("          PY\n", 1)[0]
        script = "\n".join(line[10:] for line in script.splitlines()) + "\n"
        cases = [
            ({"prepare": {"result": "failure", "outputs": {"superseded": "true"}},
              "validate": {"result": "skipped"}, "publish": {"result": "skipped"}}, "superseded"),
            ({"prepare": {"result": "success"}, "validate": {"result": "failure"},
              "publish": {"result": "skipped", "outputs": {"superseded": "true"}}}, "blocked"),
            ({"prepare": {"result": "success"}, "validate": {"result": "success"},
              "publish": {"result": "failure", "outputs": {"superseded": "true"}}}, "superseded"),
            ({"prepare": {"result": "success"}, "validate": {"result": "success"},
              "publish": {"result": "success"}}, "published"),
            ({"prepare": {"result": "cancelled", "outputs": {"superseded": "true"}},
              "validate": {"result": "skipped"}, "publish": {"result": "skipped"}}, "blocked"),
        ]
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary) / "output"
            for needs, expected in cases:
                with self.subTest(expected=expected, needs=needs):
                    output.write_text("")
                    subprocess.run([os.sys.executable, "-c", script], check=True,
                                   env={**os.environ, "NEEDS_JSON": json.dumps(needs), "GITHUB_OUTPUT": str(output)})
                    self.assertEqual(output.read_text(), "state=" + expected + "\n")


if __name__ == "__main__":
    unittest.main()
