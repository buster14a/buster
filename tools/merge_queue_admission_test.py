#!/usr/bin/env python3
"""Deterministic queue-admission fixtures; no API writes or live queue claims."""

import io
import http.client
import json
from pathlib import Path
import subprocess
import tempfile
import unittest
import urllib.error
import urllib.request
from types import SimpleNamespace
from unittest.mock import patch

import merge_queue_admission as gate
import native_retirement_integration as integration

ROOT = Path(__file__).resolve().parents[1]


class APIReadTests(unittest.TestCase):
    def failure(self, status, headers=None):
        return urllib.error.HTTPError("https://example.invalid/secret", status,
                                      "fixture-secret", headers or {}, io.BytesIO())

    def response(self):
        return io.BytesIO(b'{"verified":true}')

    def test_transient_get_recovers(self):
        for error in (self.failure(502), self.failure(503), self.failure(504),
                      self.failure(500), ConnectionResetError("fixture-secret"),
                      urllib.error.URLError("fixture-secret"), TimeoutError("fixture-secret"),
                      http.client.IncompleteRead(b"partial")):
            with self.subTest(error=type(error).__name__), \
                    patch.object(gate.urllib.request, "urlopen", side_effect=[error, self.response()]) as read, \
                    patch.object(gate.time, "sleep") as sleep:
                self.assertEqual(gate.GitHub("a/b", "fixture-secret").get("actions/runs"), {"verified": True})
                self.assertEqual(read.call_count, 2)
                sleep.assert_called_once_with(1)

    def test_attempt_limit_and_sanitized_failure(self):
        def unavailable(*_a, **_k):
            raise self.failure(502)
        with patch.object(gate.urllib.request, "urlopen", side_effect=unavailable) as read, \
                patch.object(gate.time, "sleep") as sleep:
            with self.assertRaisesRegex(gate.APIReadError, "GET actions/runs: HTTP 502 after 4") as error:
                gate.GitHub("a/b", "fixture-secret").get("actions/runs")
            self.assertNotIn("fixture-secret", str(error.exception))
            self.assertNotIn("example.invalid", str(error.exception))
            self.assertEqual(read.call_count, 4)
            self.assertEqual([call.args[0] for call in sleep.call_args_list], [1, 2, 4])

    def test_ordinary_4xx_and_malformed_responses_do_not_retry(self):
        for status in (400, 401, 403, 404, 422):
            with self.subTest(status=status), \
                    patch.object(gate.urllib.request, "urlopen", side_effect=self.failure(status)) as read, \
                    patch.object(gate.time, "sleep") as sleep:
                with self.assertRaises(gate.APIReadError):
                    gate.GitHub("a/b", "fixture-secret").get("rulesets/1")
                self.assertEqual(read.call_count, 1)
                sleep.assert_not_called()
        with patch.object(gate.urllib.request, "urlopen", return_value=io.BytesIO(b"not-json")) as read:
            with self.assertRaises(ValueError):
                gate.GitHub("a/b", "fixture-secret").get("rulesets/1")
            self.assertEqual(read.call_count, 1)

    def test_server_retry_timing_is_honored_only_within_budget(self):
        for status, headers in ((429, {"Retry-After": "3"}),
                                (403, {"Retry-After": "3"}),
                                (403, {"X-RateLimit-Remaining": "0", "X-RateLimit-Reset": "1003"}),
                                (503, {"Retry-After": "Thu, 01 Jan 1970 00:16:43 GMT"})):
            with self.subTest(status=status, headers=headers), patch.object(gate.time, "time", return_value=1000), \
                    patch.object(gate.urllib.request, "urlopen", side_effect=[self.failure(status, headers), self.response()]), \
                    patch.object(gate.time, "sleep") as sleep:
                gate.GitHub("a/b", "fixture-secret").get("actions/runs")
                sleep.assert_called_once_with(3)
        for timing in ("60", "bad-date", "inf", "nan"):
            with self.subTest(timing=timing), \
                    patch.object(gate.urllib.request, "urlopen", side_effect=self.failure(429, {"Retry-After": timing})) as read, \
                    patch.object(gate.time, "sleep") as sleep:
                with self.assertRaises(gate.APIReadError):
                    gate.GitHub("a/b", "fixture-secret").get("actions/runs")
                self.assertEqual(read.call_count, 1)
                sleep.assert_not_called()

    def test_elapsed_budget_includes_request_time(self):
        with patch.object(gate.time, "monotonic", side_effect=[0, 0, 29.5]), \
                patch.object(gate.urllib.request, "urlopen", side_effect=self.failure(502)) as read, \
                patch.object(gate.time, "sleep") as sleep:
            with self.assertRaises(gate.APIReadError):
                gate.GitHub("a/b", "fixture-secret").get("actions/runs")
            self.assertEqual(read.call_args.kwargs["timeout"], 30)
            sleep.assert_not_called()

    def test_retry_helper_refuses_writes(self):
        for method in ("POST", "PATCH", "PUT", "DELETE"):
            request = urllib.request.Request("https://example.invalid", method=method)
            with self.subTest(method=method), patch.object(gate.urllib.request, "urlopen") as read:
                with self.assertRaises(integration.IntegrationError):
                    gate.github_read_json(request, "check-runs")
                read.assert_not_called()

    def test_missing_ref_requires_successful_exact_collection_confirmation(self):
        expected = candidate()
        for rows, retired in (([], True),
                              ([{"ref": expected["head_ref"] + "-other", "object": {"sha": expected["head"]}}], True),
                              ([{"ref": expected["head_ref"], "object": {"sha": "c" * 40}}], True),
                              ([{"ref": expected["head_ref"], "object": {"sha": expected["head"]}}], False)):
            api = gate.GitHub("a/b", "fixture-secret")
            responses = [io.BytesIO(json.dumps({"object": {"sha": expected["base"]}}).encode())]
            responses += [self.failure(404) for _ in range(4)]
            responses += [io.BytesIO(json.dumps(rows).encode())]
            with self.subTest(rows=rows), patch.object(gate.urllib.request, "urlopen", side_effect=responses), \
                    patch.object(gate.time, "sleep"):
                with self.assertRaises(gate.GroupRetired if retired else gate.APIReadError):
                    gate.live_identity(api, expected)

    def test_failed_or_malformed_ref_confirmation_cannot_retire(self):
        for confirmation in (gate.APIReadError("git/matching-refs/heads/queue", 403, 1), {},
                             [None], [{"ref": candidate()["head_ref"], "object": {"sha": "bad"}}]):
            with self.subTest(confirmation=confirmation), patch.object(gate, "GitHub") as api:
                api.return_value.get.side_effect = [{"object": {"sha": "a" * 40}},
                                                   gate.APIReadError("git/ref/heads/queue", 404, 4), confirmation]
                with self.assertRaises((gate.APIReadError, gate.AdmissionError)):
                    gate.live_identity(api.return_value, candidate())


def candidate():
    return {"repository": "buster14a/buster", "base": "a" * 40, "head": "b" * 40,
            "head_ref": "refs/heads/gh-readonly-queue/main/pr-1-example",
            "policy_sha": "a" * 40, "base_first_parents": ["a" * 40]}


def live_rules():
    data = json.loads((ROOT / ".github/main-merge-queue.ruleset.json").read_text())
    data.update(id=gate.RULESET_ID, source_type="Repository", source="buster14a/buster")
    # The actual read-only Actions response does not reveal bypass actors.
    del data["bypass_actors"]
    return data


def results(checks=gate.CHECKS):
    expected = candidate()
    rows, jobs = [], {}
    for index, (filename, context) in enumerate(checks.items(), 1):
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
    def test_live_policy_selects_self_host_without_substituting_success(self):
        rows, jobs = results(gate.LEGACY_CHECKS)
        legacy = gate.required_checks(None, {"self_host_admission": True})
        main_only = gate.required_checks(None, {"self_host_admission": False})
        self.assertEqual(set(legacy) - set(main_only), {"self-host-audit.yml"})
        rows = [row for row in rows if row["path"] != ".github/workflows/self-host-audit.yml"]
        evidence, pending = gate.check_results(gate.latest_runs(rows, candidate(), main_only),
                                              jobs, candidate(), main_only)
        self.assertEqual(len(evidence), 5)
        self.assertEqual(pending, [])
        self.assertEqual(gate.check_results(gate.latest_runs(rows, candidate(), legacy),
                                           jobs, candidate(), legacy)[1],
                         ["Linux x86-64 bootstrap evidence: missing workflow"])

    def evaluate(self, rows, jobs):
        return gate.check_results(gate.latest_runs(rows, candidate()), jobs, candidate())

    def test_all_five_exact_results(self):
        evidence, pending = self.evaluate(*results())
        self.assertEqual(len(evidence), len(gate.CHECKS))
        self.assertEqual(pending, [])

    def test_every_required_job_failure_skip_cancel_neutral_and_missing(self):
        for index in range(1, len(gate.CHECKS) + 1):
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
        self.assertEqual(len(evidence), len(gate.CHECKS) - 1)
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
    def test_main_only_policy_removes_exactly_one_requirement(self):
        data = self.ruleset()
        rows = data["rules"][3]["parameters"]["required_status_checks"]
        rows[:] = [row for row in rows if row["context"] != "Linux x86-64 bootstrap evidence"]
        gate.validate_ruleset(data)
        data.update(id=gate.RULESET_ID, source_type="Repository", source="buster14a/buster")
        api = gate.GitHub("buster14a/buster", "fixture-token")
        with patch.object(api, "get", return_value=data):
            self.assertIs(gate.live_ruleset(api, "buster14a/buster")["self_host_admission"], False)
        rows.append({"context": "unreviewed", "integration_id": gate.GITHUB_ACTIONS_APP_ID})
        with self.assertRaises(gate.AdmissionError):
            gate.validate_ruleset(data)

    def ruleset(self):
        return json.loads((ROOT / ".github/main-merge-queue.ruleset.json").read_text())

    def test_desired_ruleset(self):
        gate.validate_ruleset(self.ruleset())
        self.assertEqual(self.ruleset()["bypass_actors"], gate.BYPASS_ACTORS)
        self.assertEqual(gate.QUEUE["max_entries_to_build"], 6)
        self.assertEqual(self.ruleset()["rules"][-1]["parameters"], gate.QUEUE)

    def test_readonly_omission_is_not_an_administrator_audit(self):
        data = live_rules()
        gate.validate_ruleset(data, read_only_response=True)
        with self.assertRaisesRegex(gate.AdmissionError, "inventory is hidden"):
            gate.validate_ruleset(data)
        self.assertNotIn("bypass_actors", data)

    def test_unreviewed_bypass_and_malformed_inventory_fail_in_both_modes(self):
        for inventory in (None, {}, "", False, [],
                          gate.BYPASS_ACTORS[:1], gate.BYPASS_ACTORS * 2,
                          [{"actor_type": "OrganizationAdmin"}],
                          [gate.BYPASS_ACTORS[1], gate.BYPASS_ACTORS[0]],
                          [{**gate.BYPASS_ACTORS[0], "bypass_mode": "pull_requests_only"},
                           gate.BYPASS_ACTORS[1]]):
            for read_only in (False, True):
                with self.subTest(inventory=inventory, read_only=read_only):
                    data = self.ruleset()
                    data["bypass_actors"] = inventory
                    with self.assertRaisesRegex(gate.AdmissionError, "bypass actors differ"):
                        gate.validate_ruleset(data, read_only_response=read_only)

    def test_administrator_response_accepts_known_reader_capabilities(self):
        for authority in ("never", "always", "pull_requests_only"):
            with self.subTest(authority=authority):
                data = self.ruleset()
                data["current_user_can_bypass"] = authority
                gate.validate_ruleset(data)

    def test_administrator_reader_capability_requires_reviewed_inventory(self):
        for authority in ("always", "pull_requests_only"):
            for inventory in (None, [], [{"actor_type": "OrganizationAdmin"}]):
                with self.subTest(authority=authority, inventory=inventory):
                    data = self.ruleset()
                    data["current_user_can_bypass"] = authority
                    if inventory is None:
                        del data["bypass_actors"]
                    else:
                        data["bypass_actors"] = inventory
                    with self.assertRaisesRegex(gate.AdmissionError, "bypass (inventory|actors)"):
                        gate.validate_ruleset(data)

    def test_readonly_reader_bypass_authority_is_rejected(self):
        for authority in ("always", "pull_requests_only", None, ""):
            for visible in (False, True):
                with self.subTest(authority=authority, visible=visible):
                    data = self.ruleset() if visible else live_rules()
                    data["current_user_can_bypass"] = authority
                    with self.assertRaisesRegex(gate.AdmissionError, "bypass authority"):
                        gate.validate_ruleset(data, read_only_response=True)

    def test_readonly_reader_without_bypass_is_accepted(self):
        for visible in (False, True):
            with self.subTest(visible=visible):
                data = self.ruleset() if visible else live_rules()
                data["current_user_can_bypass"] = "never"
                gate.validate_ruleset(data, read_only_response=True)

    def test_unknown_reader_capability_is_rejected_in_both_modes(self):
        for authority in (None, "", "ALWAYS", "unknown", True, 1, [], {}):
            for read_only in (False, True):
                with self.subTest(authority=authority, read_only=read_only):
                    data = self.ruleset()
                    data["current_user_can_bypass"] = authority
                    with self.assertRaises(gate.AdmissionError):
                        gate.validate_ruleset(data, read_only_response=read_only)

    def test_live_ruleset_identity_and_visibility(self):
        api = gate.GitHub("buster14a/buster", "fixture-token")
        for hidden in (False, True):
            data = live_rules()
            if not hidden:
                data["bypass_actors"] = gate.BYPASS_ACTORS
            with patch.object(api, "get", return_value=data):
                report = gate.live_ruleset(api, "buster14a/buster")
                self.assertEqual(report["bypass_inventory"],
                                 "hidden" if hidden else "verified-expected")
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
                data["rules"][-1]["parameters"]["max_entries_to_build"] = 20
            with self.subTest(change=change), self.assertRaises(gate.AdmissionError):
                gate.validate_ruleset(data, read_only_response=True)

    def test_each_required_check_remains_independently_required(self):
        for context in (*gate.POST_MERGE_CHECKS.values(), gate.RETIREMENT_CONTEXT, gate.CONTEXT):
            with self.subTest(context=context):
                data = self.ruleset()
                parameters = data["rules"][3]["parameters"]
                parameters["required_status_checks"] = [
                    row for row in parameters["required_status_checks"]
                    if row["context"] != context
                ]
                with self.assertRaises(gate.AdmissionError):
                    gate.validate_ruleset(data)

    def test_build_concurrency_is_exactly_six(self):
        # 4 and 20 are former live limits (#1805); they must fail closed, not linger as an accepted range.
        for value in (1, 2, 3, 4, 5, 7, 8, 10, 19, 20, 21):
            data = self.ruleset()
            data["rules"][-1]["parameters"]["max_entries_to_build"] = value
            with self.subTest(value=value), self.assertRaisesRegex(
                    gate.AdmissionError, f"max_entries_to_build: expected 6, got {value}"):
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

    def test_workflow_is_read_only_and_uses_independent_main(self):
        text = (ROOT / ".github/workflows/merge-queue-admission.yml").read_text()
        self.assertNotIn(": write", text)
        self.assertNotIn("pull_request_target:", text)
        self.assertNotIn("secrets.", text)
        self.assertNotIn("github.event.merge_group.base_sha || github.sha", text)
        self.assertIn("persist-credentials: false", text)
        self.assertIn("github.event_name == 'push' && github.run_id", text)
        # The reconciler is the only merge-group producer of CONTEXT (#1807):
        # no merge_group trigger, no runner-held check-group wait.
        # group_owner keys on exactly this marker in the group head's file.
        self.assertNotIn("\n  merge_group:\n", text)
        self.assertNotIn("merge_group", text.split("\non:\n", 1)[1].split("\npermissions:", 1)[0])
        self.assertNotIn("check-group", text)
        self.assertNotIn("timeout-minutes: 310", text)


class CombinedTreeTests(unittest.TestCase):
    def test_two_queued_siblings_wait_for_predecessor_and_keep_exact_tree(self):
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
            first_tree = gate.git(repo, "merge-tree", "--write-tree", base, heads[0])
            first = gate.git(repo, "commit-tree", first_tree, "-p", base,
                             "-p", heads[0], "-m", "queued first")
            old_tree = gate.git(repo, "merge-tree", "--write-tree", base, heads[1])
            new_tree = gate.git(repo, "merge-tree", "--write-tree", first, heads[1])
            self.assertNotEqual(old_tree, new_tree)
            combined = gate.git(repo, "commit-tree", new_tree, "-p", first,
                                "-p", heads[1], "-m", "queued second")
            gate.git(repo, "checkout", "--detach", base)
            event = {"action": "checks_requested", "repository": {"full_name": "buster14a/buster"},
                     "merge_group": {"base_ref": "refs/heads/main", "base_sha": first,
                                     "head_sha": combined, "head_ref": candidate()["head_ref"]}}
            report = gate.identity(event, "buster14a/buster", combined, repo)
            self.assertEqual(report["final_tree"], new_tree)
            self.assertEqual(report["policy_sha"], base)
            self.assertFalse(gate.check_current(report, base, combined))
            gate.verify_trusted_policy(report, repo)
            self.assertTrue(gate.check_current(report, first, combined))
            event_path = repo / "queue-event.json"
            event_path.write_text(json.dumps(event))
            arguments = SimpleNamespace(event=event_path, repository="buster14a/buster",
                                        sha=combined, repo_root=repo, trusted_root=repo,
                                        wait_seconds=60)
            observed_mains = iter((base, first, first))
            def read_ref(path):
                sha = next(observed_mains) if path == "git/ref/heads/main" else combined
                return {"object": {"sha": sha}}
            with patch.object(gate, "GitHub") as api, patch.object(gate.time, "sleep") as sleep:
                api.return_value.get.side_effect = read_ref
                landed = gate.wait_base(arguments)
                self.assertEqual(landed["status"], "base-landed")
                self.assertEqual(landed["base"], first)
                sleep.assert_called_once()
            observed_mains = iter((base, first))
            queue_heads = iter((combined, "f" * 40))
            def replaced_ref(path):
                sha = next(observed_mains) if path == "git/ref/heads/main" else next(queue_heads)
                return {"object": {"sha": sha}}
            with patch.object(gate, "GitHub") as api, patch.object(gate.time, "sleep"):
                api.return_value.get.side_effect = replaced_ref
                with self.assertRaisesRegex(gate.GroupRetired, "group replaced"):
                    gate.wait_base(arguments)
            with patch.object(gate, "GitHub") as api, patch.object(gate.time, "sleep") as sleep:
                api.return_value.get.side_effect = [
                    {"object": {"sha": base}}, {"object": {"sha": combined}},
                    {"object": {"sha": base}}, gate.APIReadError("git/ref/heads/queue", 404, 4), []]
                with self.assertRaisesRegex(gate.GroupRetired, "group retired"):
                    gate.wait_base(arguments)
                sleep.assert_called_once()
            for main, head in ((heads[1], combined), (base, heads[1])):
                with self.assertRaises(gate.AdmissionError):
                    gate.check_current(report, main, head)
            with self.assertRaisesRegex(gate.AdmissionError, "group head must have"):
                gate.identity(dict(event, merge_group=dict(event["merge_group"],
                                  head_sha=first)), "buster14a/buster", first, repo)
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

    def test_predecessor_policy_change_cannot_use_old_authority(self):
        with tempfile.TemporaryDirectory() as temporary:
            repo = Path(temporary)
            gate.git(repo, "init", "-b", "main")
            gate.git(repo, "config", "user.name", "Queue fixture")
            gate.git(repo, "config", "user.email", "queue@example.invalid")
            (repo / "README.md").write_text("base\n")
            gate.git(repo, "add", ".")
            gate.git(repo, "commit", "-m", "base")
            main = gate.git(repo, "rev-parse", "HEAD")
            (repo / "tools").mkdir()
            (repo / "tools/native_retirement_rebind.py").write_text("new rebinding policy\n")
            gate.git(repo, "add", ".")
            gate.git(repo, "commit", "-m", "predecessor changes policy")
            predecessor = gate.git(repo, "rev-parse", "HEAD")
            report = dict(candidate(), policy_sha=main, base=predecessor,
                          base_first_parents=[predecessor, main])
            self.assertFalse(gate.check_current(report, main, report["head"]))
            self.assertTrue(gate.check_current(report, predecessor, report["head"]))
            with self.assertRaisesRegex(gate.AdmissionError, "changed admission policy"):
                gate.verify_trusted_policy(report, repo)

    def test_current_native_declaration_predecessor_requires_new_authority(self):
        with tempfile.TemporaryDirectory() as temporary:
            repo = Path(temporary)
            gate.git(repo, "init", "-b", "main")
            gate.git(repo, "config", "user.name", "Queue fixture")
            gate.git(repo, "config", "user.email", "queue@example.invalid")
            (repo / "README.md").write_text("base\n")
            gate.git(repo, "add", ".")
            gate.git(repo, "commit", "-m", "base")
            main = gate.git(repo, "rev-parse", "HEAD")
            (repo / "docs").mkdir()
            (repo / "docs/current-native-object-census-v1.json").write_text("new rebinding policy\n")
            gate.git(repo, "add", ".")
            gate.git(repo, "commit", "-m", "predecessor changes policy")
            predecessor = gate.git(repo, "rev-parse", "HEAD")
            report = dict(candidate(), policy_sha=main, base=predecessor,
                          base_first_parents=[predecessor, main])
            self.assertFalse(gate.check_current(report, main, report["head"]))
            self.assertTrue(gate.check_current(report, predecessor, report["head"]))
            with self.assertRaisesRegex(gate.AdmissionError, "changed admission policy"):
                gate.verify_trusted_policy(report, repo)


class OrchestrationTests(unittest.TestCase):
    def test_speculative_group_does_not_run_retirement_gate_or_collect_ci_early(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            event = root / "event.json"
            event.write_text("{}")
            arguments = SimpleNamespace(event=event, repository="buster14a/buster", sha="b" * 40,
                                        repo_root=root, wait_seconds=60)
            evidence = [{"run_id": 1, "run_attempt": 1}]
            with patch.object(gate, "identity", return_value=candidate()), \
                    patch.object(gate, "live_identity", side_effect=[False, False, True, True]) as live, \
                    patch.object(gate, "verify_trusted_policy"), \
                    patch.object(gate, "GitHub") as api, \
                    patch.object(gate, "retirement_admission", return_value={"status": "admitted"}) as native, \
                    patch.object(gate, "collect", return_value=(evidence, [])) as collect, \
                    patch.object(gate.time, "sleep") as sleep:
                api.return_value.get.return_value = live_rules()
                report = gate.run_gate(arguments)
                self.assertEqual(report["status"], "admitted")
                self.assertEqual(sleep.call_count, 1)
                self.assertEqual(native.call_count, 2)
                self.assertEqual(collect.call_count, 2)
                self.assertEqual(live.call_count, 4)

    def test_removed_predecessor_times_out_without_retirement_or_ci(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            event = root / "event.json"
            event.write_text("{}")
            arguments = SimpleNamespace(event=event, repository="buster14a/buster", sha="b" * 40,
                                        repo_root=root, wait_seconds=0)
            with patch.object(gate, "identity", return_value=candidate()), \
                    patch.object(gate, "live_identity", return_value=False), \
                    patch.object(gate, "GitHub") as api, \
                    patch.object(gate, "retirement_admission") as native, \
                    patch.object(gate, "collect") as collect:
                api.return_value.get.return_value = live_rules()
                with self.assertRaisesRegex(gate.AdmissionError, "timed out"):
                    gate.run_gate(arguments)
                native.assert_not_called()
                collect.assert_not_called()

    def test_missing_retirement_gate_cannot_be_skipped(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            event = root / "event.json"
            event.write_text("{}")
            arguments = SimpleNamespace(event=event, repository="buster14a/buster", sha="b" * 40,
                                        repo_root=root, wait_seconds=0)
            rules = live_rules()
            with patch.object(gate, "identity", return_value=candidate()), \
                    patch.object(gate, "live_identity"), \
                    patch.object(gate, "verify_trusted_policy"), patch.object(gate, "GitHub") as api:
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
                        patch.object(gate, "live_identity"), \
                        patch.object(gate, "verify_trusted_policy"), patch.object(gate, "GitHub") as api, \
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
                    patch.object(gate, "live_identity") as live, \
                    patch.object(gate, "verify_trusted_policy"), patch.object(gate, "GitHub") as api, \
                    patch.object(gate.subprocess, "run", return_value=result) as native_run, \
                    patch.object(gate, "collect", return_value=(evidence, [])) as collect:
                api.return_value.get.return_value = rules
                report = gate.run_gate(arguments)
                self.assertEqual(report["status"], "admitted")
                self.assertEqual(report["ruleset_reads"], [
                    {"id": gate.RULESET_ID, "bypass_inventory": "hidden", "self_host_admission": False}] * 2)
                self.assertEqual(report["head"], "b" * 40)
                self.assertEqual(collect.call_count, 2)
                self.assertEqual(live.call_count, 3)
                self.assertEqual(api.return_value.get.call_count, 2)
                self.assertEqual(native_run.call_count, 2)
                self.assertNotIn("--allow-pending", native_run.call_args.args[0])
                self.assertIn("--repository", native_run.call_args.args[0])

    def test_unattested_retirement_groups_also_require_ephemeral_reconstruction(self):
        for mode, required in (("ordinary-merge-group", False),
                               ("ordinary-bound-merge-group", True),
                               ("trusted-integration-merge-group", True)):
            with self.subTest(mode=mode):
                checks = gate.required_checks({"status": "admitted", "mode": mode})
                self.assertEqual(set(gate.CHECKS.items()) <= set(checks.items()), True)
                self.assertEqual("native-retirement-rebind.yml" in checks, required)
        rows, jobs = results()
        expected = candidate()
        checks = gate.required_checks({"mode": "ordinary-bound-merge-group"})
        _, pending = gate.check_results(gate.latest_runs(rows, expected, checks), jobs, expected, checks)
        self.assertEqual(pending, ["Reconstruct candidate closure ephemerally: missing workflow"])
        index = len(rows) + 1
        rows.append(dict(rows[0], id=index, path=".github/workflows/native-retirement-rebind.yml"))
        for conclusion in ("failure", "cancelled", "skipped"):
            with self.subTest(conclusion=conclusion):
                jobs[(index, 1)] = [{"id": 900, "run_id": index, "run_attempt": 1,
                                     "name": "Reconstruct candidate closure ephemerally",
                                     "head_sha": expected["head"], "status": "completed",
                                     "conclusion": conclusion}]
                with self.assertRaisesRegex(gate.AdmissionError, "did not succeed"):
                    gate.check_results(gate.latest_runs(rows, expected, checks), jobs, expected, checks)
        jobs[(index, 1)][0]["conclusion"] = "success"
        evidence, pending = gate.check_results(
            gate.latest_runs(rows, expected, checks), jobs, expected, checks)
        self.assertEqual(pending, [])
        self.assertEqual(len(evidence), len(gate.CHECKS) + 1)

    def test_bound_group_collects_reconstruction_on_both_passes(self):
        with tempfile.TemporaryDirectory() as temporary:
            event = Path(temporary) / "event.json"
            event.write_text("{}")
            arguments = SimpleNamespace(event=event, repository="buster14a/buster", sha="b" * 40,
                                        repo_root=Path(temporary), wait_seconds=0)
            retirement = {"status": "admitted", "mode": "ordinary-bound-merge-group"}
            with patch.object(gate, "identity", return_value=candidate()), \
                    patch.object(gate, "live_identity", return_value=True), \
                    patch.object(gate, "verify_trusted_policy"), patch.object(gate, "GitHub") as api, \
                    patch.object(gate, "retirement_admission", return_value=retirement), \
                    patch.object(gate, "collect", return_value=([{"run_id": 1}], [])) as collect:
                api.return_value.get.return_value = live_rules()
                gate.run_gate(arguments)
                self.assertEqual(collect.call_count, 2)
                for call in collect.call_args_list:
                    self.assertIn("native-retirement-rebind.yml", call.args[2])

    def test_publication_change_during_ci_rejects_admission(self):
        with tempfile.TemporaryDirectory() as temporary:
            event = Path(temporary) / "event.json"
            event.write_text("{}")
            arguments = SimpleNamespace(event=event, repository="buster14a/buster", sha="b" * 40,
                                        repo_root=Path(temporary), wait_seconds=0)
            rules = live_rules()
            with patch.object(gate, "identity", return_value=candidate()), \
                    patch.object(gate, "live_identity"), \
                    patch.object(gate, "verify_trusted_policy"), patch.object(gate, "GitHub") as api, \
                    patch.object(gate, "collect", return_value=([{"run_id": 1}], [])), \
                    patch.object(gate, "retirement_admission", side_effect=[
                        {"attestation_id": 1}, {"attestation_id": 2}]):
                api.return_value.get.return_value = rules
                with self.assertRaisesRegex(gate.AdmissionError, "publication changed"):
                    gate.run_gate(arguments)

    def test_ruleset_change_during_ci_rejects_admission(self):
        for change, reason in (("enforcement", "must be active"),
                               ("self-host", "policy changed during collection"),
                               ("build concurrency", "max_entries_to_build: expected 6, got 1")):
            with self.subTest(change=change), tempfile.TemporaryDirectory() as temporary:
                event = Path(temporary) / "event.json"
                event.write_text("{}")
                arguments = SimpleNamespace(event=event, repository="buster14a/buster", sha="b" * 40,
                                            repo_root=Path(temporary), wait_seconds=0)
                original = live_rules()
                if change == "self-host":
                    original["rules"][3]["parameters"]["required_status_checks"].append(
                        {"context": "Linux x86-64 bootstrap evidence", "integration_id": gate.GITHUB_ACTIONS_APP_ID})
                changed = json.loads(json.dumps(original))
                if change == "enforcement":
                    changed["enforcement"] = "disabled"
                elif change == "self-host":
                    rows = changed["rules"][3]["parameters"]["required_status_checks"]
                    rows[:] = [row for row in rows if row["context"] != "Linux x86-64 bootstrap evidence"]
                else:
                    changed["rules"][-1]["parameters"]["max_entries_to_build"] = 1
                with patch.object(gate, "identity", return_value=candidate()), \
                        patch.object(gate, "live_identity"), \
                        patch.object(gate, "verify_trusted_policy"), patch.object(gate, "GitHub") as api, \
                        patch.object(gate, "collect", return_value=([{"run_id": 1}], [])), \
                        patch.object(gate, "retirement_admission", return_value={"attestation_id": 1}):
                    api.return_value.get.side_effect = [original, changed]
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


LEGACY_YAML = "name: legacy\non:\n  pull_request:\n  merge_group:\n    types: [checks_requested]\n"
EVENT_YAML = "name: readiness\non:\n  pull_request:\n  push:\n    branches: [main]\n"


class FakeReader:
    """Live refs and check runs; every read is recorded for API-cost assertions."""

    def __init__(self, main, heads):
        self.main, self.heads, self.checks, self.reads = main, dict(heads), {}, []

    def get(self, path, **query):
        self.reads.append(path)
        if path == "git/ref/heads/main":
            sha = self.main
        else:
            sha = self.heads[path.removeprefix("git/ref/")]
        return {"object": {"sha": sha}}

    def pages(self, path, field, **query):
        self.reads.append(path)
        assert field == "check_runs" and query.get("check_name") in (
            gate.CONTEXT, gate.RETIREMENT_CONTEXT)
        assert query["filter"] == "all" and query["app_id"] == gate.GITHUB_ACTIONS_APP_ID
        return [dict(row) for row in self.checks.get(path.split("/")[1], [])
                if row["name"] == query["check_name"]]


class FakeWriter:
    def __init__(self, reader):
        self.reader, self.sent = reader, []

    def send(self, existing, body):
        self.sent.append((existing and existing["id"], body))
        head = body.get("head_sha") or existing["head_sha"]
        row = dict(existing or {})
        row.update(body)
        row.update(id=existing["id"] if existing else 900 + len(self.sent), head_sha=head,
                   app={"id": gate.GITHUB_ACTIONS_APP_ID})
        self.reader.checks[head] = [item for item in self.reader.checks.get(head, [])
                                    if item["name"] != row["name"]] + [row]
        return row


class ReconcileTests(unittest.TestCase):
    """M -> G1 -> G2 against a real local origin; only the API is simulated."""

    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        root = Path(self.directory.name)
        self.origin, work = root / "origin.git", root / "work"
        gate.git(root, "init", "--bare", "-b", "main", str(self.origin))
        gate.git(root, "init", "-b", "main", str(work))
        gate.git(work, "config", "user.name", "Queue fixture")
        gate.git(work, "config", "user.email", "queue@example.invalid")
        workflow = work / gate.ADMISSION_WORKFLOW
        workflow.parent.mkdir(parents=True)
        workflow.write_text(EVENT_YAML)
        for name in ("a.c", "b.c"):
            (work / name).write_text("original\n")
        gate.git(work, "add", ".")
        gate.git(work, "commit", "-m", "M")
        self.main = gate.git(work, "rev-parse", "HEAD")
        prs = []
        for name in ("a.c", "b.c"):
            gate.git(work, "checkout", "-q", "-b", name, self.main)
            (work / name).write_text("changed\n")
            gate.git(work, "commit", "-am", name)
            prs.append(gate.git(work, "rev-parse", "HEAD"))
        tree = gate.git(work, "merge-tree", "--write-tree", self.main, prs[0])
        self.g1 = gate.git(work, "commit-tree", tree, "-p", self.main, "-p", prs[0], "-m", "G1")
        tree = gate.git(work, "merge-tree", "--write-tree", self.g1, prs[1])
        self.g2 = gate.git(work, "commit-tree", tree, "-p", self.g1, "-p", prs[1], "-m", "G2")
        self.work, self.prs = work, prs
        self.ref1 = gate.QUEUE_REF_PREFIX + "pr-1-first"
        self.ref2 = gate.QUEUE_REF_PREFIX + "pr-2-second"
        gate.git(work, "push", "-q", str(self.origin), f"{self.main}:refs/heads/main",
                 f"{self.g1}:{self.ref1}", f"{self.g2}:{self.ref2}")
        self.trusted = root / "trusted"
        gate.git(root, "clone", "-q", str(self.origin), str(self.trusted))
        self.reader = FakeReader(self.main, {self.ref1.removeprefix("refs/"): self.g1,
                                             self.ref2.removeprefix("refs/"): self.g2})
        self.writer = FakeWriter(self.reader)
        self.native = {"status": "admitted", "attestation_id": 1}
        self.evidence = [{"context": "CI complete", "run_id": 1, "run_attempt": 1, "job_id": 2}]
        self.collected = [(self.evidence, [])]

    def tearDown(self):
        self.directory.cleanup()

    def move_main(self, sha):
        gate.git(self.work, "push", "-q", "-f", str(self.origin), f"{sha}:refs/heads/main")
        self.reader.main = sha
        gate.git(self.trusted, "fetch", "-q", "origin", "main")
        gate.git(self.trusted, "checkout", "-q", "--detach", sha)

    def reconcile(self):
        arguments = SimpleNamespace(repo_root=self.trusted, repository="buster14a/buster",
                                    details_url="https://example.invalid/run")
        collected = iter(self.collected)
        with patch.object(gate, "GitHub", return_value=self.reader), \
                patch.object(gate, "CheckWriter", return_value=self.writer), \
                patch.object(gate, "live_ruleset", return_value={"id": gate.RULESET_ID}), \
                patch.object(gate, "retirement_admission",
                             side_effect=lambda *_: dict(self.native)) as native, \
                patch.object(gate, "collect", side_effect=lambda *_: next(collected)):
            report = gate.reconcile(arguments)
        self.native_calls = native.call_count
        return {group["head"]: group for group in report["groups"]}, report

    def test_front_group_pending_then_admitted_without_sleeping(self):
        self.collected = [([], ["CI complete: workflow still running"])]
        with patch.object(gate.time, "sleep", side_effect=AssertionError("reconcile never sleeps")):
            groups, report = self.reconcile()
        self.assertEqual(report["policy_sha"], self.main)
        self.assertEqual(groups[self.g1]["state"], "pending")
        self.assertEqual(groups[self.g2]["detail"], ["queued predecessor has not landed"])
        self.assertFalse(groups[self.g2]["published"])
        self.assertNotIn("commits/" + self.g2 + "/check-runs", self.reader.reads)
        (existing, body), = self.writer.sent
        self.assertIsNone(existing)
        self.assertEqual((body["status"], body["head_sha"], body["external_id"]),
                         ("in_progress", self.g1, gate.check_marker(self.g1)))
        self.assertNotIn("conclusion", body)
        # A duplicate delivery with the same pending state writes nothing.
        self.collected = [([], ["CI complete: workflow still running"])]
        self.reconcile()
        self.assertEqual(len(self.writer.sent), 1)
        # Exact prerequisites complete: double collection, then one PATCH to success.
        self.collected = [(self.evidence, []), (self.evidence, [])]
        groups, _ = self.reconcile()
        self.assertEqual(groups[self.g1]["state"], "admitted")
        existing, body = self.writer.sent[-1]
        self.assertEqual((existing, body["status"], body["conclusion"]), (901, "completed", "success"))
        self.assertNotIn("head_sha", body)
        self.assertIn(self.g1, body["output"]["text"])
        self.assertEqual(self.native_calls, 2)
        # Completed publications are terminal and never re-evaluated.
        groups, _ = self.reconcile()
        self.assertEqual(groups[self.g1]["state"], "published")
        self.assertEqual(len(self.writer.sent), 2)

    def native_group(self, native_job):
        gate.git(self.work, "checkout", "-q", "-b", "native-policy", self.main)
        path = self.work / gate.RETIREMENT_WORKFLOW
        path.write_text("name: API migration policy\non:\n  merge_group:\n    types: [checks_requested]\n"
                        "jobs:\n  policy:\n    name: API migration policy\n" +
                        ("  native-retirement-admission:\n    name: Native retirement merge admission\n"
                         if native_job else ""))
        gate.git(self.work, "add", ".")
        gate.git(self.work, "commit", "-qm", "native admission producer")
        pr = gate.git(self.work, "rev-parse", "HEAD")
        tree = gate.git(self.work, "merge-tree", "--write-tree", self.main, pr)
        head = gate.git(self.work, "commit-tree", tree, "-p", self.main, "-p", pr,
                        "-m", "native group")
        gate.git(self.work, "push", "-q", "-f", str(self.origin), f"{head}:{self.ref1}")
        self.reader.heads[self.ref1.removeprefix("refs/")] = head
        return head

    def test_native_check_publishes_only_without_legacy_job(self):
        head = self.native_group(native_job=False)
        self.collected = [(self.evidence, []), (self.evidence, [])]
        with patch.object(gate.time, "sleep", side_effect=AssertionError("no wait")):
            groups, _ = self.reconcile()
        native = groups[head]["native"]
        self.assertEqual((native["owner"], native["state"], native["published"]),
                         ("reconciler", "admitted", True))
        self.assertEqual(self.native_calls, 4)
        sent = [body for _, body in self.writer.sent if body["name"] == gate.RETIREMENT_CONTEXT]
        self.assertEqual(len(sent), 1)
        self.assertEqual(sent[0]["external_id"], gate.native_marker(head))
        self.assertEqual(sent[0]["conclusion"], "success")
        # Duplicate completion events never reissue a terminal check.
        self.collected = []
        groups, _ = self.reconcile()
        self.assertEqual(groups[head]["native"]["state"], "published")
        self.assertEqual(len([body for _, body in self.writer.sent
                              if body["name"] == gate.RETIREMENT_CONTEXT]), 1)

    def test_legacy_native_job_is_shadowed(self):
        head = self.native_group(native_job=True)
        self.collected = [(self.evidence, []), (self.evidence, [])]
        groups, _ = self.reconcile()
        self.assertEqual(groups[head]["native"]["owner"], "legacy")
        self.assertFalse(groups[head]["native"]["published"])
        self.assertTrue(all(body["name"] != gate.RETIREMENT_CONTEXT
                            for _, body in self.writer.sent))

    def test_native_denial_is_terminal_and_does_not_issue_success(self):
        head = self.native_group(native_job=False)
        self.native = {"status": "pending"}
        self.collected = [(self.evidence, []), (self.evidence, [])]
        arguments = SimpleNamespace(repo_root=self.trusted, repository="buster14a/buster",
                                    details_url="https://example.invalid/run")
        with patch.object(gate, "GitHub", return_value=self.reader), \
                patch.object(gate, "CheckWriter", return_value=self.writer), \
                patch.object(gate, "live_ruleset", return_value={"id": gate.RULESET_ID}), \
                patch.object(gate, "retirement_admission",
                             side_effect=gate.AdmissionError("publication pending")), \
                patch.object(gate, "collect", side_effect=iter(self.collected)):
            report = gate.reconcile(arguments)
            again = gate.reconcile(arguments)
        native = next(row for row in report["groups"] if row["head"] == head)["native"]
        self.assertEqual(native["state"], "rejected")
        self.assertEqual(next(row for row in again["groups"]
                              if row["head"] == head)["native"]["state"], "published")
        native_checks = [body for _, body in self.writer.sent
                         if body["name"] == gate.RETIREMENT_CONTEXT]
        self.assertEqual(len(native_checks), 1)
        self.assertEqual(native_checks[0]["conclusion"], "failure")

    def test_predecessor_landing_advances_g2_under_landed_policy_only(self):
        self.collected = []
        gate.git(self.work, "push", "-q", "-f", str(self.origin), f"{self.g1}:refs/heads/main")
        self.reader.main = self.g1
        groups, _ = self.reconcile()
        # Live main moved but this checkout still predates G1: wait for G1's push.
        self.assertEqual(groups[self.g2]["state"], "pending")
        self.assertIn("predates the landed base", groups[self.g2]["detail"][0])
        self.move_main(self.g1)
        self.collected = [(self.evidence, []), (self.evidence, [])]
        groups, report = self.reconcile()
        self.assertEqual(report["policy_sha"], self.g1)
        self.assertEqual(groups[self.g1]["state"], "landed")
        self.assertEqual(groups[self.g2]["state"], "admitted")
        self.assertEqual(groups[self.g2]["detail"]["policy_sha"], self.g1)
        self.assertEqual(self.writer.sent[-1][1]["conclusion"], "success")

    def test_reconstruction_mode_collects_the_same_inventory_as_run_gate(self):
        seen = []
        self.native = {"status": "admitted", "mode": "ordinary-bound-merge-group"}
        arguments = SimpleNamespace(repo_root=self.trusted, repository="buster14a/buster",
                                    details_url="https://example.invalid/run")
        def collect(_api, _candidate, checks=gate.CHECKS):
            seen.append(checks)
            return self.evidence, []
        with patch.object(gate, "GitHub", return_value=self.reader), \
                patch.object(gate, "CheckWriter", return_value=self.writer), \
                patch.object(gate, "live_ruleset", return_value={}), \
                patch.object(gate, "retirement_admission", side_effect=lambda *_: dict(self.native)), \
                patch.object(gate, "collect", side_effect=collect):
            gate.reconcile(arguments)
        self.assertEqual(len(seen), 2)
        self.assertTrue(all(checks == gate.required_checks(self.native) for checks in seen))
        self.assertIn("native-retirement-rebind.yml", seen[0])

    def test_new_attempt_during_collection_stays_pending(self):
        rerun = [dict(self.evidence[0], run_attempt=2)]
        self.collected = [(self.evidence, []), (rerun, [])]
        groups, _ = self.reconcile()
        self.assertEqual(groups[self.g1]["state"], "pending")
        self.assertEqual(self.writer.sent[-1][1]["status"], "in_progress")

    def test_failed_or_cancelled_gate_publishes_one_terminal_failure(self):
        with patch.object(gate, "collect", side_effect=gate.AdmissionError(
                "CI complete: cancelled, skipped, neutral or invalid workflow")):
            arguments = SimpleNamespace(repo_root=self.trusted, repository="buster14a/buster",
                                        details_url="https://example.invalid/run")
            with patch.object(gate, "GitHub", return_value=self.reader), \
                    patch.object(gate, "CheckWriter", return_value=self.writer), \
                    patch.object(gate, "live_ruleset", return_value={"self_host_admission": True}), \
                    patch.object(gate, "retirement_admission", return_value=self.native):
                report = gate.reconcile(arguments)
                again = gate.reconcile(arguments)
        self.assertEqual(report["groups"][0]["state"], "rejected")
        self.assertEqual(again["groups"][0]["state"], "published")
        self.assertEqual(len(self.writer.sent), 1)
        self.assertEqual(self.writer.sent[0][1]["conclusion"], "failure")
        self.assertIn("cancelled", self.writer.sent[0][1]["output"]["summary"])

    def test_publication_change_rejects(self):
        values = iter(({"attestation_id": 1}, {"attestation_id": 2}))
        self.collected = [(self.evidence, []), (self.evidence, [])]
        arguments = SimpleNamespace(repo_root=self.trusted, repository="buster14a/buster",
                                    details_url="https://example.invalid/run")
        collected = iter(self.collected)
        with patch.object(gate, "GitHub", return_value=self.reader), \
                patch.object(gate, "CheckWriter", return_value=self.writer), \
                patch.object(gate, "live_ruleset", return_value={}), \
                patch.object(gate, "retirement_admission", side_effect=lambda *_: next(values)), \
                patch.object(gate, "collect", side_effect=lambda *_: next(collected)):
            report = gate.reconcile(arguments)
        self.assertEqual(report["groups"][0]["state"], "rejected")
        self.assertEqual(self.writer.sent[0][1]["conclusion"], "failure")

    def test_replaced_ref_and_divergent_main_never_admit(self):
        self.reader.heads[self.ref1.removeprefix("refs/")] = "f" * 40
        groups, _ = self.reconcile()
        self.assertEqual(groups[self.g1]["state"], "retired")
        self.assertIn("group replaced", groups[self.g1]["detail"])
        gate.git(self.work, "checkout", "-q", "--detach", self.main)
        (self.work / "a.c").write_text("outside the queue\n")
        gate.git(self.work, "commit", "-qam", "divergent")
        self.move_main(gate.git(self.work, "rev-parse", "HEAD"))
        groups, _ = self.reconcile()
        self.assertEqual(groups[self.g2]["state"], "rejected")
        self.assertTrue(all(body.get("conclusion") != "success" for _, body in self.writer.sent))

    def test_second_producer_of_the_name_is_rejected(self):
        self.reader.checks[self.g1] = [{"id": 5, "name": gate.CONTEXT, "head_sha": self.g1,
                                        "app": {"id": gate.GITHUB_ACTIONS_APP_ID},
                                        "external_id": "", "status": "completed",
                                        "conclusion": "success"}]
        groups, _ = self.reconcile()
        self.assertEqual(groups[self.g1]["state"], "rejected")
        self.assertIn("second producer", groups[self.g1]["detail"])
        self.assertEqual(self.writer.sent[0][1]["conclusion"], "failure")

    def test_legacy_groups_are_shadowed_without_writes(self):
        gate.git(self.work, "checkout", "-q", "--detach", self.main)
        (self.work / gate.ADMISSION_WORKFLOW).write_text(LEGACY_YAML)
        gate.git(self.work, "commit", "-qam", "legacy workflow")
        pr = gate.git(self.work, "rev-parse", "HEAD")
        tree = gate.git(self.work, "merge-tree", "--write-tree", self.main, pr)
        legacy = gate.git(self.work, "commit-tree", tree, "-p", self.main, "-p", pr, "-m", "legacy")
        gate.git(self.work, "push", "-q", "-f", str(self.origin), f"{legacy}:{self.ref1}")
        self.reader.heads[self.ref1.removeprefix("refs/")] = legacy
        self.collected = [(self.evidence, []), (self.evidence, [])]
        groups, _ = self.reconcile()
        self.assertEqual((groups[legacy]["owner"], groups[legacy]["state"]), ("legacy", "admitted"))
        self.assertFalse(groups[legacy]["published"])
        self.assertEqual(self.writer.sent, [])
        self.assertNotIn("commits/" + legacy + "/check-runs", self.reader.reads)

    def test_transport_failure_publishes_nothing_and_fails_the_pass(self):
        def broken(*_arguments, **_query):
            raise OSError("HTTP 502")
        self.reader.pages = broken
        groups, _ = self.reconcile()
        self.assertEqual(groups[self.g1]["state"], "retry")
        self.assertEqual(self.writer.sent, [])
        with patch.object(gate, "reconcile", return_value={"groups": [{"state": "retry"}]}):
            with tempfile.TemporaryDirectory() as temporary:
                output = Path(temporary) / "out.json"
                self.assertEqual(gate.main(["reconcile", "--repo-root", ".", "--repository", "a/b",
                                            "--details-url", "u", "--output", str(output)]), 1)

    def test_retired_group_publishes_nothing_and_does_not_fail_maintenance(self):
        original = self.reader.get
        def vanished(path, **query):
            if path == "git/ref/" + self.ref1.removeprefix("refs/"):
                raise gate.APIReadError(path, 404, 4)
            if path.startswith("git/matching-refs/"):
                return []
            return original(path, **query)
        self.reader.get = vanished
        groups, report = self.reconcile()
        self.assertEqual(groups[self.g1]["state"], "retired")
        self.assertFalse(groups[self.g1]["published"])
        self.assertEqual(self.writer.sent, [])
        self.assertEqual(self.native_calls, 0)
        with patch.object(gate, "reconcile", return_value=report), tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary) / "out.json"
            self.assertEqual(gate.main(["reconcile", "--repo-root", ".", "--repository", "a/b",
                                       "--details-url", "u", "--output", str(output)]), 0)

    def test_check_read_404_is_retired_only_after_exact_ref_confirmation(self):
        for retired in (False, True):
            with self.subTest(retired=retired):
                reader = FakeReader(self.main, {self.ref1.removeprefix("refs/"): self.g1})
                writer = FakeWriter(reader)
                def missing_checks(path, *_a, **_k):
                    raise gate.APIReadError(path, 404, 1)
                reader.pages = missing_checks
                if retired:
                    def vanished(path, **_q):
                        if not path.startswith("git/matching-refs/"):
                            raise gate.APIReadError(path, 404, 4)
                        return []
                    reader.get = vanished
                arguments = SimpleNamespace(repo_root=self.trusted, repository="a/b", details_url="u")
                with patch.object(gate, "GitHub", return_value=reader), \
                        patch.object(gate, "CheckWriter", return_value=writer):
                    report = gate.reconcile(arguments)
                group = next(row for row in report["groups"] if row["head"] == self.g1)
                self.assertEqual(group["state"], "retired" if retired else "retry")
                self.assertEqual(writer.sent, [])

    def test_native_transport_failure_is_retry_not_terminal_denial(self):
        head = self.native_group(native_job=False)
        arguments = SimpleNamespace(repo_root=self.trusted, repository="buster14a/buster",
                                    details_url="https://example.invalid/run")
        with patch.object(gate, "GitHub", return_value=self.reader), \
                patch.object(gate, "CheckWriter", return_value=self.writer), \
                patch.object(gate, "retirement_admission", side_effect=OSError("GET actions/runs: HTTP 502")):
            report = gate.reconcile(arguments)
        self.assertEqual(next(row for row in report["groups"] if row["head"] == head)["state"], "retry")
        self.assertEqual(self.writer.sent, [])

    def test_retirement_subprocess_retry_is_not_an_admission_error(self):
        arguments = SimpleNamespace(repo_root=ROOT, repository="a/b")
        with patch.object(gate.subprocess, "run", return_value=SimpleNamespace(
                returncode=75, stdout="", stderr="GET actions/runs: HTTP 502")):
            with self.assertRaisesRegex(OSError, "requires API retry"):
                gate.retirement_admission(arguments, candidate())

    def test_legacy_retired_wait_reports_retry_and_never_green(self):
        with tempfile.TemporaryDirectory() as temporary, \
                patch.object(gate, "run_gate", side_effect=gate.GroupRetired("confirmed retired group")):
            output = Path(temporary) / "out.json"
            code = gate.main(["check-group", "--repo-root", ".", "--event", "event.json",
                              "--repository", "a/b", "--sha", "b" * 40, "--output", str(output)])
            self.assertEqual(code, 75)
            self.assertEqual(json.loads(output.read_text())["status"], "retry")

    def test_legacy_api_failure_reports_retry_and_never_green(self):
        for error in (gate.APIReadError("actions/runs", 502, 4), gate.DelegatedReadError("native API retry")):
            with self.subTest(error=error), tempfile.TemporaryDirectory() as temporary, \
                    patch.object(gate, "run_gate", side_effect=error):
                output = Path(temporary) / "out.json"
                code = gate.main(["check-group", "--repo-root", ".", "--event", "event.json",
                                  "--repository", "a/b", "--sha", "b" * 40, "--output", str(output)])
                self.assertEqual(code, 75)
                self.assertEqual(json.loads(output.read_text())["reason"], "api-read")

    def test_queue_ref_bound_fails_closed_before_any_publication(self):
        refs = [f"{self.g2}:{gate.QUEUE_REF_PREFIX}extra-{index}" for index in range(gate.MAX_QUEUE_REFS)]
        gate.git(self.work, "push", "-q", str(self.origin), *refs)
        with self.assertRaisesRegex(gate.AdmissionError, "exceed the bound"):
            self.reconcile()
        self.assertEqual(self.writer.sent, [])

    def test_reconciler_workflow_is_trusted_bounded_and_event_driven(self):
        text = (ROOT / ".github/workflows/merge-queue-reconcile.yml").read_text()
        self.assertNotIn("pull_request", text)
        self.assertNotIn("merge_group:", text)
        self.assertNotIn("secrets.", text)
        self.assertNotIn("head_sha", text)
        self.assertIn("permissions: {}", text)
        self.assertEqual(text.count(": write"), 1)
        self.assertIn("      checks: write\n", text)
        self.assertIn("          ref: main\n", text)
        self.assertIn("persist-credentials: false", text)
        self.assertIn("timeout-minutes: 10\n", text)
        self.assertIn("cancel-in-progress: false", text)
        self.assertIn("github.event.workflow_run.event == 'merge_group'", text)
        self.assertNotIn("sleep", text)
        for filename in {**gate.CHECKS, **gate.RECONSTRUCTION_CHECK}:
            name = (ROOT / ".github/workflows" / filename).read_text().split("\n", 1)[0]
            self.assertIn("      - " + name.removeprefix("name: ") + "\n", text)



class NoCodeResultsTests(unittest.TestCase):
    def fixture(self):
        rows, jobs = results()
        for run in rows:
            row = jobs[(run["id"], run["run_attempt"])][0]
            if row["name"] != "CI complete":
                row.update(conclusion="skipped", runner_id=0, steps=[])
            jobs[(run["id"], run["run_attempt"])].append(
                dict(row, id=row["id"] + 1000, name="No-code plan / Classify no-code changes",
                     status="completed", conclusion="success", steps=[], runner_id=1))
        return gate.latest_runs(rows, candidate()), jobs

    def test_no_code_is_not_full_execution_evidence(self):
        runs, jobs = self.fixture()
        with self.assertRaises(gate.AdmissionError):
            gate.check_results(runs, jobs, candidate())
        evidence, pending = gate.check_results(runs, jobs, dict(candidate(), no_code=True))
        self.assertFalse(pending)
        self.assertEqual(sum(row["disposition"] == "not-applicable-no-code" for row in evidence), 4)

    def test_missing_failed_cancelled_planner_or_workload_cannot_admit(self):
        fixture = dict(candidate(), no_code=True)
        for conclusion in ("failure", "cancelled", "skipped", None):
            with self.subTest(planner=conclusion):
                runs, jobs = self.fixture()
                next(iter(jobs.values()))[-1]["conclusion"] = conclusion
                with self.assertRaises(gate.AdmissionError):
                    gate.check_results(runs, jobs, fixture)
        runs, jobs = self.fixture()
        next(iter(jobs.values())).pop()
        # A missing planner cannot explain an omitted workload.
        jobs[(2, 1)].pop()
        with self.assertRaises(gate.AdmissionError):
            gate.check_results(runs, jobs, fixture)
        runs, jobs = self.fixture()
        next(iter(jobs.values()))[0]["conclusion"] = "failure"
        with self.assertRaises(gate.AdmissionError):
            gate.check_results(runs, jobs, fixture)

    def test_trusted_rollback_accepts_executed_work_and_never_calls_it_omitted(self):
        rows, jobs = results()
        for run in rows:
            row = jobs[(run["id"], run["run_attempt"])][0]
            jobs[(run["id"], run["run_attempt"])].append(
                dict(row, id=row["id"] + 1000, name="No-code plan / Classify no-code changes",
                     status="completed", conclusion="success", runner_id=1))
        evidence, pending = gate.check_results(gate.latest_runs(rows, candidate()), jobs,
                                               dict(candidate(), no_code=True))
        self.assertFalse(pending)
        self.assertTrue(all(row["disposition"] == "executed" for row in evidence))

    def test_omitted_workflow_rejects_any_allocated_or_failed_workload(self):
        for status, conclusion, runner, steps in (
                ("completed", "failure", 2, []), ("completed", "success", 2, []),
                ("completed", "skipped", 2, []), ("completed", "skipped", 0, [{"name": "work"}]),
                ("queued", None, 0, [])):
            runs, jobs = self.fixture()
            jobs[(2, 1)].append({"id": 9000, "name": "Unexpected optional work", "status": status,
                                 "conclusion": conclusion, "runner_id": runner, "steps": steps})
            with self.subTest(status=status, conclusion=conclusion, runner=runner, steps=steps):
                with self.assertRaises(gate.AdmissionError):
                    gate.check_results(runs, jobs, dict(candidate(), no_code=True))

    def test_native_adapter_rejects_stale_and_failed_plans(self):
        fixture = candidate()
        arguments = SimpleNamespace(repo_root=ROOT)
        native = {"schema": "buster-ci-no-code-v1", "base": fixture["base"],
                  "head": fixture["head"], "tested": fixture["head"], "policy": fixture["policy_sha"],
                  "profile": "no-code", "no_code": True, "reason": "reviewed-prose-only"}
        for mutation in ({"head": "c" * 40}, {"policy": "c" * 40},
                         {"schema": "old"}, {"no_code": "true"}):
            with self.subTest(mutation=mutation), patch.dict(gate.os.environ, {"BUSTER_CI_NO_CODE_DRIVER": "/trusted/driver"}), \
                    patch.object(gate.subprocess, "run", return_value=SimpleNamespace(
                        returncode=0, stdout=json.dumps(dict(native, **mutation)))):
                with self.assertRaises(gate.AdmissionError):
                    gate.trusted_no_code(arguments, fixture)
        with patch.dict(gate.os.environ, {"BUSTER_CI_NO_CODE_DRIVER": "/trusted/driver"}), \
                patch.object(gate.subprocess, "run", return_value=SimpleNamespace(returncode=1, stdout="")):
            with self.assertRaises(gate.AdmissionError):
                gate.trusted_no_code(arguments, fixture)

if __name__ == "__main__":
    unittest.main()
