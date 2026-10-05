#!/usr/bin/env python3
"""Synthetic parser controls only; these fixtures are not CI qualification runs."""
from collections import Counter
import copy
from datetime import datetime, timedelta, timezone
import hashlib
import json
from pathlib import Path
import tempfile
import unittest
from unittest import mock

import ci_checks_qualification as qualification
import ci_matrix_phases as phases
import ci_matrix_phases_test as phase_tests
import github_ci_time as github

HOST_RECORD = "CI_UNIT_HOST_V1 architecture=x86_64 feature_source=cpuid-xcr0 feature_word_count=4 word0=1 word1=2 word2=3 word3=4 simd_512_base=0 simd_512=0"
HOST_PROFILE = {"schema": "buster-native-host-profile-v1", "architecture": "x86_64", "feature_source": "cpuid-xcr0",
                "feature_words": [1, 2, 3, 4], "simd_512_base": False, "simd_512": False}


def reference(root, name, value):
    path = root / name
    path.write_text(json.dumps(value) + "\n", encoding="utf-8")
    return {"path": name, "sha256": hashlib.sha256(path.read_bytes()).hexdigest()}


def full_policy(platform):
    """Full native row order, including exclusions, checked against real anchors."""
    identity = dict(suite="desktop", platform=platform, architecture="x86_64")
    rows = []

    def add(compiler, config, sanitize=False, fuzz=False, exclusion=""):
        row = dict(compiler=compiler, configuration=config, optimize=config == "Release", sanitize=sanitize, fuzz=fuzz,
                   unity=compiler == "clang" and config == "Release" and not sanitize,
                   execution="none" if exclusion else ("runtime" if compiler == "clang" else "compile-link"),
                   state="excluded" if exclusion else "required", exclusion=exclusion)
        row["id"] = qualification.coverage_tools._coverage_row_id(identity, row)
        row["owner_shard"] = qualification.coverage_tools._coverage_row_owner(row)
        rows.append(row)

    for compiler in (("cl", "clang", "gcc", "zig") if platform == "windows" else ("clang", "gcc", "zig")):
        if compiler != "clang":
            add(compiler, "Release", exclusion="non-clang-portability-debug-only")
            sanitizer_reason = "msvc-sanitizer-not-in-combination-matrix" if compiler == "cl" else "non-clang-sanitizer-not-in-combination-matrix"
            fuzz_reason = "msvc-fuzz-not-in-combination-matrix" if compiler == "cl" else "non-clang-fuzz-not-in-combination-matrix"
            for config in ("Debug", "Release"):
                add(compiler, config, True, False, sanitizer_reason)
                add(compiler, config, True, True, fuzz_reason)
                add(compiler, config, False, True, fuzz_reason)
            add(compiler, "Debug")
        else:
            add(compiler, "Debug", exclusion="sanitized-debug-covers-unsanitized-debug")
            if platform == "macos":
                reason = "apple-fuzzer-runtime-unavailable"
                add(compiler, "Release", False, True, reason)
                add(compiler, "Debug", True, True, reason)
                add(compiler, "Release", True, True, reason)
            add(compiler, "Release", False, platform != "macos")
            add(compiler, "Debug", True, platform != "macos")
            add(compiler, "Release", True, False)
    return rows


class QualificationTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)

    def run_fixture(self, variant="combined-overlap", reuse=False, cohort_name=qualification.LEGACY_COHORT):
        names = list(github.combination_jobs("split" if variant == "split-overlap" else "combined"))
        if reuse:
            names.append(github.MAIN_REUSE_JOB)
        origin = datetime(2026, 10, 1, tzinfo=timezone.utc)
        stamp = lambda seconds: (origin + timedelta(seconds=seconds)).isoformat()
        jobs = [dict(id=i + 1, name=name, status="completed", conclusion="success", run_attempt=1, labels=["synthetic"],
                     created_at=stamp(1), started_at=stamp(10), completed_at=stamp(110), steps=[])
                for i, name in enumerate(names)]
        prefix = "codex/2120-evidence-v2-" if cohort_name == qualification.PROSPECTIVE_COHORT else "codex/ci-checks-"
        return dict(id=123, path=".github/workflows/ci.yml", event="workflow_dispatch", head_branch=prefix + variant,
                    status="completed", conclusion="success", run_attempt=1, head_sha="a" * 40, workflow_blob_sha="b" * 40,
                    created_at=stamp(0), jobs=jobs)

    def measured(self):
        return {"job_seconds": {"Windows x86-64 checks": 100}, "elapsed_seconds": 110, "runner_seconds": 2100}

    def conditions_fixture(self, run):
        jobs = {}
        for job in run["jobs"]:
            if qualification.skipped_reuse(job):
                continue
            name = qualification.role(job["name"])
            tools, caches = qualification.condition_keys(name)
            entry = dict(job_id=job["id"], image_os="fixture", image_version="1", runner="synthetic",
                         toolchains={key: "synthetic1" for key in tools}, caches={key: "not-used" for key in caches})
            if "BUSTER_CI_ZIG_CACHE_HIT" in caches:
                entry["caches"]["BUSTER_CI_ZIG_CACHE_HIT"] = "false"
            if name == "iOS AArch64":
                comparable = dict(runtime="com.apple.CoreSimulator.SimRuntime.iOS-26-5",
                                  device_type="com.apple.CoreSimulator.SimDeviceType.iPhone-17-Pro")
                receipt = dict(schema="buster-ci-ios-simulator-selection-v1", status="observed", invalid_bindings=[], reason="none",
                               repository="buster14a/buster", source_revision=run["head_sha"], run_id=str(run["id"]),
                               run_attempt=str(run["run_attempt"]), job="mobile",
                               selection=dict(kind="name-reuse", udid="12345678-1234-1234-1234-123456789ABC", **comparable))
                entry["simulator_selection"] = reference(self.root, "selected-simulator-" + str(job["id"]) + ".json", receipt)
                entry["toolchains"]["ios_simulator"] = comparable
            if name == "Android x86-64":
                entry["toolchains"].update(android_system_image="system-images;android-35;google_apis;x86_64", android_system_image_revision="9")
                entry["caches"]["android_sdk_package_state_before"] = {
                    "emulator": "failure", "platforms;android-35": "success",
                    "system-images;android-35;google_apis;x86_64": "failure"}
            if name in qualification.SELECTED_TOOL_ROLES:
                job_key, tool, key = qualification.SELECTED_TOOL_ROLES[name]
                comparable = dict(sha256="c" * 64, version="synthetic1\n")
                selection = dict(kind="executable", requested=tool, path="/synthetic/" + tool, resolved_path="/synthetic/" + tool)
                if tool == "ninja":
                    selection.update(kind="cmake-cache", requested=selection["path"], cmake_cache="/synthetic/CMakeCache.txt")
                receipt = dict(schema="buster-ci-selected-tool-v1", repository="buster14a/buster", source_revision=run["head_sha"],
                               run_id=str(run["id"]), run_attempt=str(run["run_attempt"]), job=job_key, tool=tool,
                               status="observed", selection=selection, comparable=comparable,
                               command=[selection["resolved_path"], "--version" if tool == "ninja" else "version"])
                entry["selected_tools"] = {tool: reference(self.root, "selected-tool-" + str(job["id"]) + ".json", receipt)}
                entry["toolchains"][key] = comparable
            jobs[job["name"]] = entry
        return dict(schema="buster-ci-checks-conditions-v1", run_id=run["id"], jobs=jobs)

    def test_exact_inventory_and_optional_reuse_is_counted(self):
        for variant, expected in (("combined-overlap", 21), ("split-overlap", 27)):
            for reuse in (False, True):
                with self.subTest(variant=variant, reuse=reuse), mock.patch.object(github, "measure", return_value=(self.measured(), None)):
                    result = qualification.timing(self.run_fixture(variant, reuse), variant)
                    self.assertEqual(result["job_count"], expected + int(reuse))
                    self.assertEqual(result["runner_seconds"], 100 * (expected + int(reuse)))
                    self.assertEqual(result["elapsed_seconds"], 110)
                    self.assertEqual(result["initial_queue_seconds"], 10)
                    self.assertEqual(set(result["job_queue_seconds"].values()), {9})

    def test_only_optional_first_attempt_skipped_reuse_has_zero_runner_cost(self):
        for variant, expected in (("combined-overlap", 21), ("split-overlap", 27)):
            run = self.run_fixture(variant, reuse=True)
            run["jobs"][-1].update(conclusion="skipped", created_at=None, started_at=None, completed_at=None, labels=[])
            with self.subTest(variant=variant), mock.patch.object(github, "measure", return_value=(self.measured(), None)):
                result = qualification.timing(run, variant)
                self.assertEqual(result["job_count"], expected + 1)
                self.assertEqual(result["runner_seconds"], expected * 100)
                self.assertEqual(result["elapsed_seconds"], 110)
                self.assertEqual(result["skipped_metadata_jobs"], [github.MAIN_REUSE_JOB])
                self.assertNotIn(github.MAIN_REUSE_JOB, result["job_queue_seconds"])
            for change in (dict(run_attempt=2), dict(conclusion="failure"), dict(conclusion="cancelled"), dict(status="in_progress")):
                invalid = copy.deepcopy(run)
                invalid["jobs"][-1].update(change)
                with self.subTest(change=change), mock.patch.object(github, "measure", return_value=(self.measured(), None)), self.assertRaises(ValueError):
                    qualification.timing(invalid, variant)
            run["jobs"][0].update(conclusion="skipped")
            with mock.patch.object(github, "measure", return_value=(self.measured(), None)), self.assertRaises(ValueError):
                qualification.timing(run, variant)

    def test_skipped_reuse_has_no_assigned_runner_conditions(self):
        run = self.run_fixture(reuse=True)
        run["jobs"][-1].update(conclusion="skipped", created_at=None, started_at=None, completed_at=None, labels=[])
        value = self.conditions_fixture(run)
        _, skipped = qualification.conditions(self.root, reference(self.root, "conditions.json", value), run)
        _, absent = qualification.conditions(self.root, reference(self.root, "conditions.json", value), self.run_fixture())
        self.assertEqual(skipped, absent)
        self.assertNotIn(github.MAIN_REUSE_JOB, skipped)
        value["jobs"][github.MAIN_REUSE_JOB] = dict(job_id=22, image_os="fixture", image_version="1", runner="synthetic", toolchains={}, caches={})
        with self.assertRaisesRegex(ValueError, "exact-job"):
            qualification.conditions(self.root, reference(self.root, "conditions.json", value), run)
        del value["jobs"][github.MAIN_REUSE_JOB]
        del value["jobs"][run["jobs"][0]["name"]]
        with self.assertRaisesRegex(ValueError, "exact-job"):
            qualification.conditions(self.root, reference(self.root, "conditions.json", value), run)

    def test_missing_duplicate_failed_or_partial_jobs_never_qualify(self):
        mutations = (lambda r: r["jobs"].pop(), lambda r: r["jobs"].append(r["jobs"][0]),
                     lambda r: r["jobs"][0].update(conclusion="skipped"),
                     lambda r: r["jobs"][0].update(created_at=None),
                     lambda r: r["jobs"][0].update(run_attempt=2),
                     lambda r: r.update(head_branch="main"))
        for mutation in mutations:
            run = self.run_fixture()
            mutation(run)
            with self.subTest(mutation=mutation), mock.patch.object(github, "measure", return_value=(self.measured(), None)):
                with self.assertRaises((ValueError, TypeError)):
                    qualification.timing(run, "combined-overlap")

    def test_native_success_requirements_are_not_replaced_by_timestamps(self):
        with self.assertRaisesRegex(ValueError, "ineligible GitHub attempt"):
            qualification.timing(self.run_fixture(), "combined-overlap")

    def test_named_cohorts_accept_only_their_exact_dispatch_variant_refs(self):
        for cohort_name in qualification.COHORT_BRANCHES:
            for variant in qualification.VARIANTS:
                run = self.run_fixture(variant, cohort_name=cohort_name)
                with self.subTest(cohort=cohort_name, variant=variant), mock.patch.object(github, "measure", return_value=(self.measured(), None)):
                    measured = qualification.timing(run, variant, cohort_name)
                    self.assertEqual(measured["job_count"], 27 if variant == "split-overlap" else 21)
                branches = {branch for mapping in qualification.COHORT_BRANCHES.values() for branch in mapping.values()}
                branches.discard(run["head_branch"])
                branches.update(("main", run["head_branch"] + "-extra", "prefix/" + run["head_branch"], None))
                for branch in branches:
                    with self.subTest(cohort=cohort_name, variant=variant, branch=branch), self.assertRaisesRegex(ValueError, "branch/variant mismatch"):
                        qualification.timing(dict(run, head_branch=branch), variant, cohort_name)
                for event in (None, "push", "pull_request", "merge_group", "schedule"):
                    with self.subTest(cohort=cohort_name, variant=variant, event=event), self.assertRaisesRegex(ValueError, "branch/variant mismatch"):
                        qualification.timing(dict(run, event=event), variant, cohort_name)
                if cohort_name == qualification.PROSPECTIVE_COHORT:
                    with self.assertRaisesRegex(ValueError, "branch/variant mismatch"):
                        qualification.timing(run, variant)
        with self.assertRaisesRegex(ValueError, "unknown qualification cohort"):
            qualification.timing(self.run_fixture(), "combined-overlap", "unknown")

    def test_sample_propagates_cohort_before_loading_native_evidence(self):
        run = self.run_fixture(cohort_name=qualification.PROSPECTIVE_COHORT)
        item = dict(variant="combined-overlap", run=reference(self.root, "run.json", run))
        with mock.patch.object(github, "measure", return_value=(self.measured(), None)), mock.patch.object(qualification, "conditions", side_effect=ValueError("native evidence marker")) as joined:
            with self.assertRaisesRegex(ValueError, "native evidence marker"):
                qualification.sample(self.root, dict(item, conditions={}), qualification.PROSPECTIVE_COHORT)
            self.assertEqual(joined.call_count, 1)
        with mock.patch.object(qualification, "conditions") as joined, self.assertRaisesRegex(ValueError, "branch/variant mismatch"):
            qualification.sample(self.root, item)
        joined.assert_not_called()

    def test_missing_unknown_and_unmatched_conditions_fail(self):
        run = self.run_fixture("split-overlap")
        value = self.conditions_fixture(run)
        good = reference(self.root, "conditions.json", value)
        _, normalized = qualification.conditions(self.root, good, run)
        self.assertEqual(len(normalized), 21)
        for change in (lambda v: v["jobs"].pop("Workflow lint"),
                       lambda v: v["jobs"]["Linux x86-64 portability"].update(image_version="unknown"),
                       lambda v: v["jobs"]["Linux x86-64 portability"].update(image_version="2"),
                       lambda v: v["jobs"]["Workflow lint"].pop("caches")):
            invalid = copy.deepcopy(value)
            change(invalid)
            with self.subTest(change=change), self.assertRaises(ValueError):
                qualification.conditions(self.root, reference(self.root, "invalid.json", invalid), run)

    def test_every_required_role_tool_and_cache_key_is_enforced(self):
        run = self.run_fixture("split-overlap")
        value = self.conditions_fixture(run)
        for job in run["jobs"]:
            for group in ("toolchains", "caches"):
                for key in value["jobs"][job["name"]][group]:
                    for missing in (True, False):
                        invalid = copy.deepcopy(value)
                        if missing:
                            del invalid["jobs"][job["name"]][group][key]
                        else:
                            invalid["jobs"][job["name"]][group][key] = "unknown"
                        with self.subTest(job=job["name"], group=group, key=key, missing=missing), self.assertRaises(ValueError):
                            qualification.conditions(self.root, reference(self.root, "invalid.json", invalid), run)

    def test_desktop_required_tools_preserve_the_actual_supported_architecture_scope(self):
        for shard in ("release", "checks"):
            self.assertEqual(qualification.condition_keys("Windows AArch64 " + shard)[0], {"cl", "clang"})
            self.assertEqual(qualification.condition_keys("Windows x86-64 " + shard)[0], {"cl", "clang", "gcc", "zig"})
            for platform in ("Linux x86-64", "Linux AArch64", "macOS AArch64"):
                self.assertEqual(qualification.condition_keys(platform + " " + shard)[0], {"clang", "gcc", "zig"})
        for shard in github.SPLIT_CHECK_SHARDS:
            self.assertEqual(qualification.condition_keys("Windows x86-64 " + shard), qualification.condition_keys("Windows x86-64 checks"))

    def test_wrong_role_maps_and_empty_required_tools_do_not_pass(self):
        run = self.run_fixture()
        value = self.conditions_fixture(run)
        for name in ("Workflow lint", "Linux x86-64 native", "UEFI firmware boot", "Android x86-64"):
            for tools in ({"fixture": "1"}, {key: {} for key in value["jobs"][name]["toolchains"]}):
                invalid = copy.deepcopy(value)
                invalid["jobs"][name]["toolchains"] = tools
                with self.subTest(name=name, tools=tools), self.assertRaises(ValueError):
                    qualification.conditions(self.root, reference(self.root, "invalid.json", invalid), run)
        with self.assertRaisesRegex(ValueError, "unknown conditions role"):
            qualification.condition_keys("not a workflow job")

    def test_selected_simulator_uuid_is_provenance_not_a_comparable(self):
        run = self.run_fixture()
        value = self.conditions_fixture(run)
        entry = value["jobs"]["iOS AArch64"]
        _, original = qualification.conditions(self.root, reference(self.root, "first-conditions.json", value), run)
        path = self.root / entry["simulator_selection"]["path"]
        receipt = json.loads(path.read_text())
        receipt["selection"]["udid"] = "87654321-4321-4321-4321-CBA987654321"
        entry["simulator_selection"] = reference(self.root, path.name, receipt)
        _, changed = qualification.conditions(self.root, reference(self.root, "second-conditions.json", value), run)
        self.assertEqual(original, changed)
        self.assertNotIn("udid", changed["iOS AArch64"]["toolchains"]["ios_simulator"])

    def test_missing_or_guessed_simulator_observations_never_pass(self):
        run = self.run_fixture()
        value = self.conditions_fixture(run)
        changes = (lambda e: e.pop("simulator_selection"),
                   lambda e: e["toolchains"].update(ios_simulator="synthetic1"),
                   lambda e: e["toolchains"]["ios_simulator"].update(udid="12345678-1234-1234-1234-123456789ABC"),
                   lambda e: e["toolchains"]["ios_simulator"].update(runtime="latest"))
        for change in changes:
            invalid = copy.deepcopy(value)
            change(invalid["jobs"]["iOS AArch64"])
            with self.subTest(change=change), self.assertRaises(ValueError):
                qualification.conditions(self.root, reference(self.root, "invalid-conditions.json", invalid), run)

    def test_simulator_receipt_identity_actual_fields_and_status_are_required(self):
        run = self.run_fixture()
        value = self.conditions_fixture(run)
        entry = value["jobs"]["iOS AArch64"]
        path = self.root / entry["simulator_selection"]["path"]
        original = json.loads(path.read_text())
        changes = ({"source_revision": "d" * 40}, {"repository": "elsewhere/repo"}, {"run_id": "124"},
                   {"run_attempt": "2"}, {"job": "lint"}, {"status": "unknown"}, {"invalid_bindings": ["run_id"]},
                   {"selection": dict(original["selection"], kind="explicit")},
                   {"selection": dict(original["selection"], udid="FAKE-UDID")},
                   {"selection": dict(original["selection"], runtime="latest")},
                   {"selection": dict(original["selection"], device_type=None)},
                   {"selection": {k: v for k, v in original["selection"].items() if k != "device_type"}})
        for change in changes:
            invalid = copy.deepcopy(original)
            invalid.update(change)
            entry["simulator_selection"] = reference(self.root, path.name, invalid)
            with self.subTest(change=change), self.assertRaises(ValueError):
                qualification.conditions(self.root, reference(self.root, "invalid-conditions.json", value), run)

    def test_selected_tool_receipt_identity_and_authority_are_required(self):
        run = self.run_fixture()
        value = self.conditions_fixture(run)
        entry = value["jobs"]["Workflow lint"]
        path = self.root / entry["selected_tools"]["go"]["path"]
        original = json.loads(path.read_text())
        changes = ({"source_revision": "d" * 40}, {"repository": "somewhere/else"}, {"run_id": "124"},
                   {"run_attempt": "2"}, {"job": "mobile"}, {"tool": "ninja"}, {"status": "unknown"},
                   {"command": ["/synthetic/go", "--version"]}, {"comparable": {"sha256": "bad", "version": "1"}},
                   {"selection": dict(original["selection"], kind="cmake-cache")},
                   {"selection": dict(original["selection"], requested=False)},
                   {"selection": dict(original["selection"], requested=123)},
                   {"selection": dict(original["selection"], requested="go\x00")},
                   {"selection": dict(original["selection"], path="/synthetic/go\x00")},
                   {"selection": dict(original["selection"], resolved_path="/synthetic/go\x00")})
        for change in changes:
            invalid = copy.deepcopy(value)
            receipt = dict(original, **change)
            invalid["jobs"]["Workflow lint"]["selected_tools"]["go"] = reference(self.root, "changed-tool.json", receipt)
            with self.subTest(change=change), self.assertRaises(ValueError):
                qualification.conditions(self.root, reference(self.root, "invalid.json", invalid), run)
        for selected in ({}, {"ninja": entry["selected_tools"]["go"]}):
            invalid = copy.deepcopy(value)
            invalid["jobs"]["Workflow lint"]["selected_tools"] = selected
            with self.subTest(selected=selected), self.assertRaises(ValueError):
                qualification.conditions(self.root, reference(self.root, "invalid.json", invalid), run)

    def test_selected_tool_provenance_is_retained_without_comparing_temporary_roots(self):
        run = self.run_fixture()
        value = self.conditions_fixture(run)
        _, original = qualification.conditions(self.root, reference(self.root, "conditions.json", value), run)
        entry = value["jobs"]["UEFI firmware boot"]
        receipt = json.loads((self.root / entry["selected_tools"]["ninja"]["path"]).read_text())
        receipt["selection"]["cmake_cache"] = "/different/job/root/CMakeCache.txt"
        receipt["selection"].update(path="/different/tools/ninja", resolved_path="/different/tools/ninja", requested="/different/tools/ninja")
        receipt["command"][0] = "/different/tools/ninja"
        entry["selected_tools"]["ninja"] = reference(self.root, "different-root.json", receipt)
        _, changed = qualification.conditions(self.root, reference(self.root, "conditions.json", value), run)
        self.assertEqual(original, changed)
        entry["toolchains"]["ninja"]["version"] = "different\n"
        with self.assertRaisesRegex(ValueError, "differs from condition map"):
            qualification.conditions(self.root, reference(self.root, "conditions.json", value), run)

    def test_selected_tool_binary_digest_is_a_string_even_when_all_digits(self):
        run = self.run_fixture()
        value = self.conditions_fixture(run)
        entry = value["jobs"]["Workflow lint"]
        receipt = json.loads((self.root / entry["selected_tools"]["go"]["path"]).read_text())
        receipt["comparable"]["sha256"] = int("1" * 64)
        entry["toolchains"]["go"] = receipt["comparable"]
        entry["selected_tools"]["go"] = reference(self.root, "numeric-digest.json", receipt)
        with self.assertRaisesRegex(ValueError, "bounded selected tool identity"):
            qualification.conditions(self.root, reference(self.root, "invalid.json", value), run)

    def test_malformed_selected_receipt_objects_fail_with_a_pending_input_error(self):
        run = self.run_fixture()
        value = self.conditions_fixture(run)
        ref = value["jobs"]["Workflow lint"]["selected_tools"]["go"]
        original = json.loads((self.root / ref["path"]).read_text())
        for malformed in ([], "record", 1, None):
            for field in (None, "selection", "comparable"):
                receipt = copy.deepcopy(original)
                if field is None:
                    receipt = malformed
                else:
                    receipt[field] = malformed
                invalid = copy.deepcopy(value)
                invalid["jobs"]["Workflow lint"]["selected_tools"]["go"] = reference(self.root, "malformed.json", receipt)
                with self.subTest(field=field, malformed=malformed), self.assertRaises(ValueError):
                    qualification.conditions(self.root, reference(self.root, "invalid.json", invalid), run)
            for name in ("Workflow lint", "Linux x86-64 native", "CI complete"):
                invalid = copy.deepcopy(value)
                invalid["jobs"][name]["selected_tools"] = malformed
                with self.subTest(references=name, malformed=malformed), self.assertRaises(ValueError):
                    qualification.conditions(self.root, reference(self.root, "invalid.json", invalid), run)

    def test_split_roles_keep_full_tool_maps_and_exact_cache_equality(self):
        run = self.run_fixture("split-overlap")
        value = self.conditions_fixture(run)
        for group, key, change in (("toolchains", "gcc", "other compiler"), ("caches", "BUSTER_CI_ZIG_CACHE_HIT", "true")):
            invalid = copy.deepcopy(value)
            invalid["jobs"]["Linux x86-64 portability"][group][key] = change
            with self.subTest(group=group), self.assertRaisesRegex(ValueError, "split jobs have different"):
                qualification.conditions(self.root, reference(self.root, "invalid.json", invalid), run)
        invalid = copy.deepcopy(value)
        del invalid["jobs"]["Linux x86-64 portability"]["toolchains"]["clang"]
        with self.assertRaisesRegex(ValueError, "required role"):
            qualification.conditions(self.root, reference(self.root, "invalid.json", invalid), run)

    def test_android_cache_scope_is_initial_validity_and_revision_not_absence(self):
        run = self.run_fixture()
        value = self.conditions_fixture(run)
        qualification.conditions(self.root, reference(self.root, "conditions.json", value), run)
        for change in ("absent", "unknown"):
            invalid = copy.deepcopy(value)
            invalid["jobs"]["Android x86-64"]["caches"]["android_sdk_package_state_before"]["emulator"] = change
            with self.subTest(change=change), self.assertRaises(ValueError):
                qualification.conditions(self.root, reference(self.root, "invalid.json", invalid), run)
        for revision in ("unknown", True, False, 1.0, 0, "0", "09"):
            invalid = copy.deepcopy(value)
            invalid["jobs"]["Android x86-64"]["toolchains"]["android_system_image_revision"] = revision
            with self.subTest(revision=revision), self.assertRaises(ValueError):
                qualification.conditions(self.root, reference(self.root, "invalid.json", invalid), run)

    def test_digest_binding_and_empty_campaign_stay_pending(self):
        item = reference(self.root, "retained.json", {"sample": 1})
        (self.root / item["path"]).write_text("{}\n")
        with self.assertRaisesRegex(ValueError, "digest mismatch"):
            qualification.record(self.root, item)
        path = self.root / "campaign.json"
        reference(self.root, path.name, dict(schema=qualification.SCHEMA, repository="buster14a/buster", samples=[]))
        report = qualification.qualify(path)
        self.assertEqual(report["status"], "pending")
        self.assertFalse(report["performance_accepted"])

    def observation(self, variant, identity):
        windows, wall = {"combined-overlap": (100, 1000), "combined-all-builds": (90, 990), "split-overlap": (80, 850)}[variant]
        return dict(variant=variant, run_id=identity, head_sha="a" * 40, workflow_blob_sha="b" * 40,
                    conditions={"same": True}, platforms={"same": True}, timing=dict(elapsed_seconds=wall, runner_seconds=105 if variant != "combined-overlap" else 100,
                    job_seconds={"Windows x86-64 checks": windows}, initial_queue_seconds=5, job_queue_seconds={}))

    def campaign(self, cohort=None):
        path = self.root / "campaign.json"
        samples = [dict(variant=variant, synthetic_id=i + 1) for i, variant in enumerate(qualification.VARIANTS * 3)]
        value = dict(schema=qualification.SCHEMA, repository="buster14a/buster", samples=samples)
        if cohort is not None:
            value["cohort"] = cohort
        reference(self.root, path.name, value)
        return path

    def prospective_cohort(self):
        return dict(name=qualification.PROSPECTIVE_COHORT, head_sha="a" * 40, workflow_blob_sha="b" * 40)

    def test_prospective_cohort_pins_are_retained_without_accepting_resources(self):
        declaration = self.prospective_cohort()
        def collect(root, item, cohort_name):
            self.assertEqual(cohort_name, qualification.PROSPECTIVE_COHORT)
            return self.observation(item["variant"], item["synthetic_id"])
        with mock.patch.object(qualification, "sample", side_effect=collect) as collected:
            report = qualification.qualify(self.campaign(declaration))
        self.assertEqual(collected.call_count, 9)
        self.assertEqual(report["cohort"], declaration)
        self.assertEqual(report["head_sha"], declaration["head_sha"])
        self.assertEqual(report["workflow_blob_sha"], declaration["workflow_blob_sha"])
        self.assertEqual(report["timing_status"], "accepted")
        self.assertEqual(report["status"], "pending")
        self.assertFalse(report["performance_accepted"])
        self.assertEqual(report["resource_review"], "pending")
        self.assertEqual(report["issues"]["2119"]["maximum_time_ratio"], .90)
        self.assertEqual(report["issues"]["2120"]["maximum_time_ratio"], .85)
        self.assertTrue(all(issue["maximum_runner_seconds_ratio"] == 1.05 for issue in report["issues"].values()))

    def test_malformed_cohort_declarations_refuse_before_loading_samples(self):
        valid = self.prospective_cohort()
        invalid = [None, [], "issue2120-evidence-v2", {}, dict(valid, name="unknown"), dict(valid, name=False),
                   dict(valid, branches={}), {key: value for key, value in valid.items() if key != "name"}]
        for key in ("head_sha", "workflow_blob_sha"):
            invalid.append({field: value for field, value in valid.items() if field != key})
            invalid.extend(dict(valid, **{key: value}) for value in (None, False, True, int("1" * 40), "a" * 39, "a" * 41,
                                                                        "A" * 40, "g" * 40, "a" * 40 + "\n", []))
        for declaration in invalid:
            path = self.campaign()
            value = json.loads(path.read_text())
            value["cohort"] = declaration
            reference(self.root, path.name, value)
            with self.subTest(declaration=declaration), mock.patch.object(qualification, "sample") as collected:
                report = qualification.qualify(path)
            collected.assert_not_called()
            self.assertEqual(report["status"], "pending")
            self.assertFalse(report["performance_accepted"])
            self.assertTrue(report["errors"])

    def test_every_prospective_sample_must_match_both_declared_pins(self):
        for key in ("head_sha", "workflow_blob_sha"):
            for changed in range(1, 10):
                def collect(root, item, cohort_name):
                    result = self.observation(item["variant"], item["synthetic_id"])
                    if item["synthetic_id"] == changed:
                        result[key] = "c" * 40
                    return result
                with self.subTest(key=key, changed=changed), mock.patch.object(qualification, "sample", side_effect=collect):
                    report = qualification.qualify(self.campaign(self.prospective_cohort()))
                self.assertEqual(report["status"], "pending")
                self.assertFalse(report["performance_accepted"])
                self.assertIn("sample differs from declared cohort: " + key, report["errors"])

    def test_mutually_equal_samples_cannot_redefine_declared_source_or_workflow(self):
        for key in ("head_sha", "workflow_blob_sha"):
            def collect(root, item, cohort_name):
                result = self.observation(item["variant"], item["synthetic_id"])
                result[key] = "c" * 40
                return result
            with self.subTest(key=key), mock.patch.object(qualification, "sample", side_effect=collect):
                report = qualification.qualify(self.campaign(self.prospective_cohort()))
            self.assertEqual(report["status"], "pending")
            self.assertFalse(report["performance_accepted"])
            self.assertIn("sample differs from declared cohort: " + key, report["errors"])

    def test_declared_cohort_keeps_nine_distinct_samples_and_exact_comparability(self):
        for mutation in ("missing", "duplicate-run", "conditions", "platforms"):
            def collect(root, item, cohort_name):
                result = self.observation(item["variant"], item["synthetic_id"])
                if item["synthetic_id"] == 9:
                    if mutation == "duplicate-run":
                        result["run_id"] = 1
                    elif mutation in ("conditions", "platforms"):
                        result[mutation] = {"changed": True}
                return result
            path = self.campaign(self.prospective_cohort())
            if mutation == "missing":
                value = json.loads(path.read_text())
                value["samples"].pop()
                reference(self.root, path.name, value)
            with self.subTest(mutation=mutation), mock.patch.object(qualification, "sample", side_effect=collect):
                report = qualification.qualify(path)
            self.assertEqual(report["status"], "pending")
            self.assertFalse(report["performance_accepted"])
            self.assertTrue(report["errors"])

    def test_contract_boundaries_and_runner_growth_are_enforced(self):
        def collect(root, item, cohort_name=qualification.LEGACY_COHORT):
            return self.observation(item["variant"], item["synthetic_id"])
        with mock.patch.object(qualification, "sample", side_effect=collect):
            report = qualification.qualify(self.campaign())
        self.assertEqual(report["status"], "pending")
        self.assertEqual(report["timing_status"], "accepted")
        self.assertTrue(report["timing_contract_met"])
        self.assertFalse(report["performance_accepted"])
        self.assertEqual(report["resource_review"], "pending")
        self.assertTrue(report["pending_reviews"])
        self.assertTrue(all(issue["status"] == "pending" for issue in report["issues"].values()))
        self.assertEqual(report["issues"]["2119"]["time_ratio"], .90)
        self.assertEqual(report["issues"]["2120"]["time_ratio"], .85)
        def costly(root, item, cohort_name=qualification.LEGACY_COHORT):
            result = collect(root, item)
            if result["variant"] == "split-overlap":
                result["timing"]["runner_seconds"] = 105.1
            return result
        with mock.patch.object(qualification, "sample", side_effect=costly):
            report = qualification.qualify(self.campaign())
        self.assertEqual(report["status"], "rejected")
        self.assertEqual(report["timing_status"], "rejected")
        self.assertFalse(report["timing_contract_met"])
        self.assertFalse(report["performance_accepted"])
        self.assertEqual(report["issues"]["2120"]["status"], "rejected")

    def test_changed_census_source_images_and_repeated_runs_stay_pending(self):
        for key in ("head_sha", "workflow_blob_sha", "conditions", "platforms", "run_id"):
            def collect(root, item, cohort_name=qualification.LEGACY_COHORT):
                result = self.observation(item["variant"], item["synthetic_id"])
                if item["synthetic_id"] == 9:
                    result[key] = 1 if key == "run_id" else "changed"
                return result
            with self.subTest(key=key), mock.patch.object(qualification, "sample", side_effect=collect):
                result = qualification.qualify(self.campaign())
                self.assertEqual(result["status"], "pending")
                self.assertFalse(result["performance_accepted"])

    def test_archived_native_journal_failure_cannot_be_hidden_by_complete_summary(self):
        item, _, condition = self.complete_desktop()
        directory = self.root / item["phase_directory"]
        next(directory.glob("*.end.json")).unlink()
        with self.assertRaisesRegex(ValueError, "interrupted publication"):
            qualification.desktop(self.root, item, self.run_fixture(), condition, "combined-overlap")

    def complete_desktop(self, release=False, direct=False, shard=None):
        shard = shard or ("release" if release else "checks")
        release = shard == "release"
        platform = "macos" if direct else "windows"
        directory = self.root / "matrix-phases"
        directory.mkdir()
        identity = dict(lane_id=f"desktop/{shard}/{platform}/x86_64/source={'a' * 40}/run=123/attempt=1", suite="desktop", shard=shard,
                        platform=platform, architecture="x86_64", source_revision="a" * 40, source_tree="b" * 40,
                        source_hash="c" * 64, driver_hash="d" * 64, repository="buster14a/buster", run_id="123", run_attempt="1",
                        workflow="CI", job="test", source_path="/retained/producer/build.c" if direct else r"C:\retained\producer\build.c")
        expected = full_policy(platform)
        selected = qualification.coverage_tools._coverage_selected_ids({r["id"]: r for r in expected}, shard)
        policy = dict(version=1, row_count=len(expected), required_count=sum(r["state"] == "required" for r in expected),
                      excluded_count=sum(r["state"] == "excluded" for r in expected),
                      fingerprint=qualification.coverage_tools._coverage_policy_fingerprint(identity, expected))
        self.assertEqual(tuple(policy[k] for k in ("row_count", "required_count", "excluded_count", "fingerprint")),
                         (23, 5, 18, "63bcfb8fade23151") if direct else (28, 6, 22, "46ffb69c2ceae9c0"))
        obligations = {name: dict(state="not-applicable", reason="owned-by-release-shard")
                       for name in ("self_host", "fixed_point", "unity_analysis", "table_audit")}
        if release:
            obligations = dict(self_host=dict(state="not-applicable" if direct else "scheduled", reason="direct-matrix-does-not-consume-fanout" if direct else "canonical-release-fanout"),
                               fixed_point=dict(state="not-applicable" if direct else "scheduled", reason="direct-matrix-does-not-run-self-host" if direct else "canonical-release-fanout"),
                               unity_analysis=dict(state="scheduled", reason="canonical-clang-release"),
                               table_audit=dict(state="scheduled", reason="direct-matrix-default-audit" if direct else "canonical-superbuild-tree"))
        capabilities = [dict(id=row["id"], compiler=row["compiler"], path=row["compiler"], path_hash="e" * 64,
                             identity=row["compiler"], version="1", target="fixture", state="excluded" if row["exclusion"] else "available",
                             reason=row["exclusion"]) for row in expected]
        coverage = dict(identity=identity.copy(), kind="desktop-matrix-coverage", mode="ci", phase="complete", partition_version=2,
                        expected=expected, detected=capabilities, policy=policy, obligations=obligations,
                        executed=[dict(lane_id=identity["lane_id"], status="success", evidence="driver-complete", rows=sorted(selected))])
        rows = [row for row in expected if row["id"] in selected]
        plan = dict(schema=phases.SCHEMA, epoch_us=1, identity=identity, scheduler="direct" if direct else "pooled", test_admission="overlap",
                    outer_jobs=1 if direct else min(len(rows), 4), logical_cpus=4, cpu_budget=4,
                    cpu_time="unknown", peak_rss="unknown", trees=[], tasks=[])

        def task(tree, phase, config, start, end, dependency="ready", pool=""):
            name = phases.task_id(tree, phase, config)
            plan["tasks"].append(dict(id=name, tree=tree, phase=phase, configuration=config, dependency=dependency,
                                      pool_edge=pool, inner_jobs=1, argv=[]))
            argv = ["fixture", name]
            if phase in ("build", "validation", "post_test", "clean"):
                argv = ["fixture-cmake", "--build", f"build/{tree}", "--parallel", "1"]
            elif phase == "self_host":
                argv = ["fixture-driver", "self_host_from_existing", "--build-directory", f"build/{tree}"]
            elif phase == "test":
                argv = [f"build/{tree}/{config}/ide" + ("" if direct else ".exe"), "test"]
            common = dict(id=name, epoch_us=1, pid=10 + len(plan["tasks"]), start_us=start, argv=argv)
            if phase == "evidence":
                common["authority"] = "driver_callback"
            phase_tests.write(directory, f"{name}.{common['pid']}.start.json", dict(common, state="running"))
            phase_tests.write(directory, f"{name}.{common['pid']}.end.json", dict(common, state="success", child_start_us=start,
                              end_us=end, publication_start_us=end, result=0, platform_status=0, spawned=1, timed_out=0,
                              termination_requested=0, forcibly_terminated=0, cpu_time="unknown", peak_rss="unknown",
                              test_jobs="1", ctest_jobs="not-applicable"))
            if dependency == "ready":
                phase_tests.write(directory, name + ".ready.json", dict(epoch_us=1, ready_us=start))

        for i, row in enumerate(rows):
            compiler, config, tree = row["compiler"], row["configuration"], f"tree{i}"
            plan["trees"].append(dict(id=tree, rows=[row["id"]], build_directory=f"build/{tree}", compiler=compiler,
                                      compiler_path=compiler, compiler_sha256="e" * 64, compiler_identity=compiler, compiler_version="1",
                                      target="fixture", configurations=config, sanitize=int(row["sanitize"]), fuzz=int(row["fuzz"]),
                                      lto=False, generator="Ninja Multi-Config", linker="DEFAULT"))
            task(tree, "configure", "", 2 + i * 2, 3 + i * 2)
        if not direct:
            task("matrix", "scheduler", "", 20, 900)
            for i in range(len(rows)):
                task(f"tree{i}", "build", "", 30 + i * 20, 40 + i * 20, "scheduler", f"build-tree{i}")
        current = 150
        for i, row in enumerate(rows):
            tree, config = f"tree{i}", row["configuration"]
            if row["compiler"] == "clang":
                if direct and row["unity"]:
                    task(tree, "build", config, current, current + 10)
                    current += 15
                task(tree, "validation", config, current, current + 30,
                     "ready" if direct else phases.task_id(tree, "build"), "" if direct else f"validation-{tree}")
                task(tree, "test", config, current + 5, current + 25, "nested")
                current += 35
            elif direct:
                task(tree, "build", config, current, current + 10)
                current += 15
        if release:
            task("tree0", "post_test", "Release", current, current + 10,
                 "ready" if direct else phases.task_id("tree0", "validation", "Release"), "" if direct else "validation-tree0")
            current += 15
            if not direct:
                task("tree0", "self_host", "Release", current, current + 10, phases.task_id("tree0", "build"), "self-host")
                current += 15
                for phase, config in (("clean", "Release"), ("evidence", "capture"), ("evidence", "clean")):
                    task("tree0", phase, config, current, current + 5)
                    current += 10
        task("matrix", "evidence", "coverage", 910, 915)
        phase_tests.write(directory, "plan.json", plan)
        phase_tests.write(directory, "terminal.json", dict(epoch_us=1, terminal_us=920, result=0))
        condition = dict(image_os="fixture", image_version="1", runner="synthetic", caches={"BUSTER_CI_ZIG_CACHE_HIT": "false"})
        meta = dict(GITHUB_SHA="a" * 40, GITHUB_RUN_ID="123", GITHUB_RUN_ATTEMPT="1", BUSTER_MATRIX_SHARD=shard,
                    ImageOS="fixture", ImageVersion="1", BUSTER_CI_RUNNER="synthetic", BUSTER_CI_ZIG_CACHE_HIT="false")
        summary = phases.analyze(directory, coverage, meta)
        tests = []
        pairs = [(row, cap) for row, cap in zip(coverage["expected"], coverage["detected"]) if row["id"] in selected and row["execution"] == "runtime"]
        for i, (row, cap) in enumerate(pairs):
            tree = next(t for t in summary["trees"] if row["id"] in t["rows"])
            event = next(e for e in summary["events"] if e["id"] == phases.task_id(tree["id"], "test", row["configuration"]))
            sidecar = self.root / "unit-observations" / event["id"]
            sidecar.mkdir(parents=True)
            inventory = [dict(index=0, name="compiler_driver_tests", table_audit=False), dict(index=1, name="fixture", table_audit=False), dict(index=2, name="table_audit_fixture", table_audit=True)]
            lines = [HOST_RECORD] + [f"CI_UNIT_MODULE_V1 index={r['index']} module={r['name']} table_audit={int(r['table_audit'])} enabled={int(not r['table_audit'])} selected=0 group={'driver' if r['index'] == 0 else 'rest'}" for r in inventory]
            lines += ["CI_UNIT_BATCH_V1 group=inventory modules=0 modules_passed=0 assertions=0 passed=0 failed=0 external=0 external_passed=0 status=inventory", "[0/0] Unit tests (0 of 3 modules selected)", "[0/0] Module tests", "[0/0] External tests"]
            inventory_path = sidecar / "inventory.log"
            inventory_path.write_text("\n".join(lines) + "\n")
            log = sidecar / "test.log"
            timing = "TEST_MODULE_TIMING index=0 module=compiler_driver_tests duration_ns=1 passed=1 failed=0 assertions=1 status=pass\nTEST_MODULE_TIMING index=1 module=fixture duration_ns=1 passed=2 failed=0 assertions=2 status=pass\n"
            if release or direct:
                timing += "TEST_MODULE_TIMING index=2 module=table_audit_fixture duration_ns=1 passed=1 failed=0 assertions=1 status=pass\n"
            log.write_text(timing + ("[4/4] Unit tests\n[3/3] Module tests\n" if release or direct else "[3/3] Unit tests\n[2/2] Module tests\n") + "[0/0] External tests\n")
            unit = dict(schema="buster-ci-unit-tests-measure-v1", arm="baseline", mode="serial", exit_code=0, test_workers=1, elapsed_us=20,
                        inventory=inventory, log=str(log.relative_to(self.root)),
                        identity=dict(source_revision="a" * 40, binary_sha256="e" * 64, runner_image={k: condition[k] for k in ("image_os", "image_version", "runner")},
                                      platform="macos" if direct else "windows", architecture="x86_64", configuration=row["configuration"], sanitize=row["sanitize"], fuzz=row["fuzz"], table_audits=release or direct,
                                      toolchain={k: cap[k] for k in qualification.CAP_KEYS}, cpu_budget=4, native_host_profile=copy.deepcopy(HOST_PROFILE)))
            receipt = {k: event[k] for k in ("id", "epoch_us", "pid", "argv")}
            receipt.update(schema="buster-desktop-unit-observation-v1", source_revision="a" * 40, run_id="123", run_attempt="1", binary_path=event["argv"][0], binary_sha256="e" * 64,
                           inventory_file="inventory.log", log_file="test.log", inventory_sha256=hashlib.sha256(inventory_path.read_bytes()).hexdigest(),
                           log_sha256=hashlib.sha256(log.read_bytes()).hexdigest(), binary_unchanged=True, capture_complete=True, test_result=0)
            tests.append(dict(row_id=row["id"], manifest=reference(self.root, f"unit{i}.json", unit),
                              observation=reference(self.root, str((sidecar / "observation.json").relative_to(self.root)), receipt), log_sha256=receipt["log_sha256"]))
        result = dict(success=True, metadata=meta, matrix_phases=summary)
        item = dict(job=("macOS x86-64" if direct else "Windows x86-64") + " " + shard, phase_directory=directory.name, tests=tests,
                    coverage=reference(self.root, "coverage.json", coverage), result=reference(self.root, "result.json", result), phases=reference(self.root, "phases.json", summary))
        return item, coverage, condition

    def test_complete_census_replays_and_nonreproducible_binary_hashes_do_not_change_population(self):
        item, coverage, condition = self.complete_desktop()
        before = qualification.desktop(self.root, item, self.run_fixture(), condition, "combined-overlap")
        directory = self.root / item["phase_directory"]
        plan = phases.read(directory / "plan.json")
        plan["identity"]["driver_hash"] = coverage["identity"]["driver_hash"] = "f" * 64
        phase_tests.write(directory, "plan.json", plan)
        result = qualification.record(self.root, item["result"])
        summary = phases.analyze(directory, coverage, result["metadata"])
        result["matrix_phases"] = summary
        item.update(coverage=reference(self.root, "coverage.json", coverage), result=reference(self.root, "result.json", result), phases=reference(self.root, "phases.json", summary))
        for i, test in enumerate(item["tests"]):
            unit = qualification.record(self.root, test["manifest"])
            unit["identity"]["binary_sha256"] = "f" * 64
            test["manifest"] = reference(self.root, f"unit{i}.json", unit)
            receipt = qualification.record(self.root, test["observation"])
            receipt["binary_sha256"] = "f" * 64
            test["observation"] = reference(self.root, test["observation"]["path"], receipt)
        after = qualification.desktop(self.root, item, self.run_fixture(), condition, "combined-overlap")
        self.assertEqual(before, after)
        self.assertEqual(len(after["census"]), 2)
        item["tests"].pop()
        with self.assertRaisesRegex(ValueError, "runtime assertion census"):
            qualification.desktop(self.root, item, self.run_fixture(), condition, "combined-overlap")

    def test_host_profile_is_required_and_bound_to_same_binary_query(self):
        item, _, condition = self.complete_desktop()
        test = item["tests"][0]
        manifest_path = self.root / test["manifest"]["path"]
        original = phases.read(manifest_path)
        for profile in (None, dict(HOST_PROFILE, feature_words=[9, 2, 3, 4]),
                        dict(HOST_PROFILE, simd_512_base=True), dict(HOST_PROFILE, architecture="aarch64"),
                        dict(HOST_PROFILE, feature_words=[True, 2, 3, 4]), dict(HOST_PROFILE, simd_512_base=0),
                        dict(HOST_PROFILE, feature_words=[1.0, 2, 3, 4])):
            manifest = copy.deepcopy(original)
            if profile is None:
                del manifest["identity"]["native_host_profile"]
            else:
                manifest["identity"]["native_host_profile"] = profile
            test["manifest"] = reference(self.root, test["manifest"]["path"], manifest)
            with self.subTest(profile=profile), self.assertRaisesRegex(ValueError, "same-binary inventory query"):
                qualification.desktop(self.root, item, self.run_fixture(), condition, "combined-overlap")

    def test_missing_duplicate_and_malformed_host_query_cannot_qualify(self):
        item, _, condition = self.complete_desktop()
        test = item["tests"][0]
        receipt_path = self.root / test["observation"]["path"]
        original_receipt = phases.read(receipt_path)
        inventory_path = receipt_path.parent / "inventory.log"
        original = inventory_path.read_text()
        mutations = (original.replace(HOST_RECORD + "\n", ""), HOST_RECORD + "\n" + original,
                     original.replace("word0=1", "word0=18446744073709551616"))
        for mutation in mutations:
            inventory_path.write_text(mutation)
            receipt = dict(original_receipt, inventory_sha256=hashlib.sha256(inventory_path.read_bytes()).hexdigest())
            test["observation"] = reference(self.root, test["observation"]["path"], receipt)
            with self.subTest(mutation=mutation), self.assertRaises(ValueError):
                qualification.desktop(self.root, item, self.run_fixture(), condition, "combined-overlap")

    def test_host_profile_and_assertion_differences_both_keep_campaign_pending(self):
        item, _, condition = self.complete_desktop()
        desktop = qualification.desktop(self.root, item, self.run_fixture(), condition, "combined-overlap")
        self.assertTrue(all(row["native_host_profile"] == HOST_PROFILE for row in desktop["census"].values()))
        for changed in ("native_host_profile", "modules"):
            def collect(root, sample, cohort_name=qualification.LEGACY_COHORT):
                result = self.observation(sample["variant"], sample["synthetic_id"])
                result["platforms"] = copy.deepcopy(desktop)
                if sample["synthetic_id"] == 9:
                    census = next(iter(result["platforms"]["census"].values()))
                    if changed == "native_host_profile":
                        census[changed]["feature_words"][0] = 9
                    else:
                        census[changed]["compiler_driver_tests"]["assertions"] += 1
                        census[changed]["compiler_driver_tests"]["passed"] += 1
                return result
            with self.subTest(changed=changed), mock.patch.object(qualification, "sample", side_effect=collect):
                result = qualification.qualify(self.campaign())
            self.assertEqual(result["status"], "pending")
            self.assertFalse(result["performance_accepted"])

    def test_unknown_metadata_keeps_retained_native_runner_observations(self):
        item, _, condition = self.complete_desktop()
        expected = qualification.desktop(self.root, item, self.run_fixture(), condition, "combined-overlap")
        result = qualification.record(self.root, item["result"])
        for name in ("ImageOS", "ImageVersion", "BUSTER_CI_RUNNER", "BUSTER_CI_ZIG_CACHE_HIT"):
            result["metadata"][name] = "unknown"
        item["result"] = reference(self.root, "unknown-metadata.json", result)
        self.assertEqual(qualification.desktop(self.root, item, self.run_fixture(), condition, "combined-overlap"), expected)
        for name in ("ImageOS", "ImageVersion", "BUSTER_CI_RUNNER", "BUSTER_CI_ZIG_CACHE_HIT"):
            del result["metadata"][name]
        item["result"] = reference(self.root, "missing-metadata.json", result)
        self.assertEqual(qualification.desktop(self.root, item, self.run_fixture(), condition, "combined-overlap"), expected)
        self.assertEqual(qualification.phase_environment({"runner": {"ImageOS": "unknown"}}, {"ImageOS": "unknown"}), {"ImageOS": "unknown"})
        result["metadata"]["GITHUB_SHA"] = "unknown"
        item["result"] = reference(self.root, "unknown-source.json", result)
        with self.assertRaisesRegex(ValueError, "result run identity"):
            qualification.desktop(self.root, item, self.run_fixture(), condition, "combined-overlap")

    def test_conflicting_known_metadata_cannot_replace_native_observations(self):
        item, _, condition = self.complete_desktop()
        for field, value in (("ImageOS", "other"), ("ImageVersion", "2"), ("BUSTER_CI_RUNNER", "other"), ("BUSTER_CI_ZIG_CACHE_HIT", "true")):
            result = qualification.record(self.root, item["result"])
            result["metadata"][field] = value
            invalid = dict(item, result=reference(self.root, "conflicting-metadata.json", result))
            with self.subTest(field=field), self.assertRaisesRegex(ValueError, "conflicting known phase metadata"):
                qualification.desktop(self.root, invalid, self.run_fixture(), condition, "combined-overlap")

    def reject_coverage(self, item, coverage, condition, change, message):
        changed = copy.deepcopy(coverage)
        change(changed)
        invalid = dict(item, coverage=reference(self.root, "invalid-coverage.json", changed))
        with self.assertRaisesRegex(ValueError, message):
            qualification.desktop(self.root, invalid, self.run_fixture(), condition, "combined-overlap")

    def test_detected_capability_duplicates_are_rejected_before_projection(self):
        item, coverage, condition = self.complete_desktop()
        qualification.desktop(self.root, item, self.run_fixture(), condition, "combined-overlap")
        original = coverage["detected"]
        conflicting = dict(original[0], version="conflicting")
        changes = {
            "identical-extra": original + [copy.deepcopy(original[0])],
            "conflicting-overwritten": [conflicting] + original,
            "identical-same-length": original[:1] + [copy.deepcopy(original[0])] + original[2:],
            "conflicting-same-length": original[:1] + [conflicting] + original[2:],
        }
        for name, detected in changes.items():
            with self.subTest(change=name):
                self.reject_coverage(item, coverage, condition,
                                     lambda value: value.update(detected=detected), "detected capability")

    def test_detected_capability_census_rejects_missing_foreign_and_malformed_records(self):
        item, coverage, condition = self.complete_desktop()
        original = coverage["detected"]
        changes = [None, False, 1, "records", {}, [], original[:-1],
                   original[:-1] + [dict(original[-1], id="foreign")]]
        for row in (None, False, 1, "record", [], {}, {"id": None}, {"id": False},
                    {"id": 1}, {"id": []}, {"id": {}}, {"id": ""}):
            changes.append(original[:-1] + [row])
        for detected in changes:
            with self.subTest(detected=detected):
                self.reject_coverage(item, coverage, condition,
                                     lambda value: value.update(detected=detected), "detected capability")
        self.reject_coverage(item, coverage, condition, lambda value: value.pop("detected"), "detected capability")

    def test_recomputed_shrunken_policy_is_rejected_by_independent_anchor(self):
        item, coverage, condition = self.complete_desktop()
        def shrink(value):
            row = next(r for r in value["expected"] if r["state"] == "excluded")
            value["expected"].remove(row)
            value["detected"] = [cap for cap in value["detected"] if cap["id"] != row["id"]]
            value["policy"].update(row_count=27, excluded_count=21,
                                   fingerprint=qualification.coverage_tools._coverage_policy_fingerprint(value["identity"], value["expected"]))
        self.reject_coverage(item, coverage, condition, shrink, "independent lane anchor")

    def test_row_complete_smaller_native_journal_still_cannot_qualify(self):
        item, coverage, condition = self.complete_desktop()
        omitted = next(row for row in coverage["expected"] if row["state"] == "required" and row["compiler"] == "cl")
        coverage["expected"].remove(omitted)
        coverage["detected"] = [cap for cap in coverage["detected"] if cap["id"] != omitted["id"]]
        coverage["executed"][0]["rows"].remove(omitted["id"])
        coverage["policy"].update(row_count=27, required_count=5,
                                  fingerprint=qualification.coverage_tools._coverage_policy_fingerprint(coverage["identity"], coverage["expected"]))
        directory = self.root / item["phase_directory"]
        plan = phases.read(directory / "plan.json")
        tree = next(tree for tree in plan["trees"] if omitted["id"] in tree["rows"])
        plan["trees"].remove(tree)
        removed = {task["id"] for task in plan["tasks"] if task["tree"] == tree["id"]}
        plan["tasks"] = [task for task in plan["tasks"] if task["id"] not in removed]
        for path in directory.iterdir():
            if path.name.split(".", 1)[0] in removed:
                path.unlink()
        phase_tests.write(directory, "plan.json", plan)
        result = qualification.record(self.root, item["result"])
        summary = phases.analyze(directory, coverage, result["metadata"])
        self.assertTrue(summary["complete"])
        result["matrix_phases"] = summary
        item.update(coverage=reference(self.root, "coverage.json", coverage), result=reference(self.root, "result.json", result),
                    phases=reference(self.root, "phases.json", summary))
        with self.assertRaisesRegex(ValueError, "independent lane anchor"):
            qualification.desktop(self.root, item, self.run_fixture(), condition, "combined-overlap")

    def test_policy_version_counts_and_fingerprint_match_independent_anchor(self):
        item, coverage, condition = self.complete_desktop()
        changes = [("version", value) for value in (None, True, False, 2)] + [
            ("row_count", 27), ("required_count", 5), ("excluded_count", 21), ("fingerprint", "0" * 16)]
        for key, value in changes:
            with self.subTest(key=key, value=value):
                self.reject_coverage(item, coverage, condition, lambda p: p["policy"].update({key: value}), "policy version|independent lane anchor")
        self.reject_coverage(item, coverage, condition, lambda p: p["policy"].pop("version"), "policy version")

    def test_partition_version_two_is_independent_from_policy_version_one(self):
        item, coverage, condition = self.complete_desktop()
        self.assertEqual(coverage["policy"]["version"], 1)
        self.assertEqual(coverage["partition_version"], 2)
        for value in (1, 3, None, True, False):
            with self.subTest(value=value):
                self.reject_coverage(item, coverage, condition, lambda p: p.update(partition_version=value), "partition version")
        self.reject_coverage(item, coverage, condition, lambda p: p.pop("partition_version"), "partition version")

    def test_full_source_bound_row_semantics_are_checked_before_digest(self):
        item, coverage, condition = self.complete_desktop()
        clang = next(i for i, row in enumerate(coverage["expected"]) if row["state"] == "required" and row["compiler"] == "clang" and row["configuration"] == "Debug")
        portability = next(i for i, row in enumerate(coverage["expected"]) if row["state"] == "required" and row["compiler"] == "cl")
        excluded = next(i for i, row in enumerate(coverage["expected"]) if row["state"] == "excluded")
        mutations = [(clang, "execution", "compile-link", "execution kind"),
                     (clang, "execution", "package-only", "execution kind"),
                     (portability, "execution", "package-only", "execution kind"),
                     (excluded, "execution", "runtime", "execution kind"),
                     (clang, "optimize", True, "optimization/unity"),
                     (clang, "unity", True, "optimization/unity")]
        mutations += [(clang, key, value, "row booleans") for key, value in (("optimize", 0), ("sanitize", 1), ("fuzz", "false"), ("unity", None))]
        for index, key, value, message in mutations:
            with self.subTest(index=index, key=key, value=value):
                self.reject_coverage(item, coverage, condition, lambda p: p["expected"][index].update({key: value}), message)

    def test_obligations_cannot_be_omitted_expanded_or_reassigned(self):
        item, coverage, condition = self.complete_desktop(release=True)
        self.assertEqual(set(coverage["obligations"]), {"self_host", "fixed_point", "unity_analysis", "table_audit"})
        self.assertTrue(all(value["state"] == "scheduled" for value in coverage["obligations"].values()))
        changes = [lambda p: p.pop("obligations"), lambda p: p.update(obligations={}),
                   lambda p: p["obligations"].update(extra=dict(state="scheduled", reason="invented"))]
        for name in coverage["obligations"]:
            changes.extend((lambda p, n=name: p["obligations"].pop(n),
                            lambda p, n=name: p["obligations"][n].update(state="not-applicable"),
                            lambda p, n=name: p["obligations"][n].update(reason="owned-by-release-shard")))
        for i, change in enumerate(changes):
            with self.subTest(change=i):
                self.reject_coverage(item, coverage, condition, change, "obligations.*independent lane policy")

    def test_grouped_and_split_jobs_preserve_full_policy_and_release_obligations(self):
        original = self.root
        reports = {}
        try:
            for shard in ("checks", "sanitized-debug", "sanitized-release", "portability", "release"):
                with self.subTest(shard=shard):
                    self.root = original / shard
                    self.root.mkdir()
                    item, coverage, condition = self.complete_desktop(shard=shard)
                    variant = "split-overlap" if shard in github.SPLIT_CHECK_SHARDS else "combined-overlap"
                    reports[shard] = qualification.desktop(self.root, item, self.run_fixture(variant), condition, variant)
                    self.assertEqual(len(reports[shard]["rows"]), 28)
                    self.assertEqual(coverage["policy"]["required_count"], 6)
                    if shard != "release":
                        self.assertTrue(all(v == dict(state="not-applicable", reason="owned-by-release-shard") for v in coverage["obligations"].values()))
                    else:
                        self.assertTrue(all(v["state"] == "scheduled" for v in coverage["obligations"].values()))
            split_rows = [row for shard in github.SPLIT_CHECK_SHARDS for row in reports[shard]["selected"]]
            self.assertEqual(Counter(split_rows), Counter(reports["checks"]["selected"]))
            self.assertEqual(len(split_rows), 5)
            self.assertEqual(len(reports["release"]["selected"]), 1)
            self.assertEqual(set(split_rows) | reports["release"]["selected"],
                             {row["id"] for row in reports["checks"]["rows"] if row["state"] == "required"})
            for shard in github.SPLIT_CHECK_SHARDS:
                self.assertEqual(reports[shard]["policy"], reports["checks"]["policy"])
                self.assertEqual(reports[shard]["rows"], reports["checks"]["rows"])
        finally:
            self.root = original

    def test_native_receipt_identity_completion_binary_and_task_path_are_required(self):
        item, _, condition = self.complete_desktop()
        receipt = qualification.record(self.root, item["tests"][0]["observation"])
        changes = {"schema": "wrong", "id": "foreign-task", "pid": 999, "epoch_us": 2, "argv": ["other-binary"],
                   "source_revision": "f" * 40, "run_id": "999", "run_attempt": "2", "binary_path": "other-binary",
                   "binary_sha256": "f" * 64, "binary_unchanged": False, "capture_complete": False, "test_result": 1,
                   "inventory_file": "other.log", "log_file": "other.log"}
        for key, value in changes.items():
            invalid = copy.deepcopy(item)
            changed = dict(receipt, **{key: value})
            test = invalid["tests"][0]
            test["observation"] = reference(self.root, test["observation"]["path"], changed)
            with self.subTest(key=key), self.assertRaises(ValueError):
                qualification.desktop(self.root, invalid, self.run_fixture(), condition, "combined-overlap")
        invalid = copy.deepcopy(item)
        invalid["tests"][0]["observation"] = reference(self.root, "foreign-observation.json", receipt)
        with self.assertRaisesRegex(ValueError, "exact native task sidecar"):
            qualification.desktop(self.root, invalid, self.run_fixture(), condition, "combined-overlap")

    def test_same_bytes_from_another_log_cannot_replace_native_log(self):
        item, _, condition = self.complete_desktop()
        test = item["tests"][0]
        unit = qualification.record(self.root, test["manifest"])
        original = self.root / unit["log"]
        substitute = self.root / "substitute.log"
        substitute.write_bytes(original.read_bytes())
        unit["log"] = substitute.name
        test["manifest"] = reference(self.root, test["manifest"]["path"], unit)
        with self.assertRaisesRegex(ValueError, "native test log"):
            qualification.desktop(self.root, item, self.run_fixture(), condition, "combined-overlap")

    def test_native_inventory_digest_and_independent_query_are_checked(self):
        item, _, condition = self.complete_desktop()
        test = item["tests"][0]
        receipt = qualification.record(self.root, test["observation"])
        inventory_path = (self.root / test["observation"]["path"]).parent / "inventory.log"
        original = inventory_path.read_text()
        inventory_path.write_text(original.replace("index=2", "index=3"))
        with self.assertRaisesRegex(ValueError, "digest mismatch"):
            qualification.desktop(self.root, item, self.run_fixture(), condition, "combined-overlap")
        receipt["inventory_sha256"] = hashlib.sha256(inventory_path.read_bytes()).hexdigest()
        test["observation"] = reference(self.root, test["observation"]["path"], receipt)
        with self.assertRaisesRegex(ValueError, "canonical index"):
            qualification.desktop(self.root, item, self.run_fixture(), condition, "combined-overlap")
        inventory_path.write_text("\n".join(line for line in original.splitlines() if "index=2 " not in line).replace("of 3 modules", "of 2 modules") + "\n")
        receipt["inventory_sha256"] = hashlib.sha256(inventory_path.read_bytes()).hexdigest()
        test["observation"] = reference(self.root, test["observation"]["path"], receipt)
        with self.assertRaisesRegex(ValueError, "independent inventory"):
            qualification.desktop(self.root, item, self.run_fixture(), condition, "combined-overlap")

    def test_observed_binary_paths_use_original_windows_and_posix_source_roots(self):
        item, coverage, _ = self.complete_desktop()
        test = item["tests"][0]
        manifest_path = qualification.retained(self.root, test["manifest"])
        unit = qualification.units.validate_sample(manifest_path)
        receipt = qualification.record(self.root, test["observation"])
        summary = qualification.record(self.root, item["phases"])
        original_event = next(e for e in summary["events"] if e["id"] == receipt["id"])
        cases = (("windows", r"C:\retained\producer\build.c", r"build\tree0\Debug\ide.exe", r"c:\RETAINED\PRODUCER\build\tree0\Debug\ide.exe"),
                 ("linux", "/retained/producer/build.c", "build/tree0/Debug/ide", "/retained/producer/build/tree0/Debug/ide"))
        for platform, source, binary, observed in cases:
            identity = dict(coverage["identity"], platform=platform, source_path=source)
            event = dict(original_event, argv=[binary, "test"])
            value = dict(receipt, argv=event["argv"], binary_path=observed)
            test["observation"] = reference(self.root, test["observation"]["path"], value)
            with self.subTest(platform=platform):
                self.assertEqual(qualification.observation(self.root, item, test, unit, manifest_path, event, identity), value)
            value["binary_path"] = observed.replace("Debug", "Release")
            test["observation"] = reference(self.root, test["observation"]["path"], value)
            with self.subTest(wrong_tree=platform), self.assertRaisesRegex(ValueError, "binary/path mismatch"):
                qualification.observation(self.root, item, test, unit, manifest_path, event, identity)

    def test_canonical_release_executes_audits_after_audit_disabled_inventory_query(self):
        item, _, condition = self.complete_desktop(release=True)
        result = qualification.desktop(self.root, item, self.run_fixture(), condition, "combined-overlap")
        census = next(iter(result["census"].values()))
        self.assertEqual(census["skipped_table_audits"], [])
        self.assertEqual(set(census["modules"]), {"compiler_driver_tests", "fixture", "table_audit_fixture"})
        self.assertEqual(sum(m["assertions"] for m in census["modules"].values()), 4)
        test = item["tests"][0]
        unit = qualification.record(self.root, test["manifest"])
        unit["identity"]["table_audits"] = False
        test["manifest"] = reference(self.root, test["manifest"]["path"], unit)
        log = self.root / unit["log"]
        log.write_text("\n".join(line for line in log.read_text().splitlines() if "module=table_audit_fixture" not in line).replace("[4/4] Unit", "[3/3] Unit").replace("[3/3] Module", "[2/2] Module") + "\n")
        receipt = qualification.record(self.root, test["observation"])
        receipt["log_sha256"] = test["log_sha256"] = hashlib.sha256(log.read_bytes()).hexdigest()
        test["observation"] = reference(self.root, test["observation"]["path"], receipt)
        with self.assertRaisesRegex(ValueError, "table audit policy"):
            qualification.desktop(self.root, item, self.run_fixture(), condition, "combined-overlap")

    def test_direct_canonical_release_keeps_analysis_and_its_default_audits(self):
        item, coverage, condition = self.complete_desktop(release=True, direct=True)
        result = qualification.desktop(self.root, item, self.run_fixture(), condition, "combined-overlap")
        self.assertEqual(coverage["obligations"]["self_host"], dict(state="not-applicable", reason="direct-matrix-does-not-consume-fanout"))
        self.assertEqual(coverage["obligations"]["fixed_point"], dict(state="not-applicable", reason="direct-matrix-does-not-run-self-host"))
        self.assertEqual(coverage["obligations"]["unity_analysis"], dict(state="scheduled", reason="canonical-clang-release"))
        self.assertEqual(coverage["obligations"]["table_audit"], dict(state="scheduled", reason="direct-matrix-default-audit"))
        self.assertEqual(len(result["selected"]), 1)
        census = next(iter(result["census"].values()))
        self.assertFalse(census["skipped_table_audits"])
        self.assertIn("table_audit_fixture", census["modules"])
        self.reject_coverage(item, coverage, condition, lambda p: p["obligations"]["unity_analysis"].update(state="not-applicable"), "obligations.*independent lane policy")

    def test_direct_runtime_keeps_default_audits_even_without_unity(self):
        item, _, condition = self.complete_desktop(direct=True)
        result = qualification.desktop(self.root, item, self.run_fixture(), condition, "combined-overlap")
        self.assertEqual(len(result["rows"]), 23)
        self.assertTrue(all(not row["unity"] for row in result["rows"] if row["id"] in result["selected"]))
        self.assertTrue(all(not census["skipped_table_audits"] and "table_audit_fixture" in census["modules"] for census in result["census"].values()))
        test = item["tests"][0]
        unit = qualification.record(self.root, test["manifest"])
        unit["identity"]["table_audits"] = False
        test["manifest"] = reference(self.root, test["manifest"]["path"], unit)
        log = self.root / unit["log"]
        log.write_text("\n".join(line for line in log.read_text().splitlines() if "module=table_audit_fixture" not in line).replace("[4/4] Unit", "[3/3] Unit").replace("[3/3] Module", "[2/2] Module") + "\n")
        receipt = qualification.record(self.root, test["observation"])
        receipt["log_sha256"] = test["log_sha256"] = hashlib.sha256(log.read_bytes()).hexdigest()
        test["observation"] = reference(self.root, test["observation"]["path"], receipt)
        with self.assertRaisesRegex(ValueError, "table audit policy"):
            qualification.desktop(self.root, item, self.run_fixture(), condition, "combined-overlap")


if __name__ == "__main__":
    unittest.main()
