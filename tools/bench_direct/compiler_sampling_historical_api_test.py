#!/usr/bin/env python3
"""Historical sampling API replay fixtures; no request, admission or host work.

The fixture API records model GitHub's original attempts and current CLOSED,
advanced pull. The production collector and fixed native data validator consume
their exact committed records; the native bridge is observed, not replaced.
Run on hosted CI after the historical C entry has been integrated.
"""

from __future__ import annotations

import base64
import copy
import hashlib
import json
from pathlib import Path
import sys
import unittest
from unittest import mock
from urllib.parse import parse_qs, urlsplit

sys.path.insert(0, str(Path(__file__).resolve().parent))
import authorize  # noqa: E402

REPOSITORY = "buster14a/buster"
OWNER = {"login": "davidgmbb", "id": 39247043}
BASE, TREE, CONTEXT = "a" * 40, "b" * 40, "c" * 40
HARNESS = TREE
ACQUISITION_REVISION, PILOT_REVISION = BASE, "d" * 40
ACQUISITION_POLICY, PILOT_POLICY = "e" * 40, "f" * 40
ACQUISITION_HEAD, PILOT_HEAD, ADVANCED_HEAD = "1" * 40, "2" * 40, "9" * 40
FIRST_PARENT, SECOND_PARENT = "3" * 40, "4" * 40
PROTOCOL = "a" * 64
SINCE = "2026-10-09T00:00:00Z"
NATIVE_REVIEW = authorize.sampling_review_native
API_NAMES = (
    "schema", "repository", "policy_revision", "policy_main_relation",
    "request_run_id", "request_run_attempt", "request_latest_attempt", "request_workflow",
    "request_event", "request_status", "request_conclusion", "request_head", "source_commit",
    "first_parent", "second_parent", "compare_parent_0", "compare_head_0", "compare_parent_1",
    "compare_head_1", "executor_run_id", "executor_run_attempt", "executor_latest_attempt",
    "executor_workflow", "executor_event", "executor_branch", "executor_head", "executor_title",
    "executor_status", "executor_conclusion", "executor_actor_login", "executor_actor_id",
    "executor_triggering_login", "executor_triggering_id", "pull_number", "associated_pull_number",
    "associated_commit", "pull_state", "pull_current_head", "allowlist_sha256", "facts_sha256",
    "history_sha256", "freeze_sha256", "parent_freeze_sha256", "acquisition_sha256", "check_name",
    "check_app_id", "check_head", "check_external_id", "check_status", "check_conclusion", "check_title",
)


def tsv(fields: dict[str, str]) -> str:
    return "".join(f"{name}\t{value}\n" for name, value in fields.items())


def digest(text: str) -> str:
    return hashlib.sha256(text.encode("utf-8")).hexdigest()


def acquisition_plan() -> str:
    # Native fixture 6f651: frozen workload context is distinct from the fresh
    # marker head; BASE/G equals baseline_revision and all source arms differ.
    return tsv({
        "schema": "buster-main-sampling-acquisition-v1", "phase": "acquire",
        "base": BASE, "base_tree": TREE, "request_head": CONTEXT,
        "trusted_revision": HARNESS, "baseline_revision": BASE,
        "ab1_revision": TREE, "ab2_revision": CONTEXT, "protocol_sha256": PROTOCOL,
        "source_root": "/srv/buster/source", "store_root": "/srv/buster/evidence",
        "closure_policy": "snapshot-v1", "toolchain_policy": "clang-release-tests-off-native-v1",
        "measurement": "false", "physical_budget_seconds": "1800",
    })


def pilot_freeze(plan: str) -> str:
    return tsv({
        "schema": "buster-main-sampling-freeze-v1", "phase": "pilot",
        "campaign_parent": digest(plan), "campaign_parent_revision": ACQUISITION_REVISION,
        "base": BASE, "base_tree": TREE, "request_head": CONTEXT, "trusted_revision": HARNESS,
        "baseline_revision": BASE, "aa_candidate_revision": BASE,
        "ab1_revision": TREE, "ab2_revision": CONTEXT, "protocol_sha256": PROTOCOL,
        "lab_sha256": PROTOCOL, "python_sha256": PROTOCOL, "driver_sha256": PROTOCOL,
        "closure_sha256": PROTOCOL, "prepared_sha256": PROTOCOL, "baseline_sha256": PROTOCOL,
        "aa_candidate_sha256": PROTOCOL, "ab1_candidate_sha256": "b" * 64,
        "ab2_candidate_sha256": "c" * 64, "baseline_bytes": "10000000",
        "ab1_candidate_bytes": "10001000", "ab2_candidate_bytes": "10002000",
        "candidate_pairs": "0", "selected_candidate": "exploratory",
        "calibration_ab1_low_percent": "-", "calibration_ab1_high_percent": "-",
        "calibration_ab2_low_percent": "-", "calibration_ab2_high_percent": "-",
    })


def allowlist(phase: str, revision: str, frozen: str, parent: str = "") -> str:
    return tsv({
        "schema": "buster-main-sampling-admission-v1", "state": phase,
        "freeze_revision": revision, "freeze_sha256": digest(frozen),
        "campaign_parent": digest(parent) if parent else "-",
        "parent_freeze_revision": ACQUISITION_REVISION if parent else "-",
        "protocol_sha256": PROTOCOL, "history_since": SINCE,
        "repository": REPOSITORY, "owner_login": OWNER["login"], "owner_id": str(OWNER["id"]),
    })


class OriginalApi:
    """Complete API records, with strict endpoint inventory and immutable refs."""

    def __init__(self, pilot: bool = False, merge: bool = False):
        self.calls = []
        self.records = {}
        self.plan = acquisition_plan()
        self.freeze = pilot_freeze(self.plan)
        self.contents = {
            (authorize.SAMPLING_FREEZE, ACQUISITION_REVISION): self.plan,
            (authorize.SAMPLING_FREEZE, PILOT_REVISION): self.freeze,
            (authorize.SAMPLING_ALLOWLIST, ACQUISITION_POLICY):
                allowlist("acquire", ACQUISITION_REVISION, self.plan),
            (authorize.SAMPLING_ALLOWLIST, PILOT_POLICY):
                allowlist("pilot", PILOT_REVISION, self.freeze, self.plan),
        }
        self._add_attempt(10000, 20000, ACQUISITION_HEAD, ACQUISITION_POLICY, "acquire",
                          ACQUISITION_REVISION, self.plan, "2026-10-09T00:01:00Z", merge)
        if pilot:
            self._add_attempt(10010, 20010, PILOT_HEAD, PILOT_POLICY, "pilot",
                              PILOT_REVISION, self.freeze, "2026-10-09T00:02:00Z", merge)
        self.current = 10010 if pilot else 10000
        self.executor = self.current + 10000
        self.inventory = [copy.deepcopy(self.records[f"/actions/runs/{run}"])
                          for run in ((10000, 10010) if pilot else (10000,))]
        self.executor_inventory = [copy.deepcopy(self.records[f"/actions/runs/{run}"])
                                   for run in ((20000, 20010) if pilot else (20000,))]

    def _add_attempt(self, request_id, executor_id, head, policy, phase, revision, frozen, created, merge=False):
        line = f"profile: compiler-main-sampling-{phase}-v1 packet: 0 freeze: {revision}"
        self.contents[(authorize.COMPARE_REQUEST, head)] = line + "\n"
        request = {
            "id": request_id, "run_attempt": 1, "path": authorize.REQUEST_WORKFLOW,
            "event": "pull_request", "status": "completed", "conclusion": "success",
            "head_sha": head, "head_branch": "sampling-request", "display_title": "Compiler request",
            "repository": {"full_name": REPOSITORY}, "head_repository": {"full_name": REPOSITORY},
            "actor": dict(OWNER), "triggering_actor": dict(OWNER), "created_at": created,
            "pull_requests": [{"number": 42}],
        }
        execution = {
            "id": executor_id, "run_attempt": 1, "path": ".github/workflows/9700x-direct-bench.yml",
            "event": "workflow_run", "status": "completed", "conclusion": "success",
            "head_sha": policy, "head_branch": "main",
            "display_title": f"9700X request {request_id}.1 head {head}",
            "repository": {"full_name": REPOSITORY}, "head_repository": {"full_name": REPOSITORY},
            "actor": dict(OWNER), "triggering_actor": dict(OWNER), "created_at": created,
        }
        for row in (request, execution):
            self.records[f"/actions/runs/{row['id']}"] = copy.deepcopy(row)
            self.records[f"/actions/runs/{row['id']}/attempts/1"] = copy.deepcopy(row)
        parents = [FIRST_PARENT, SECOND_PARENT] if merge else [FIRST_PARENT]
        self.records[f"/commits/{head}"] = {"sha": head, "parents": [{"sha": p} for p in parents]}
        for parent in parents:
            # Real GitHub compare shape has commits/total_commits, not head_commit.
            self.records[f"/compare/{parent}...{head}"] = {
                "status": "ahead", "base_commit": {"sha": parent}, "merge_base_commit": {"sha": parent},
                "total_commits": 1, "commits": [{"sha": head}], "files": [{
                    "filename": authorize.COMPARE_REQUEST, "status": "modified",
                    "patch": "@@ -1 +1 @@\n-old request\n+" + line,
                }],
            }
        self.records[f"/commits/{head}/pulls?per_page=100"] = [{
            "number": 42, "state": "closed", "user": dict(OWNER),
            "head": {"sha": ADVANCED_HEAD, "repo": {"full_name": REPOSITORY}},
            "base": {"sha": BASE, "repo": {"full_name": REPOSITORY}},
        }]
        check = {
            "id": executor_id + 100000, "name": authorize.SAMPLING_CHECK, "head_sha": head,
            "app": {"id": 15368},
            "external_id": f"buster-main-sampling-v1:{digest(frozen)}:{phase}:0:{request_id}:{executor_id}:1",
            "status": "completed", "conclusion": "success",
            "output": {"title": "Valid unqualified sampling packet"},
        }
        path = f"/commits/{head}/check-runs?check_name=9700X%20compiler%20sampling%20research&filter=all&per_page=100"
        self.records[path] = {"check_runs": [check]}
        self.records[f"/actions/runs/{executor_id}/attempts/1/jobs?per_page=100"] = {"jobs": [
            {"name": "Sampling qualification packet", "conclusion": "success",
             "started_at": "2026-10-09T00:01:02Z", "completed_at": "2026-10-09T00:01:03Z"},
            {"name": "Validate sampling packet evidence", "conclusion": "success"},
        ]}

    def request(self, path):
        self.calls.append(path)
        if path.startswith("/contents/"):
            parsed = urlsplit(path)
            ref = parse_qs(parsed.query).get("ref")
            key = (parsed.path[len("/contents/"):], ref[0] if ref and len(ref) == 1 else "")
            if key not in self.contents:
                raise AssertionError(f"unexpected content read {path}")
            raw = self.contents[key].encode("utf-8")
            return {"type": "file", "encoding": "base64", "size": len(raw),
                    "content": base64.b64encode(raw).decode("ascii")}
        if path.startswith("/actions/workflows/9700x-direct-request.yml/runs?"):
            query = parse_qs(urlsplit(path).query)
            if query != {"event": ["pull_request"], "created": [">=" + SINCE], "per_page": ["100"], "page": ["1"]}:
                raise AssertionError(f"unexpected incomplete inventory query {path}")
            return {"total_count": len(self.inventory), "workflow_runs": copy.deepcopy(self.inventory)}
        if path.startswith("/actions/workflows/9700x-direct-bench.yml/runs?"):
            query = parse_qs(urlsplit(path).query)
            if query != {"event": ["workflow_run"], "created": [">=" + SINCE], "per_page": ["100"], "page": ["1"]}:
                raise AssertionError(f"unexpected incomplete executor inventory query {path}")
            return {"total_count": getattr(self, "executor_inventory_total", len(self.executor_inventory)),
                    "workflow_runs": copy.deepcopy(self.executor_inventory)}
        if path in self.records:
            return copy.deepcopy(self.records[path])
        if path.startswith("/compare/"):
            left, right = path[len("/compare/"):].split("...")
            if left in (ACQUISITION_POLICY, PILOT_POLICY) and right == "main" or \
                    left in (ACQUISITION_REVISION, PILOT_REVISION, HARNESS) and right in (ACQUISITION_POLICY, PILOT_POLICY):
                return {"status": "ahead", "base_commit": {"sha": left},
                        "merge_base_commit": {"sha": left}, "commits": [{"sha": right}], "total_commits": 1}
        raise AssertionError(f"unexpected API read {path}")

    def originals(self):
        return (copy.deepcopy(self.records[f"/actions/runs/{self.executor}/attempts/1"]),
                copy.deepcopy(self.records[f"/actions/runs/{self.current}/attempts/1"]))

    def pull(self, head=None):
        return self.records[f"/commits/{head or self.originals()[1]['head_sha']}/pulls?per_page=100"][0]

    def check(self, head=None):
        source = head or self.originals()[1]["head_sha"]
        path = f"/commits/{source}/check-runs?check_name=9700X%20compiler%20sampling%20research&filter=all&per_page=100"
        return self.records[path]["check_runs"][0]


class HistoricalSamplingApiTest(unittest.TestCase):
    def assert_transport_then_native(self, records):
        self.assertEqual(set(records), {"allowlist", "request", "facts", "history", "freeze",
                                        "parent", "acquisition", "api"})
        proof = authorize.sampling_review_record(records["api"])
        self.assertEqual(tuple(proof), API_NAMES)
        for field, member in (("allowlist_sha256", "allowlist"), ("facts_sha256", "facts"),
                              ("history_sha256", "history"), ("freeze_sha256", "freeze"),
                              ("acquisition_sha256", "acquisition")):
            self.assertEqual(proof[field], digest(records[member]))
        self.assertEqual(proof["parent_freeze_sha256"], digest(records["parent"]) if records["parent"] else "-")
        facts = authorize.sampling_review_record(records["facts"])
        self.assertEqual(facts["request_head"], proof["request_head"])
        self.assertEqual(facts["trusted_revision"], proof["policy_revision"])
        self.assertEqual(facts["pull_state"], "closed")
        self.assertEqual(proof["pull_current_head"], ADVANCED_HEAD)
        self.assertNotEqual(proof["pull_current_head"], proof["request_head"])
        self.assertEqual(facts["fresh_parent_0"], records["request"].rstrip("\n"))
        self.assertEqual(records["history"].splitlines()[0], "\t".join(authorize.SAMPLING_HISTORY_HEADER))
        self.native_records.append(copy.deepcopy(records))
        return NATIVE_REVIEW(records)

    def review(self, api, prefix=None, public=False):
        self.native_records = []
        execution, request = api.originals()
        with mock.patch.object(authorize, "sampling_review_native", side_effect=self.assert_transport_then_native):
            reader = authorize.review_sampling_authority if public else authorize._review_sampling_attempt
            return reader(api, REPOSITORY, execution, request, prefix)

    def rejects_before_native(self, api, originals=None):
        execution, request = originals if originals else api.originals()
        with mock.patch.object(authorize, "sampling_review_native") as bridge:
            with self.assertRaises(ValueError):
                authorize._review_sampling_attempt(api, REPOSITORY, execution, request)
            bridge.assert_not_called()

    def test_closed_advanced_pull_replays_original_policy_and_frozen_context(self):
        api = OriginalApi()
        authority = self.review(api, [], public=True)
        self.assertTrue(authority["historical_review"])
        self.assertEqual(authority["request"]["head_sha"], ACQUISITION_HEAD)
        self.assertEqual(authority["executor"]["head_sha"], ACQUISITION_POLICY)
        self.assertEqual(authority["facts"]["pull_state"], "closed")
        self.assertEqual(authority["acquisition_plan"]["request_head"], CONTEXT)
        self.assertNotEqual(CONTEXT, ACQUISITION_HEAD)
        self.assertEqual(authority["admitted"]["sampling_trusted_revision"], HARNESS)
        self.assertEqual(authority["admitted"]["sampling_policy_revision"], ACQUISITION_POLICY)
        self.assertEqual(authority["admitted"]["sampling_historical_valid"], "true")
        self.assertEqual(authority["admitted"]["sampling_historical_execution_authority"], "false")
        self.assertEqual(authority["admitted"]["sampling_historical_qualification"], "unqualified")
        self.assertNotIn("sampling_admitted", authority["admitted"])
        self.assertEqual(len(self.native_records), 1)
        self.assertIn(f"/actions/runs/{api.current}/attempts/1", api.calls)
        envelope = json.loads(authority["terminal_api_envelope"])
        self.assertEqual(envelope["native_api_proof"]["pull_state"], "closed")
        self.assertEqual(envelope["native_api_proof"]["pull_current_head"], ADVANCED_HEAD)
        self.assertEqual(authority["terminal_api_sha256"],
                         hashlib.sha256(authority["terminal_api_envelope"]).hexdigest())
        # Existing live OPEN-only admission remains distinct and rejects these
        # real current pull facts; historical review never edits them to open.
        failures, _ = authorize.verify(REPOSITORY, api.current, ACQUISITION_HEAD,
                                       authority["request"], [api.pull()], expected_run_attempt=1)
        self.assertTrue(failures)

    def test_pilot_rebinds_its_separate_original_acquisition_and_policy(self):
        api = OriginalApi(pilot=True)
        authority = self.review(api, public=True)
        original = authority["historical_acquisition"]
        self.assertEqual(len(self.native_records), 2)
        self.assertEqual(authority["admitted"]["sampling_phase"], "pilot")
        self.assertEqual(authority["admitted"]["sampling_family"], "aa")
        self.assertEqual(authority["admitted"]["sampling_reservation_seconds"], "3000")
        self.assertEqual(original["admitted"]["sampling_phase"], "acquire")
        self.assertEqual(original["history"], [])
        self.assertEqual(original["executor"]["head_sha"], ACQUISITION_POLICY)
        self.assertEqual(authority["executor"]["head_sha"], PILOT_POLICY)
        self.assertEqual(original["freeze_bytes"], authority["acquisition_plan_bytes"])
        self.assertEqual(authority["history"][0]["request_run_id"], "10000")
        self.assertEqual(authority["history"][0]["physical_wall_us"], "3000000")
        self.assertEqual(authority["facts"]["request_head"], PILOT_HEAD)
        self.assertEqual(original["facts"]["request_head"], ACQUISITION_HEAD)

    def test_two_actual_parents_require_selector_new_in_both(self):
        api = OriginalApi(merge=True)
        authority = self.review(api, [])
        self.assertEqual(authority["facts"]["parent_count"], "2")
        self.assertEqual(authority["facts"]["fresh_parent_0"], authority["facts"]["fresh_parent_1"])
        for parent in (FIRST_PARENT, SECOND_PARENT):
            changed = OriginalApi(merge=True)
            comparison = changed.records[f"/compare/{parent}...{ACQUISITION_HEAD}"]
            comparison["files"][0]["patch"] = "@@ -1 +1 @@\n unchanged selector"
            self.rejects_before_native(changed)

    def test_exact_original_attempt_api_shape_is_required(self):
        for target in ("request", "executor"):
            for value in (None, True, 1.0, "1", 0, 2):
                with self.subTest(target=target, value=value):
                    api = OriginalApi()
                    originals = api.originals()
                    run = api.current if target == "request" else api.executor
                    api.records[f"/actions/runs/{run}/attempts/1"]["run_attempt"] = value
                    self.rejects_before_native(api, originals)
            for endpoint in ("", "/attempts/1"):
                for value in (True, float(10000 if target == "request" else 20000), "10000", 0):
                    with self.subTest(target=target, endpoint=endpoint, id=value):
                        api = OriginalApi()
                        originals = api.originals()
                        run = api.current if target == "request" else api.executor
                        api.records[f"/actions/runs/{run}{endpoint}"]["id"] = value
                        self.rejects_before_native(api, originals)
            api = OriginalApi()
            originals = api.originals()
            run = api.current if target == "request" else api.executor
            api.records[f"/actions/runs/{run}"]["run_attempt"] = 2
            self.rejects_before_native(api, originals)

    def test_caller_relabel_and_original_workflow_identity_are_refused(self):
        for target, field, value in (
                (0, "head_sha", "8" * 40), (1, "head_sha", "8" * 40),
                (0, "path", "foreign.yml"), (1, "event", "push"),
                (0, "id", 10000), (1, "run_attempt", 2)):
            with self.subTest(target=target, field=field):
                api = OriginalApi()
                originals = list(api.originals())
                originals[target][field] = value
                self.rejects_before_native(api, tuple(originals))

    def test_owner_and_repository_provenance_is_independently_requeried(self):
        for target in ("request", "executor", "pull"):
            for field in (("actor", "triggering_actor") if target != "pull" else ("user",)):
                for owner in ({"login": "davidgmbb", "id": 7},
                              {"login": "foreign", "id": 39247043},
                              {"login": "davidgmbb", "id": True}):
                    with self.subTest(target=target, field=field, owner=owner):
                        api = OriginalApi()
                        originals = api.originals()
                        row = api.pull() if target == "pull" else api.records[
                            f"/actions/runs/{api.current if target == 'request' else api.executor}/attempts/1"]
                        row[field] = owner
                        self.rejects_before_native(api, originals)
        for target, field in (("request", "repository"), ("request", "head_repository"),
                              ("executor", "repository"), ("executor", "head_repository"),
                              ("pull", "head"), ("pull", "base")):
            with self.subTest(target=target, field=field):
                api = OriginalApi()
                originals = api.originals()
                if target == "pull":
                    api.pull()[field]["repo"]["full_name"] = "foreign/buster"
                else:
                    row = api.records[f"/actions/runs/{api.current if target == 'request' else api.executor}/attempts/1"]
                    row[field] = {"full_name": "foreign/buster"}
                self.rejects_before_native(api, originals)

    def test_current_pull_membership_remains_unique_and_owner_bound(self):
        for change in ("missing", "duplicate", "different-number", "ambiguous-snapshot", "foreign-state"):
            with self.subTest(change=change):
                api = OriginalApi()
                originals = api.originals()
                path = f"/commits/{ACQUISITION_HEAD}/pulls?per_page=100"
                if change == "missing":
                    api.records[path] = []
                elif change == "duplicate":
                    api.records[path].append(copy.deepcopy(api.records[path][0]))
                elif change == "different-number":
                    api.pull()["number"] = 43
                elif change == "ambiguous-snapshot":
                    api.records[f"/actions/runs/{api.current}/attempts/1"]["pull_requests"] = [{"number": 42}, {"number": 43}]
                else:
                    api.pull()["state"] = "unknown"
                self.rejects_before_native(api, originals)

    def test_original_policy_and_all_frozen_refs_need_api_ancestry(self):
        for path in (f"/compare/{ACQUISITION_POLICY}...main",
                     f"/compare/{ACQUISITION_REVISION}...{ACQUISITION_POLICY}",
                     f"/compare/{HARNESS}...{ACQUISITION_POLICY}"):
            for value in ("behind", "diverged", None):
                with self.subTest(path=path, relation=value):
                    api = OriginalApi()
                    api.records[path] = {"status": value}
                    self.rejects_before_native(api)
        api = OriginalApi(pilot=True)
        api.records[f"/compare/{ACQUISITION_REVISION}...{PILOT_POLICY}"] = {"status": "behind"}
        self.rejects_before_native(api)

    def test_actual_compare_head_and_complete_commit_population_are_required(self):
        for mutation in ("last-head", "count", "empty", "capped", "count-type", "optional-head"):
            with self.subTest(mutation=mutation):
                api = OriginalApi()
                compared = api.records[f"/compare/{FIRST_PARENT}...{ACQUISITION_HEAD}"]
                if mutation == "last-head":
                    compared["commits"][-1]["sha"] = ADVANCED_HEAD
                elif mutation == "count":
                    compared["total_commits"] = 2
                elif mutation == "empty":
                    compared["commits"], compared["total_commits"] = [], 0
                elif mutation == "capped":
                    compared["commits"] = [{"sha": ACQUISITION_HEAD}] * 251
                    compared["total_commits"] = 251
                elif mutation == "count-type":
                    compared["total_commits"] = True
                else:
                    compared["head_commit"] = {"sha": ADVANCED_HEAD}
                self.rejects_before_native(api)

    def test_committed_policy_freeze_and_retained_check_cannot_self_grant(self):
        for change in ("policy-freeze-hash", "cross-phase-freeze", "check-title", "check-conclusion", "check-status"):
            with self.subTest(change=change):
                api = OriginalApi()
                if change == "policy-freeze-hash":
                    config = authorize.sampling_review_record(api.contents[(authorize.SAMPLING_ALLOWLIST, ACQUISITION_POLICY)])
                    config["freeze_sha256"] = "0" * 64
                    api.contents[(authorize.SAMPLING_ALLOWLIST, ACQUISITION_POLICY)] = tsv(config)
                elif change == "cross-phase-freeze":
                    api.contents[(authorize.SAMPLING_FREEZE, ACQUISITION_REVISION)] = api.freeze
                elif change == "check-title":
                    api.check()["output"]["title"] = "Qualified"
                elif change == "check-conclusion":
                    api.check()["conclusion"] = "neutral"
                else:
                    api.check()["status"] = "queued"
                with self.assertRaises(ValueError):
                    self.review(api, [])
        for field, value in (("app", {"id": 7}), ("head_sha", ADVANCED_HEAD),
                              ("external_id", "buster-main-sampling-v1:" + "0" * 64 + ":acquire:0:10000:20000:1")):
            with self.subTest(field=field):
                api = OriginalApi()
                api.check()[field] = value
                self.rejects_before_native(api)

    def test_prefix_inventory_is_original_complete_and_strictly_earlier(self):
        api = OriginalApi(pilot=True)
        prefix = self.review(api)["history"]
        expected_rows = [[row[name] for name in authorize.SAMPLING_HISTORY_HEADER] for row in prefix]
        authority = self.review(OriginalApi(pilot=True), expected_rows)
        self.assertEqual(authority["history"], prefix)
        # A caller supplied prefix can neither omit an earlier failed attempt
        # nor silently reorder/replace the API-selected original acquisition.
        for changed in ([], [expected_rows[0] + ["extra"]], [list(expected_rows[0])]):
            if changed:
                changed[0][2] = "9999"
            with self.subTest(prefix=changed), self.assertRaises(ValueError):
                self.review(OriginalApi(pilot=True), changed)
        for kind in ("request-rerun", "executor-rerun", "failed", "cancelled", "missing", "later-id"):
            with self.subTest(kind=kind):
                changed = OriginalApi(pilot=True)
                if kind == "request-rerun":
                    changed.inventory[0]["run_attempt"] = 2
                elif kind == "executor-rerun":
                    changed.records["/actions/runs/20000"]["run_attempt"] = 2
                elif kind in ("failed", "cancelled"):
                    changed.records["/actions/runs/20000"]["conclusion"] = kind if kind == "cancelled" else "failure"
                    changed.records["/actions/runs/20000/attempts/1"]["conclusion"] = kind if kind == "cancelled" else "failure"
                elif kind == "missing":
                    changed.inventory = changed.inventory[1:]
                else:
                    # Chronologically earlier timestamp with a later immutable
                    # request ID is rejected by the native prefix check.
                    old = changed.records.pop("/actions/runs/10000/attempts/1")
                    old["id"] = 30000
                    changed.records["/actions/runs/30000/attempts/1"] = old
                    latest = changed.records.pop("/actions/runs/10000")
                    latest["id"] = 30000
                    changed.records["/actions/runs/30000"] = latest
                    changed.inventory[0] = copy.deepcopy(latest)
                    execution = changed.records["/actions/runs/20000/attempts/1"]
                    execution["display_title"] = f"9700X request 30000.1 head {ACQUISITION_HEAD}"
                    changed.records["/actions/runs/20000"]["display_title"] = execution["display_title"]
                    changed.executor_inventory[0]["display_title"] = execution["display_title"]
                    changed.check(ACQUISITION_HEAD)["external_id"] = (
                        f"buster-main-sampling-v1:{digest(changed.plan)}:acquire:0:30000:20000:1")
                with self.assertRaises(ValueError):
                    self.review(changed)

    def test_later_request_is_not_in_the_original_prefix(self):
        api = OriginalApi(pilot=True)
        api._add_attempt(10020, 20020, "5" * 40, PILOT_POLICY, "pilot", PILOT_REVISION,
                         api.freeze, "2026-10-09T00:03:00Z")
        api.inventory.append(copy.deepcopy(api.records["/actions/runs/10020"]))
        result = self.review(api)
        self.assertEqual([row["request_run_id"] for row in result["history"]], ["10000"])
        self.assertNotIn("/commits/" + "5" * 40, api.calls)
        self.assertNotIn("/actions/runs/10020/attempts/1", api.calls)

    def test_equal_timestamp_uses_original_request_id_to_bound_prefix(self):
        api = OriginalApi(pilot=True)
        for endpoint in ("/actions/runs/10000", "/actions/runs/10000/attempts/1"):
            api.records[endpoint]["created_at"] = "2026-10-09T00:02:00Z"
        api.inventory[0]["created_at"] = "2026-10-09T00:02:00Z"
        api._add_attempt(10020, 20020, "5" * 40, PILOT_POLICY, "pilot", PILOT_REVISION,
                         api.freeze, "2026-10-09T00:02:00Z")
        api.inventory.append(copy.deepcopy(api.records["/actions/runs/10020"]))
        result = self.review(api)
        self.assertEqual([row["request_run_id"] for row in result["history"]], ["10000"])

    def test_prefix_creation_time_cannot_be_rewritten_between_api_records(self):
        for target in ("listing", "original", "malformed"):
            with self.subTest(target=target):
                api = OriginalApi(pilot=True)
                if target == "listing":
                    api.inventory[0]["created_at"] = "2026-10-09T00:00:30Z"
                elif target == "original":
                    api.records["/actions/runs/10000/attempts/1"]["created_at"] = "2026-10-09T00:00:30Z"
                else:
                    api.inventory[0]["created_at"] = "2026-13-09T00:01:00Z"
                self.rejects_before_native(api)

    def test_prefix_original_owner_attempt_and_compare_head_are_replayed(self):
        for kind in ("owner", "attempt", "compare-head"):
            with self.subTest(kind=kind):
                api = OriginalApi(pilot=True)
                if kind == "owner":
                    api.records["/actions/runs/10000/attempts/1"]["actor"] = {"login": "davidgmbb", "id": 7}
                elif kind == "attempt":
                    api.records["/actions/runs/10000/attempts/1"]["run_attempt"] = True
                else:
                    api.records[f"/compare/{FIRST_PARENT}...{ACQUISITION_HEAD}"]["commits"][-1]["sha"] = ADVANCED_HEAD
                with self.assertRaises(ValueError):
                    self.review(api, public=True)

    def prefix_history(self, api):
        return authorize.sampling_attempt_history(
            REPOSITORY, "", str(api.current), SINCE, PILOT_REVISION, digest(api.freeze),
            ACQUISITION_REVISION, digest(api.plan), api=api, historical=True,
            before_created=api.originals()[1]["created_at"])

    def test_prequeue_cancelled_executor_is_retained_and_continuation_is_refused(self):
        api = OriginalApi(pilot=True)
        check_path = (f"/commits/{ACQUISITION_HEAD}/check-runs?"
                      "check_name=9700X%20compiler%20sampling%20research&filter=all&per_page=100")
        api.records[check_path] = {"check_runs": []}
        for endpoint in ("/actions/runs/20000", "/actions/runs/20000/attempts/1"):
            api.records[endpoint]["conclusion"] = "cancelled"
        api.executor_inventory[0]["conclusion"] = "cancelled"
        api.records["/actions/runs/20000/attempts/1/jobs?per_page=100"] = {"jobs": []}
        rows = self.prefix_history(api)
        self.assertEqual(len(rows), 1)
        row = dict(zip(authorize.SAMPLING_HISTORY_HEADER, rows[0]))
        self.assertEqual(row["request_run_id"], "10000")
        self.assertEqual(row["executor_run_id"], "20000")
        self.assertEqual(row["executor_run_attempt"], "1")
        self.assertEqual(row["state"], "cancelled")
        self.assertEqual(row["physical_wall_us"], "-")
        self.assertIn("/actions/runs/20000/attempts/1", api.calls)
        with self.assertRaises(ValueError):
            self.review(api)

    def test_hostless_success_is_preserved_and_never_becomes_unattempted(self):
        api = OriginalApi(pilot=True)
        api.executor_inventory = api.executor_inventory[1:]
        check_path = (f"/commits/{ACQUISITION_HEAD}/check-runs?"
                      "check_name=9700X%20compiler%20sampling%20research&filter=all&per_page=100")
        api.records[check_path] = {"check_runs": []}
        rows = self.prefix_history(api)
        self.assertEqual(len(rows), 1)
        row = dict(zip(authorize.SAMPLING_HISTORY_HEADER, rows[0]))
        self.assertEqual(row["state"], "hostless")
        self.assertEqual(row["executor_run_id"], "-")
        self.assertEqual(row["executor_run_attempt"], "-")
        self.assertEqual(row["physical_wall_us"], "-")
        with self.assertRaises(ValueError):
            self.review(api)

    def test_independent_executor_inventory_refuses_duplicates_and_counterfeit_provenance(self):
        for kind in ("duplicate", "foreign-owner", "renamed-workflow", "counterfeit-title", "rerun"):
            with self.subTest(kind=kind):
                api = OriginalApi(pilot=True)
                if kind == "duplicate":
                    extra = copy.deepcopy(api.executor_inventory[0])
                    extra["id"] = 20001
                    api.executor_inventory.append(extra)
                    api.records["/actions/runs/20001"] = copy.deepcopy(extra)
                    api.records["/actions/runs/20001/attempts/1"] = copy.deepcopy(extra)
                elif kind == "foreign-owner":
                    api.records["/actions/runs/20000/attempts/1"]["actor"] = {"login": "davidgmbb", "id": 7}
                elif kind == "renamed-workflow":
                    api.records["/actions/runs/20000/attempts/1"]["path"] = ".github/workflows/foreign.yml"
                elif kind == "counterfeit-title":
                    title = f"9700X request 10000.1 head {ADVANCED_HEAD}"
                    api.executor_inventory[0]["display_title"] = title
                    api.records["/actions/runs/20000/attempts/1"]["display_title"] = title
                    api.records["/actions/runs/20000"]["display_title"] = title
                else:
                    api.records["/actions/runs/20000"]["run_attempt"] = 2
                    api.executor_inventory[0]["run_attempt"] = 2
                with self.assertRaises(ValueError):
                    self.prefix_history(api)

    def test_current_original_executor_must_be_a_unique_independent_inventory_member(self):
        for kind in ("absent", "duplicate"):
            with self.subTest(kind=kind):
                api = OriginalApi()
                if kind == "absent":
                    api.executor_inventory = []
                else:
                    extra = copy.deepcopy(api.executor_inventory[0])
                    extra["id"] = 20001
                    api.executor_inventory.append(extra)
                    api.records["/actions/runs/20001"] = copy.deepcopy(extra)
                    api.records["/actions/runs/20001/attempts/1"] = copy.deepcopy(extra)
                self.rejects_before_native(api)

    def test_executor_for_earlier_request_may_be_created_after_current_request(self):
        api = OriginalApi(pilot=True)
        for endpoint in ("/actions/runs/20000", "/actions/runs/20000/attempts/1"):
            api.records[endpoint]["created_at"] = "2026-10-09T00:03:00Z"
        api.executor_inventory[0]["created_at"] = "2026-10-09T00:03:00Z"
        rows = self.prefix_history(api)
        self.assertEqual(rows[0][2:6], ["10000", "1", "20000", "1"])
        self.assertEqual(rows[0][6], "complete")

    def test_executor_inventory_total_and_id_population_are_complete_and_typed(self):
        for kind in ("total-bool", "total-large", "total-short", "duplicate-id", "typed-id"):
            with self.subTest(kind=kind):
                api = OriginalApi()
                if kind == "total-bool":
                    api.executor_inventory_total = True
                elif kind == "total-large":
                    api.executor_inventory_total = 1001
                elif kind == "total-short":
                    api.executor_inventory_total = 2
                elif kind == "duplicate-id":
                    api.executor_inventory.append(copy.deepcopy(api.executor_inventory[0]))
                else:
                    api.executor_inventory[0]["id"] = float(api.executor)
                self.rejects_before_native(api)

    def test_prequeue_failed_executor_keeps_original_policy_and_unknown_wall(self):
        api = OriginalApi(pilot=True)
        check_path = (f"/commits/{ACQUISITION_HEAD}/check-runs?"
                      "check_name=9700X%20compiler%20sampling%20research&filter=all&per_page=100")
        api.records[check_path] = {"check_runs": []}
        for endpoint in ("/actions/runs/20000", "/actions/runs/20000/attempts/1"):
            api.records[endpoint]["conclusion"] = "failure"
        api.executor_inventory[0]["conclusion"] = "failure"
        api.records["/actions/runs/20000/attempts/1/jobs?per_page=100"] = {"jobs": []}
        rows = self.prefix_history(api)
        self.assertEqual(rows[0][2:8], ["10000", "1", "20000", "1", "failed", "-"])
        self.assertEqual(api.records["/actions/runs/20000/attempts/1"]["head_sha"], ACQUISITION_POLICY)
        with self.assertRaises(ValueError):
            self.review(api)

    def test_original_acquisition_cannot_borrow_the_later_policy(self):
        api = OriginalApi(pilot=True)
        api.records["/actions/runs/20000/attempts/1"]["head_sha"] = PILOT_POLICY
        api.records["/actions/runs/20000"]["head_sha"] = PILOT_POLICY
        # The pilot's current policy text admits pilot, so original acquire
        # replay fails even though all raw bytes/digests are internally valid.
        with self.assertRaises(ValueError):
            self.review(api, public=True)


if __name__ == "__main__":
    unittest.main()
