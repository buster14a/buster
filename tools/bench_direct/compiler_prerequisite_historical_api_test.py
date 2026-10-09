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
        self.records[self.check_path()] = {"check_runs": [check]}
        host_name = "Compiler closure utility" if utility else "Compiler preparation qualification"
        self.records[f"/actions/runs/{self.executor}/attempts/1/jobs?per_page=100"] = {"jobs": [{
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
        self.assertEqual(records["history"], "\t".join(authorize.SAMPLING_HISTORY_HEADER) + "\n")
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
                self.assertEqual(process.call_count, 1)
                command = process.call_args.args[0]
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


if __name__ == "__main__":
    unittest.main()
