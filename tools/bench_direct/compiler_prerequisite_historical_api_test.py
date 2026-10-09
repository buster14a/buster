#!/usr/bin/env python3
"""Original preparation/Utility API data fixtures using the actual native bridge.

Current CLOSED/advanced observations and actual original OPEN artifact bytes
are separate transports. No fixture rewrites an API fact to obtain authority.
Run on hosted CI after the prerequisite API collector and native data helpers
are integrated; all API responses below are synthetic and no host work runs.
"""

from __future__ import annotations

import copy
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import unittest
from unittest import mock
from urllib.parse import quote

sys.path.insert(0, str(Path(__file__).resolve().parent))
import authorize  # noqa: E402
from compiler_sampling_historical_api_test import (  # noqa: E402
    API_NAMES, OriginalApi, REPOSITORY, OWNER, BASE, TREE, CONTEXT, HARNESS,
    ACQUISITION_REVISION, ACQUISITION_POLICY, ACQUISITION_HEAD, ADVANCED_HEAD,
    FIRST_PARENT, SECOND_PARENT, PROTOCOL, SINCE, digest, tsv,
)

NATIVE_REVIEW = authorize.prerequisite_review_native
TERMINAL_NATIVE_REVIEW = authorize.terminal_review_native
REAL_RUN = subprocess.run


def prerequisite_plan(utility: bool = False) -> str:
    common = {
        "schema": "buster-compiler-closure-utility-plan-v1" if utility else "buster-compiler-preparation-plan-v1",
        "phase": "utility" if utility else "qualify", "baseline_revision": BASE, "baseline_tree": TREE,
        "candidate_revision": CONTEXT, "candidate_tree": "5" * 40,
    }
    if utility:
        common["pull_head"] = "6" * 40
    common.update({"trusted_revision": HARNESS, "trusted_root": "/home/runner/work/buster/buster/trusted",
                   "protocol_sha256": PROTOCOL, "lab_sha256": "b" * 64})
    if utility:
        common.update({"comparator_sha256": "c" * 64, "receipt_sha256": "d" * 64,
                       "owned_phase_sha256": "e" * 64})
    common.update({"python_sha256": "c" * 64, "python_path": "/usr/bin/python3",
                   "native_driver_sha256": "d" * 64,
                   "source_root": "/tmp/buster-3211-utility-source" if utility else "/tmp/buster-3211-closure-source",
                   "output_root": "/tmp/buster-3211-utility-output" if utility else "/tmp/buster-3211-closure-output"})
    if utility:
        common.update({
            "legacy_treatment": "legacy-rebuild", "snapshot_treatment": "snapshot-v1",
            "toolchain_policy": "clang-release-tests-off-native-v1", "command": "compiler-closure-utility-v1",
            "mode": "main", "legs": "2", "leg_order": "legacy-then-snapshot", "compare_profile": "compiler-compare-v1",
            "lab_target_minutes": "10", "lab_warmups": "1", "lab_cpu": "2", "lab_profile_steps": "none",
            "throughput_profile": "throughput-corpus-v2", "throughput_arguments": "ci-all-p20-w2-t120-cpu2",
            "throughput_cells": "12", "throughput_rounds": "2",
            "leg_clock_scope": "bootstrap-through-export-hashfinalization", "cache_policy": "fresh-mutable-per-leg",
            "utility_charge_policy": "all-physical-residual-to-snapshot",
            "utility_criterion": "snapshot-plus-residual-less-than-legacy",
            "physical_budget_seconds": "5400", "worker_budget_seconds": "5280",
            "tail_budget_seconds": "120", "study_budget_seconds": "10800",
        })
    else:
        common.update({
            "baseline_treatment": "legacy-rebuild", "candidate_treatment": "snapshot-v1", "closure_policy": "snapshot-v1",
            "toolchain_policy": "clang-release-tests-off-native-v1", "command": "compiler_closure-qualify-v1",
            "lab_repetitions": "5", "compiler_repetitions": "5", "aa_families": "3", "aa_primary": "wall",
            "aa_confidence_percent": "95", "aa_ratio_lower": "0.995", "aa_ratio_upper": "1.005",
            "net_preparation": "snapshot-less-than-legacy", "physical_budget_seconds": "5400",
            "worker_budget_seconds": "5280", "tail_budget_seconds": "120",
        })
    return tsv(common)


def prerequisite_allowlist(plan: str, utility: bool = False) -> str:
    return tsv({
        "schema": "buster-compiler-closure-utility-admission-v1" if utility else "buster-compiler-preparation-admission-v1",
        "state": "utility" if utility else "qualify", "freeze_revision": ACQUISITION_REVISION,
        "freeze_sha256": digest(plan), "protocol_sha256": PROTOCOL, "history_since": SINCE,
        "repository": REPOSITORY, "owner_login": OWNER["login"], "owner_id": str(OWNER["id"]),
    })


class PrerequisiteApi(OriginalApi):
    """The approved complete inventory API shape, with distinct prerequisite plans."""

    def __init__(self, *, utility=False, merge=False, state="closed", advanced=True, negative=False):
        super().__init__(merge=merge)
        self.utility = utility
        self.prefix = "utility" if utility else "preparation"
        self.phase = "utility" if utility else "qualify"
        self.plan_path = authorize.UTILITY_PLAN if utility else authorize.PREPARATION_PLAN
        self.allowlist_path = authorize.UTILITY_ALLOWLIST if utility else authorize.PREPARATION_ALLOWLIST
        self.check_name = authorize.UTILITY_CHECK if utility else authorize.PREPARATION_CHECK
        self.plan = prerequisite_plan(utility)
        profile = "compiler-baseline-closure-utility-v1" if utility else "compiler-baseline-closure-qualification-v1"
        self.line = f"profile: {profile} packet: 0 freeze: {ACQUISITION_REVISION}"
        self.contents = {
            (self.plan_path, ACQUISITION_REVISION): self.plan,
            (self.allowlist_path, ACQUISITION_POLICY): prerequisite_allowlist(self.plan, utility),
            (authorize.COMPARE_REQUEST, ACQUISITION_HEAD): self.line + "\n",
        }
        for parent in ((FIRST_PARENT, SECOND_PARENT) if merge else (FIRST_PARENT,)):
            self.records[f"/compare/{parent}...{ACQUISITION_HEAD}"]["files"][0]["patch"] = (
                "@@ -1 +1 @@\n-old request\n+" + self.line)
        old_path = (f"/commits/{ACQUISITION_HEAD}/check-runs?check_name=" +
                    quote(authorize.SAMPLING_CHECK) + "&filter=all&per_page=100")
        self.records.pop(old_path)
        check = {
            "id": self.executor + 100000, "name": self.check_name, "head_sha": ACQUISITION_HEAD,
            "app": {"id": 15368},
            "external_id": ("buster-compiler-closure-utility-v1:" if utility else "buster-compiler-preparation-v1:") +
                f"{digest(self.plan)}:{self.phase}:0:{self.current}:{self.executor}:1",
            "status": "completed", "conclusion": "success",
            "output": {"title": "Valid unqualified utility packet" if utility else "Valid unqualified preparation packet"},
        }
        self.records[self.check_path()] = {"total_count": 1, "check_runs": [check]}
        host_name = "Compiler closure utility" if utility else "Compiler preparation qualification"
        self.records[f"/actions/runs/{self.executor}/attempts/1/jobs?per_page=100"] = {"total_count": 1, "jobs": [{
            "id": 90000, "run_id": self.executor, "run_attempt": 1, "head_sha": ACQUISITION_POLICY,
            "name": host_name, "status": "completed", "conclusion": "success",
            "started_at": "2026-10-09T00:01:02Z", "completed_at": "2026-10-09T00:01:03Z",
            "runner_id": 9700, "runner_name": "synthetic-9700x", "labels": ["self-hosted", "9700X"],
        }]}
        self.observe_pull(state, ADVANCED_HEAD if advanced else ACQUISITION_HEAD)
        if negative:
            self.check()["conclusion"] = "failure"
            self.check()["output"]["title"] = ("Complete unqualified Utility; net criterion not met" if utility else
                                               "Unqualified preparation controls failed")
            for endpoint in ("", "/attempts/1"):
                self.records[f"/actions/runs/{self.executor}{endpoint}"]["conclusion"] = "failure"
            self.executor_inventory[0]["conclusion"] = "failure"

    def check_path(self, head=None):
        return (f"/commits/{head or ACQUISITION_HEAD}/check-runs?check_name=" +
                quote(self.check_name) + "&filter=all&per_page=100")

    def check(self, head=None):
        return self.records[self.check_path(head)]["check_runs"][0]

    def observe_pull(self, state, head):
        # Model the mutable observed API record, never any native/raw TSV.
        self.pull()["state"] = state
        self.pull()["head"]["sha"] = head



TERMINAL_NAMES = (
    "schema", "kind", "phase", "packet", "family", "executor_inventory_count",
    "selected_executor_inventory_id", "physical_job_id", "physical_job_state",
    "physical_job_conclusion", "physical_job_started_at", "physical_job_completed_at",
    "terminal_state", "terminal_api_sha256", "terminal_api_bytes",
    "context_revision", "context_main_relation",
)


def terminalize(api, kind, *, conclusion="cancelled", physical=False, timestamps=(None, None), no_executor=False):
    """Change actual synthetic API records, not emitted observations or TSV."""
    api.kind = kind
    api.no_executor = no_executor
    api.context_revision = api.originals()[0]["head_sha"] if no_executor else None
    execution, request = api.originals()
    head, policy = request["head_sha"], execution["head_sha"]
    check_name = (authorize.SAMPLING_CHECK if kind == "sampling" else
                  authorize.UTILITY_CHECK if kind == "utility" else authorize.PREPARATION_CHECK)
    api.terminal_check_path = (f"/commits/{head}/check-runs?check_name=" + quote(check_name) +
                               "&filter=all&per_page=100")
    api.records[api.terminal_check_path] = {"total_count": 0, "check_runs": []}
    if no_executor:
        api.executor_inventory = [row for row in api.executor_inventory if row["id"] != api.executor]
    else:
        for endpoint in ("", "/attempts/1"):
            api.records[f"/actions/runs/{api.executor}{endpoint}"]["conclusion"] = conclusion
        for row in api.executor_inventory:
            if row["id"] == api.executor:
                row["conclusion"] = conclusion
    host_name = ("Sampling qualification packet" if kind == "sampling" else
                 "Compiler closure utility" if kind == "utility" else "Compiler preparation qualification")
    jobs = [{
        "id": 90000 + api.executor, "run_id": api.executor, "run_attempt": 1, "head_sha": policy,
        "name": host_name, "status": "completed", "conclusion": conclusion,
        "started_at": timestamps[0], "completed_at": timestamps[1],
    }] if physical else []
    api.terminal_jobs_path = f"/actions/runs/{api.executor}/attempts/1/jobs?per_page=100"
    api.records[api.terminal_jobs_path] = {"total_count": len(jobs), "jobs": jobs}
    api.terminal_artifact_path = f"/actions/runs/{api.executor}/artifacts?per_page=100&page=1"
    api.records[api.terminal_artifact_path] = {"total_count": 0, "artifacts": []}
    return api


def terminal_prerequisite_api(utility=False, **options):
    return terminalize(PrerequisiteApi(utility=utility), "utility" if utility else "preparation", **options)


class HistoricalPrerequisiteApiTest(unittest.TestCase):
    def assert_transport_then_native(self, records, *, utility=False):
        self.assertEqual(set(records), {"allowlist", "request", "facts", "history", "plan", "api"})
        self.assertTrue(all(isinstance(value, str) for value in records.values()))
        proof = authorize.sampling_review_record(records["api"])
        self.assertEqual(tuple(proof), API_NAMES)
        self.assertEqual(proof["schema"], "buster-compiler-prerequisite-historical-api-v1")
        for field, member in (("allowlist_sha256", "allowlist"), ("facts_sha256", "facts"),
                              ("history_sha256", "history"), ("freeze_sha256", "plan")):
            self.assertEqual(proof[field], digest(records[member]))
        self.assertEqual((proof["parent_freeze_sha256"], proof["acquisition_sha256"]), ("-", "-"))
        facts = authorize.sampling_review_record(records["facts"])
        self.assertEqual(facts["request_head"], proof["request_head"])
        self.assertEqual(facts["trusted_revision"], proof["policy_revision"])
        self.assertEqual(facts["pull_state"], self.active_api.pull()["state"])
        self.assertEqual(proof["pull_current_head"], self.active_api.pull()["head"]["sha"])
        self.assertEqual(facts["fresh_parent_0"], records["request"].rstrip("\n"))
        self.assertTrue(records["history"].startswith("\t".join(authorize.SAMPLING_HISTORY_HEADER) + "\n"))
        self.native_records.append(copy.deepcopy(records))
        # Observe the fixed real subprocess bridge, never replace its outcome.
        return NATIVE_REVIEW(records, utility=utility)

    def review(self, api, prefix=None):
        self.active_api = api
        self.native_records = []
        execution, request = api.originals()
        with mock.patch.object(authorize, "prerequisite_review_native", side_effect=self.assert_transport_then_native):
            result = authorize.review_prerequisite_authority(api, REPOSITORY, execution, request,
                                                            prefix, utility=api.utility)
        return result

    def rejects_before_native(self, api, originals=None):
        execution, request = originals if originals else api.originals()
        with mock.patch.object(authorize, "prerequisite_review_native") as bridge:
            with self.assertRaises(ValueError):
                authorize.review_prerequisite_authority(api, REPOSITORY, execution, request, utility=api.utility)
            bridge.assert_not_called()

    def assert_authority(self, api, authority):
        prefix = api.prefix
        admitted = authority["admitted"]
        self.assertTrue(authority["historical_review"])
        self.assertEqual(authority["pull"], str(api.pull()["number"]))
        self.assertEqual(admitted[prefix + "_historical_valid"], "true")
        self.assertEqual(admitted[prefix + "_historical_execution_authority"], "false")
        self.assertEqual(admitted[prefix + "_historical_qualification"], "unqualified")
        self.assertNotIn(prefix + "_admitted", admitted)
        self.assertEqual(admitted[prefix + "_policy_revision"], ACQUISITION_POLICY)
        self.assertEqual(admitted[prefix + "_trusted_revision"], HARNESS)
        self.assertNotEqual(ACQUISITION_POLICY, HARNESS)
        self.assertEqual((admitted[prefix + "_phase"], admitted[prefix + "_packet"], admitted[prefix + "_family"]),
                         (api.phase, "0", prefix))
        self.assertEqual(admitted[prefix + "_plan_revision"], ACQUISITION_REVISION)
        self.assertEqual(admitted[prefix + "_plan_sha256"], digest(api.plan))
        self.assertEqual(admitted[prefix + "_historical_api_sha256"],
                         hashlib.sha256(authority["historical_records"]["api"]).hexdigest())
        self.assertEqual(authority["plan_bytes"], api.plan.encode())
        self.assertEqual(set(authority["historical_records"]), {"allowlist", "request", "facts", "history", "plan", "api"})
        self.assertTrue(all(isinstance(value, bytes) for value in authority["historical_records"].values()))
        self.assertEqual(set(authority["raw"]), {"allowlist.tsv", "request.txt", "facts.tsv", "history.tsv", "plan.tsv"})
        for name, member in (("allowlist.tsv", "allowlist"), ("request.txt", "request"),
                             ("facts.tsv", "facts"), ("history.tsv", "history"), ("plan.tsv", "plan")):
            self.assertEqual(authority["raw"][name], authority["historical_records"][member])
        proof = authority["native_api_proof"]
        self.assertEqual(tsv(proof).encode(), authority["historical_records"]["api"])
        self.assertEqual(proof["pull_state"], api.pull()["state"])
        self.assertEqual(proof["pull_current_head"], api.pull()["head"]["sha"])
        envelope = json.loads(authority["terminal_api_envelope"])
        self.assertEqual(envelope["native_api_proof"], proof)
        self.assertEqual(envelope["api_observations"][f"/actions/runs/{api.executor}/attempts/1"], api.originals()[0])
        self.assertEqual(authority["terminal_api_sha256"], hashlib.sha256(authority["terminal_api_envelope"]).hexdigest())

    def test_both_closed_advanced_prerequisites_use_original_policy_and_real_native_output(self):
        for utility in (False, True):
            with self.subTest(utility=utility):
                api = PrerequisiteApi(utility=utility)
                authority = self.review(api, [])
                self.assert_authority(api, authority)
                self.assertEqual(authority["facts"]["pull_state"], "closed")
                self.assertEqual(api.pull()["head"]["sha"], ADVANCED_HEAD)
                self.assertEqual(authority["facts"]["request_head"], ACQUISITION_HEAD)
                self.assertEqual(len(self.native_records), 1)
                self.assertIn(f"/actions/runs/{api.executor}/attempts/1", api.calls)
                self.assertIn(f"/compare/{ACQUISITION_REVISION}...{ACQUISITION_POLICY}", api.calls)
                failures, _ = authorize.verify(REPOSITORY, api.current, ACQUISITION_HEAD,
                    authority["request"], [api.pull()], expected_run_attempt=1)
                self.assertTrue(failures)

    def test_complete_negative_titles_and_actual_executor_failure_remain_data(self):
        for utility in (False, True):
            with self.subTest(utility=utility):
                api = PrerequisiteApi(utility=utility, negative=True)
                authority = self.review(api, [])
                self.assert_authority(api, authority)
                admitted = authority["admitted"]
                self.assertEqual(admitted[api.prefix + "_historical_executor_conclusion"], "failure")
                self.assertEqual(admitted[api.prefix + "_historical_check_conclusion"], "failure")
                self.assertEqual(admitted[api.prefix + "_historical_check_title"], api.check()["output"]["title"])
                self.assertEqual(api.records[f"/actions/runs/{api.executor}/attempts/1/jobs?per_page=100"]
                                 ["jobs"][0]["conclusion"], "success")

    def test_two_actual_parents_keep_fresh_marker_and_exact_compare_head(self):
        for utility in (False, True):
            api = PrerequisiteApi(utility=utility, merge=True)
            authority = self.review(api, [])
            self.assertEqual(authority["facts"]["parent_count"], "2")
            self.assertEqual(authority["facts"]["fresh_parent_0"], authority["facts"]["fresh_parent_1"])
            for parent in (FIRST_PARENT, SECOND_PARENT):
                with self.subTest(utility=utility, parent=parent):
                    changed = PrerequisiteApi(utility=utility, merge=True)
                    changed.records[f"/compare/{parent}...{ACQUISITION_HEAD}"]["files"][0]["patch"] = " inherited"
                    self.rejects_before_native(changed)

    def original_and_current(self, utility=False):
        # The retained OPEN transport is constructed by a separate real native
        # review of the original API observation. It is never re-encoded from
        # the later CLOSED facts to bypass either native policy.
        api = PrerequisiteApi(utility=utility, state="open", advanced=False)
        original = self.review(api, [])
        original_files = copy.deepcopy(original["raw"])
        api.observe_pull("closed", ADVANCED_HEAD)
        current = self.review(api, [])
        return api, original, original_files, current

    def test_actual_open_artifact_binds_actual_current_closed_facts_with_real_helper(self):
        for utility in (False, True):
            with self.subTest(utility=utility):
                api, original, files, current = self.original_and_current(utility)
                before = copy.deepcopy(current)
                with mock.patch.object(subprocess, "run", wraps=REAL_RUN) as process:
                    bound = authorize.bind_historical_original_transport(current, files, api.prefix)
                self.assertEqual(process.call_count, 2)
                first = process.call_args_list[0].args[0]
                self.assertEqual(first[1:3], ["compiler_profile_qualification", "--validate-historical-" + api.prefix])
                self.assertEqual(len(first), 10)
                command = process.call_args_list[1].args[0]
                self.assertEqual(command[1:3], ["compiler_profile_qualification", "--validate-historical-original-facts"])
                self.assertEqual(len(command), 6)
                expected = {"historical_original_facts_valid": "true",
                            "current_facts_sha256": hashlib.sha256(current["raw"]["facts.tsv"]).hexdigest(),
                            "original_facts_sha256": hashlib.sha256(files["facts.tsv"]).hexdigest(),
                            "historical_execution_authority": "false", "qualification": "unqualified"}
                self.assertEqual(bound["historical_original_facts_binding"], expected)
                self.assertEqual(bound["raw_original"], files)
                self.assertEqual(current, before)
                self.assertEqual(authorize.sampling_review_record(files["facts.tsv"].decode())["pull_state"], "open")
                self.assertEqual(authorize.sampling_review_record(current["raw"]["facts.tsv"].decode())["pull_state"], "closed")
                self.assertNotEqual(files["facts.tsv"], current["raw"]["facts.tsv"])
                for name in set(files) - {"facts.tsv"}:
                    self.assertEqual(files[name], current["raw"][name])
                self.assertEqual(original["admitted"][api.prefix + "_policy_revision"],
                                 current["admitted"][api.prefix + "_policy_revision"])


    def test_original_and_latest_attempt_types_cannot_be_relabelled(self):
        for utility in (False, True):
            for run in (10000, 20000):
                for endpoint in ("", "/attempts/1"):
                    for attempt in (2, "1", True, None):
                        with self.subTest(utility=utility, run=run, endpoint=endpoint, attempt=attempt):
                            api = PrerequisiteApi(utility=utility)
                            api.records[f"/actions/runs/{run}{endpoint}"]["run_attempt"] = attempt
                            self.rejects_before_native(api)
            api = PrerequisiteApi(utility=utility)
            execution, request = api.originals()
            execution["head_sha"] = HARNESS
            self.rejects_before_native(api, (execution, request))
            execution, request = api.originals()
            request["head_sha"] = ADVANCED_HEAD
            self.rejects_before_native(api, (execution, request))

    def test_actual_api_owner_membership_ancestry_and_compare_head_are_required(self):
        for utility in (False, True):
            mutations = {
                "foreign_request_actor": lambda a: a.records[f"/actions/runs/{a.current}/attempts/1"]
                    ["actor"].update(id=OWNER["id"] + 1),
                "foreign_executor_trigger": lambda a: a.records[f"/actions/runs/{a.executor}/attempts/1"]
                    ["triggering_actor"].update(login="foreign"),
                "foreign_request_repo": lambda a: a.records[f"/actions/runs/{a.current}/attempts/1"]
                    ["head_repository"].update(full_name="foreign/buster"),
                "foreign_pull_author": lambda a: a.pull()["user"].update(id=OWNER["id"] + 1),
                "foreign_pull_repo": lambda a: a.pull()["head"]["repo"].update(full_name="foreign/buster"),
                "invalid_observed_state": lambda a: a.pull().update(state="merged"),
                "duplicate_association": lambda a: a.records[f"/commits/{ACQUISITION_HEAD}/pulls?per_page=100"]
                    .append(copy.deepcopy(a.pull())),
                "foreign_association": lambda a: a.pull().update(number=43),
                "source_commit_relabel": lambda a: a.records[f"/commits/{ACQUISITION_HEAD}"].update(sha=ADVANCED_HEAD),
                "foreign_compare_head": lambda a: a.records[f"/compare/{FIRST_PARENT}...{ACQUISITION_HEAD}"]
                    ["commits"][0].update(sha=ADVANCED_HEAD),
                "incomplete_compare": lambda a: a.records[f"/compare/{FIRST_PARENT}...{ACQUISITION_HEAD}"]
                    .update(total_commits=2),
                "unprotected_policy": lambda a: a.records.update({f"/compare/{ACQUISITION_POLICY}...main": {"status": "behind"}}),
                "foreign_plan_lineage": lambda a: a.records.update({f"/compare/{ACQUISITION_REVISION}...{ACQUISITION_POLICY}":
                                                                   {"status": "diverged"}}),
                "foreign_harness_lineage": lambda a: a.records.update({f"/compare/{HARNESS}...{ACQUISITION_POLICY}":
                                                                      {"status": "behind"}}),
            }
            for name, change in mutations.items():
                with self.subTest(utility=utility, mutation=name):
                    api = PrerequisiteApi(utility=utility)
                    change(api)
                    # Some semantic failures are deliberately delegated to the
                    # real native validator; neither path may emit authority.
                    with self.assertRaises(ValueError):
                        self.review(api, [])

    def test_executor_inventory_and_supplied_prefix_must_be_complete_and_unique(self):
        for utility in (False, True):
            for mutation in ("empty", "duplicate", "count", "foreign_title", "foreign_id"):
                with self.subTest(utility=utility, mutation=mutation):
                    api = PrerequisiteApi(utility=utility)
                    if mutation == "empty":
                        api.executor_inventory = []
                    elif mutation == "duplicate":
                        api.executor_inventory.append(copy.deepcopy(api.executor_inventory[0]))
                    elif mutation == "count":
                        api.executor_inventory_total = 2
                    elif mutation == "foreign_title":
                        api.executor_inventory[0]["display_title"] = f"9700X request {api.current}.1 head {ADVANCED_HEAD}"
                    else:
                        api.executor_inventory[0]["id"] += 1
                    self.rejects_before_native(api)
            api = PrerequisiteApi(utility=utility)
            execution, request = api.originals()
            with mock.patch.object(authorize, "prerequisite_review_native") as bridge:
                with self.assertRaises(ValueError):
                    authorize.review_prerequisite_authority(api, REPOSITORY, execution, request,
                                                           [["fabricated prior attempt"]], utility=utility)
                bridge.assert_not_called()

    def test_exact_positive_and_complete_negative_check_pairing_is_native_checked(self):
        for utility in (False, True):
            for negative in (False, True):
                for mutation in ("app", "head", "external", "duplicate", "title", "conclusion", "executor"):
                    with self.subTest(utility=utility, negative=negative, mutation=mutation):
                        api = PrerequisiteApi(utility=utility, negative=negative)
                        check = api.check()
                        if mutation == "app":
                            check["app"]["id"] = 1
                        elif mutation == "head":
                            check["head_sha"] = ADVANCED_HEAD
                        elif mutation == "external":
                            check["external_id"] += ":foreign"
                        elif mutation == "duplicate":
                            api.records[api.check_path()]["check_runs"].append(copy.deepcopy(check))
                        elif mutation == "title":
                            check["output"]["title"] = ("Valid unqualified preparation packet" if utility else
                                                        "Valid unqualified utility packet")
                        elif mutation == "conclusion":
                            check["conclusion"] = "success" if negative else "failure"
                        else:
                            for endpoint in ("", "/attempts/1"):
                                api.records[f"/actions/runs/{api.executor}{endpoint}"]["conclusion"] = (
                                    "success" if negative else "failure")
                            api.executor_inventory[0]["conclusion"] = "success" if negative else "failure"
                        with self.assertRaises(ValueError):
                            self.review(api, [])

    def test_current_raw_and_record_map_drift_is_rejected_before_either_native_call(self):
        for utility in (False, True):
            api, _, files, current = self.original_and_current(utility)
            changes = {
                "extra_record": lambda a: a["historical_records"].update(extra=b"unreviewed\n"),
                "record_mismatch": lambda a: a["historical_records"].update(plan=b"foreign\n"),
                "raw_mismatch": lambda a: a["raw"].update(plan=a["raw"]["plan.tsv"]),
                "missing_record": lambda a: a["historical_records"].pop("history"),
                "facts_object": lambda a: a["facts"].update(pull_state="open"),
                "request_freshness": lambda a: a["raw"].update({"request.txt": b"inherited selector\n"}),
                "api_bytes": lambda a: a["historical_records"].update(api=b"foreign\n"),
                "api_digest": lambda a: a["admitted"].update({api.prefix + "_historical_api_sha256": "0" * 64}),
                "execution_boundary": lambda a: a["admitted"].update({api.prefix + "_historical_execution_authority": "true"}),
                "admission_boundary": lambda a: a["admitted"].update({api.prefix + "_admitted": "true"}),
                "review_type": lambda a: a.update(historical_review=1),
            }
            for name, change in changes.items():
                with self.subTest(utility=utility, mutation=name):
                    changed = copy.deepcopy(current)
                    change(changed)
                    with mock.patch.object(subprocess, "run", wraps=REAL_RUN) as process:
                        with self.assertRaises(ValueError):
                            authorize.bind_historical_original_transport(changed, files, api.prefix)
                        process.assert_not_called()

    def test_original_nonfacts_records_cannot_drift_from_current_committed_records(self):
        for utility in (False, True):
            api, _, files, current = self.original_and_current(utility)
            for name in ("allowlist.tsv", "request.txt", "history.tsv", "plan.tsv"):
                with self.subTest(utility=utility, member=name):
                    changed = copy.deepcopy(files)
                    changed[name] += b"foreign\tvalue\n"
                    with mock.patch.object(subprocess, "run", wraps=REAL_RUN) as process:
                        with self.assertRaises(ValueError):
                            authorize.bind_historical_original_transport(current, changed, api.prefix)
                        process.assert_not_called()

    def test_retained_original_facts_are_native_bound_without_rewriting_observations(self):
        for utility in (False, True):
            api, _, files, current = self.original_and_current(utility)
            edits = {"pull_state": "closed", "trusted_revision": HARNESS, "request_head": ADVANCED_HEAD,
                     "owner_id": str(OWNER["id"] + 1), "actor_login": "foreign",
                     "executor_run_attempt": "2", "fresh_parent_0": "inherited selector"}
            for field, value in edits.items():
                with self.subTest(utility=utility, field=field):
                    changed = copy.deepcopy(files)
                    facts = authorize.sampling_review_record(changed["facts.tsv"].decode())
                    facts[field] = value
                    changed["facts.tsv"] = tsv(facts).encode()
                    before = copy.deepcopy(current)
                    with mock.patch.object(subprocess, "run", wraps=REAL_RUN) as process:
                        with self.assertRaises(ValueError):
                            authorize.bind_historical_original_transport(current, changed, api.prefix)
                    self.assertEqual(process.call_count, 2)
                    self.assertEqual(process.call_args_list[0].args[0][2], "--validate-historical-" + api.prefix)
                    self.assertEqual(process.call_args_list[1].args[0][2], "--validate-historical-original-facts")
                    self.assertEqual(current, before)

    def test_coherently_reencoded_api_proof_is_revalidated_by_real_current_native_bridge(self):
        for utility in (False, True):
            api, _, files, current = self.original_and_current(utility)
            for field, value in (("executor_actor_id", str(OWNER["id"] + 1)),
                                 ("request_latest_attempt", "2"), ("policy_revision", HARNESS),
                                 ("request_head", ADVANCED_HEAD), ("first_parent", SECOND_PARENT),
                                 ("check_external_id", "foreign"), ("pull_state", "open")):
                with self.subTest(utility=utility, field=field):
                    changed = copy.deepcopy(current)
                    changed["native_api_proof"][field] = value
                    changed["historical_records"]["api"] = tsv(changed["native_api_proof"]).encode()
                    changed["admitted"][api.prefix + "_historical_api_sha256"] = hashlib.sha256(
                        changed["historical_records"]["api"]).hexdigest()
                    with mock.patch.object(subprocess, "run", wraps=REAL_RUN) as process:
                        with self.assertRaises(ValueError):
                            authorize.bind_historical_original_transport(changed, files, api.prefix)
                    self.assertEqual(process.call_count, 1)
                    self.assertEqual(process.call_args.args[0][2], "--validate-historical-" + api.prefix)



class HistoricalTerminalApiTest(unittest.TestCase):
    def assert_transport_then_native(self, records, kind):
        names = {"allowlist", "request", "facts", "history", "api", "terminal", "envelope"}
        names |= {"freeze", "parent", "acquisition"} if kind == "sampling" else {"plan"}
        self.assertEqual(set(records), names)
        self.assertTrue(all(isinstance(value, str) for value in records.values()))
        proof = authorize.sampling_review_record(records["api"])
        terminal = authorize.sampling_review_record(records["terminal"])
        self.assertEqual(tuple(proof), API_NAMES)
        self.assertEqual(tuple(terminal), TERMINAL_NAMES)
        self.assertEqual(terminal["schema"], "buster-compiler-historical-terminal-v1")
        self.assertEqual(proof["schema"], "buster-main-sampling-historical-api-v1" if kind == "sampling" else
                         "buster-compiler-prerequisite-historical-api-v1")
        for member, field in (("allowlist", "allowlist_sha256"), ("facts", "facts_sha256"),
                              ("history", "history_sha256"), ("freeze" if kind == "sampling" else "plan", "freeze_sha256")):
            self.assertEqual(proof[field], digest(records[member]))
        self.assertEqual(terminal["terminal_api_sha256"], digest(records["envelope"]))
        self.assertEqual(terminal["terminal_api_bytes"], str(len(records["envelope"].encode("utf-8"))))
        envelope = json.loads(records["envelope"])
        self.assertEqual(records["envelope"], json.dumps(envelope, sort_keys=True, separators=(",", ":"), ensure_ascii=True))
        self.assertEqual(envelope["native_api_proof"], proof)
        self.assertEqual(envelope["api_observations"][f"/actions/runs/{self.active_api.current}/attempts/1"],
                         self.active_api.originals()[1])
        facts = authorize.sampling_review_record(records["facts"])
        self.assertEqual(facts["pull_state"], self.active_api.pull()["state"])
        self.assertEqual(proof["pull_current_head"], self.active_api.pull()["head"]["sha"])
        self.assertEqual(facts["request_head"], self.active_api.originals()[1]["head_sha"])
        self.assertEqual(facts["fresh_parent_0"], records["request"].rstrip("\n"))
        self.native_records.append(copy.deepcopy(records))
        return TERMINAL_NATIVE_REVIEW(records, kind)

    def review(self, api, *, context=None, prefix=None):
        self.active_api = api
        self.native_records = []
        execution, request = api.originals()
        with mock.patch.object(authorize, "terminal_review_native", side_effect=self.assert_transport_then_native):
            return authorize.review_terminal_authority(api, REPOSITORY, request,
                None if api.no_executor else execution, api.kind,
                context_revision=api.context_revision if context is None else context, prefix_attempts=prefix)

    def reject_before_native(self, api, **options):
        execution, request = api.originals()
        with mock.patch.object(authorize, "terminal_review_native") as bridge:
            with self.assertRaises(ValueError):
                authorize.review_terminal_authority(api, REPOSITORY, request,
                    None if api.no_executor else execution, api.kind,
                    context_revision=options.get("context", api.context_revision),
                    prefix_attempts=options.get("prefix"))
            bridge.assert_not_called()

    def assert_authority(self, api, authority, state):
        kind = api.kind
        admitted, proof, terminal = authority["admitted"], authority["native_api_proof"], authority["terminal_proof"]
        self.assertIs(authority["historical_terminal_review"], True)
        self.assertNotIn("historical_review", authority)
        self.assertEqual(authority["pull"], str(api.pull()["number"]))
        records = authority["historical_records"]
        self.assertEqual(len(records), 10 if kind == "sampling" else 8)
        self.assertTrue(all(isinstance(value, bytes) for value in records.values()))
        self.assertEqual(records["api"], tsv(proof).encode())
        self.assertEqual(records["terminal"], tsv(terminal).encode())
        self.assertEqual(records["envelope"], authority["terminal_api_envelope"])
        self.assertEqual(terminal["terminal_api_sha256"], hashlib.sha256(records["envelope"]).hexdigest())
        self.assertEqual(terminal["terminal_api_bytes"], str(len(records["envelope"])))
        self.assertEqual(authority["terminal_api_sha256"], terminal["terminal_api_sha256"])
        for field, value in (("historical_terminal_valid", "true"), ("historical_valid", "false"),
                             ("historical_measurement_valid", "false"), ("historical_execution_authority", "false"),
                             ("historical_qualification", "unqualified"), ("historical_terminal_state", state)):
            self.assertEqual(admitted[kind + "_" + field], value)
        self.assertNotIn(kind + "_admitted", admitted)
        self.assertEqual(admitted[kind + "_historical_api_sha256"], hashlib.sha256(records["api"]).hexdigest())
        self.assertEqual(admitted[kind + "_historical_terminal_api_sha256"], terminal["terminal_api_sha256"])
        self.assertEqual(admitted[kind + "_historical_terminal_api_bytes"], str(len(records["envelope"])))
        self.assertEqual(admitted[kind + "_historical_request_run_id"], str(api.current))
        self.assertEqual(admitted[kind + "_historical_request_head"], api.originals()[1]["head_sha"])
        self.assertEqual(proof["pull_state"], "closed")
        self.assertEqual(proof["pull_current_head"], ADVANCED_HEAD)
        self.assertEqual(authority["facts"]["pull_state"], "closed")
        self.assertEqual(admitted[kind + "_trusted_revision"], HARNESS)
        self.assertEqual(len(self.native_records), 1)
        self.assertIs(authority["artifact_inventory_complete"], True)
        envelope = json.loads(records["envelope"])
        selection = envelope["artifact_inventory_selection"]
        self.assertIs(selection["complete"], True)
        self.assertEqual(selection["expected_name"], authority["expected_artifact_name"])
        self.assertEqual(selection["selected_id"], authority["selected_artifact"]["id"] if authority["selected_artifact"] else None)
        if api.no_executor:
            self.assertEqual((proof["policy_revision"], proof["policy_main_relation"]), ("-", "-"))
            self.assertEqual((authority["facts"]["trusted_revision"], authority["facts"]["executor_run_id"],
                              authority["facts"]["executor_run_attempt"]), ("-", "-", "-"))
            self.assertEqual(records["allowlist"], b"")
            self.assertEqual(terminal["executor_inventory_count"], "0")
            self.assertEqual(terminal["context_revision"], api.context_revision)
            self.assertEqual(authority["historical_context_revision"], api.context_revision)
            for field in API_NAMES:
                if field.startswith("executor_") or field.startswith("check_"):
                    self.assertEqual(proof[field], "-")
            self.assertNotIn(api.terminal_jobs_path, api.calls)
            self.assertFalse(any("/artifacts?" in path for path in api.calls))
            self.assertEqual(selection["pages"], [])
        else:
            policy = api.originals()[0]["head_sha"]
            self.assertEqual(proof["policy_revision"], policy)
            self.assertEqual(terminal["context_revision"], "-")
            self.assertEqual(terminal["executor_inventory_count"], "1")
            self.assertEqual(terminal["selected_executor_inventory_id"], str(api.executor))
            self.assertEqual(admitted[kind + "_historical_executor_run_id"], str(api.executor))
            self.assertEqual(admitted[kind + "_historical_executor_run_attempt"], "1")
            self.assertIn(api.terminal_jobs_path, api.calls)
            self.assertIn(api.terminal_artifact_path, api.calls)
        job = authority["selected_physical_job"]
        for field in ("id", "state", "conclusion", "started_at", "completed_at"):
            key = "physical_job_" + field
            if job is None:
                self.assertEqual(terminal[key], "-")
            else:
                source = "status" if field == "state" else field
                self.assertEqual(terminal[key], str(job[source]) if job.get(source) is not None else "-")
            self.assertEqual(admitted[kind + "_historical_" + key], terminal[key])
        self.assertNotIn("physical_wall_us", authority)
        self.assertNotIn("slot_results", authority)
        self.assertNotIn("raw_original", authority)

    def test_known_executor_before_queue_and_check_absence_are_charged_data(self):
        for utility in (False, True):
            for conclusion, state in (("failure", "failed"), ("cancelled", "cancelled")):
                with self.subTest(utility=utility, conclusion=conclusion):
                    api = terminal_prerequisite_api(utility, conclusion=conclusion)
                    authority = self.review(api, prefix=[])
                    self.assert_authority(api, authority, state)
                    self.assertEqual(authority["selected_physical_job"], None)
                    self.assertEqual(authority["selected_artifact"], None)
                    self.assertEqual(authority["native_api_proof"]["check_name"], "-")
                    self.assertEqual(authority["admitted"][api.kind + "_phase"], api.phase)
                    self.assertEqual(authority["admitted"][api.kind + "_family"], api.kind)
                    self.assertEqual(authority["admitted"][api.kind + "_packet"], "0")

    def test_hostless_requires_independent_inventory_and_preserves_unknown_original_policy(self):
        for utility in (False, True):
            with self.subTest(utility=utility):
                api = terminal_prerequisite_api(utility, no_executor=True)
                authority = self.review(api, prefix=[])
                self.assert_authority(api, authority, "hostless")
                self.assertEqual(authority["admitted"][api.kind + "_policy_revision"], "-")
                self.assertEqual(authority["admitted"][api.kind + "_historical_context_revision"], ACQUISITION_POLICY)
                # A validated terminal is not accepted by the complete-data binder.
                with mock.patch.object(subprocess, "run", wraps=REAL_RUN) as process:
                    with self.assertRaises(ValueError):
                        authorize.bind_historical_original_transport(authority, authority["raw"], api.kind)
                    process.assert_not_called()
                changed = terminal_prerequisite_api(utility, no_executor=True)
                self.reject_before_native(changed, context="-")
                changed = terminal_prerequisite_api(utility, no_executor=True)
                changed.executor_inventory.append(copy.deepcopy(changed.originals()[0]))
                self.reject_before_native(changed)
                changed = terminal_prerequisite_api(utility, no_executor=True)
                changed.records[f"/compare/{ACQUISITION_POLICY}...main"] = {"status": "behind"}
                self.reject_before_native(changed)

    def test_actual_nullable_job_timestamps_remain_unavailable_and_equal_seconds_are_allowed(self):
        for utility in (False, True):
            for times in ((None, None), (None, "2026-10-09T00:01:03Z"),
                          ("2026-10-09T00:01:02Z", None),
                          ("2026-10-09T00:01:03Z", "2026-10-09T00:01:03Z")):
                with self.subTest(utility=utility, timestamps=times):
                    api = terminal_prerequisite_api(utility, physical=True, timestamps=times)
                    authority = self.review(api, prefix=[])
                    self.assert_authority(api, authority, "cancelled")
                    observed = json.loads(authority["terminal_api_envelope"])["api_observations"][api.terminal_jobs_path]["jobs"][0]
                    self.assertEqual((observed["started_at"], observed["completed_at"]), times)
                    self.assertEqual(authority["terminal_proof"]["physical_job_started_at"], times[0] or "-")
                    self.assertEqual(authority["terminal_proof"]["physical_job_completed_at"], times[1] or "-")

    def test_terminal_job_owner_attempt_head_state_and_complete_counts_are_required(self):
        for utility in (False, True):
            edits = {"id": True, "run_id": 99999, "run_attempt": 2, "head_sha": HARNESS,
                     "status": "queued", "conclusion": None, "started_at": "not-a-time"}
            for field, value in edits.items():
                with self.subTest(utility=utility, field=field):
                    api = terminal_prerequisite_api(utility, physical=True)
                    api.records[api.terminal_jobs_path]["jobs"][0][field] = value
                    self.reject_before_native(api)
            for member in ("checks", "jobs"):
                for count in (True, "1", -1, 2):
                    with self.subTest(utility=utility, member=member, count=count):
                        api = terminal_prerequisite_api(utility, physical=True)
                        path = api.terminal_check_path if member == "checks" else api.terminal_jobs_path
                        api.records[path]["total_count"] = count
                        self.reject_before_native(api)
            api = terminal_prerequisite_api(utility, physical=True,
                timestamps=("2026-10-09T00:01:03Z", "2026-10-09T00:01:02Z"))
            self.reject_before_native(api)
            api = terminal_prerequisite_api(utility, physical=True)
            api.records[api.terminal_jobs_path]["jobs"].append(copy.deepcopy(api.records[api.terminal_jobs_path]["jobs"][0]))
            api.records[api.terminal_jobs_path]["total_count"] = 2
            self.reject_before_native(api)

    def test_terminal_original_and_inventory_executor_reruns_and_ambiguity_are_refused(self):
        for utility in (False, True):
            for mutation in ("latest_request", "latest_executor", "original_attempt_type",
                             "duplicate_executor", "missing_executor", "foreign_executor", "unknown_title"):
                with self.subTest(utility=utility, mutation=mutation):
                    api = terminal_prerequisite_api(utility)
                    if mutation == "latest_request":
                        api.records[f"/actions/runs/{api.current}"]["run_attempt"] = 2
                    elif mutation == "latest_executor":
                        api.records[f"/actions/runs/{api.executor}"]["run_attempt"] = 2
                    elif mutation == "original_attempt_type":
                        api.records[f"/actions/runs/{api.executor}/attempts/1"]["run_attempt"] = True
                    elif mutation == "duplicate_executor":
                        api.executor_inventory.append(copy.deepcopy(api.executor_inventory[0]))
                    elif mutation == "missing_executor":
                        api.executor_inventory = []
                    elif mutation == "foreign_executor":
                        api.executor_inventory[0]["display_title"] = f"9700X request {api.current}.1 head {ADVANCED_HEAD}"
                    else:
                        api.executor_inventory[0]["display_title"] = None
                    self.reject_before_native(api)

    def test_partial_or_expired_expected_artifact_metadata_is_retained_without_zip_or_science(self):
        for utility in (False, True):
            for expired, size in ((False, 0), (True, 32)):
                with self.subTest(utility=utility, expired=expired, size=size):
                    api = terminal_prerequisite_api(utility, conclusion="failure")
                    artifact = {"id": 87654, "name": "buster-9700x-" + api.kind + "-" + ACQUISITION_HEAD + "-1",
                                "expired": expired, "size_in_bytes": size,
                                "workflow_run": {"id": api.executor, "head_sha": ACQUISITION_POLICY}}
                    api.records[api.terminal_artifact_path] = {"total_count": 1, "artifacts": [artifact]}
                    authority = self.review(api, prefix=[])
                    self.assert_authority(api, authority, "failed")
                    self.assertEqual(authority["selected_artifact"], artifact)
                    self.assertEqual(authority["terminal_artifact_inventory"], [artifact])
                    self.assertFalse(any("/zip" in path for path in api.calls))
                    envelope = json.loads(authority["terminal_api_envelope"])
                    self.assertEqual(envelope["artifact_inventory_selection"]["matching_ids"], [artifact["id"]])


if __name__ == "__main__":
    unittest.main()
