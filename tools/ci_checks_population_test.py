#!/usr/bin/env python3
"""Offline reader controls; synthetic timings are never campaign evidence."""
import copy
from datetime import datetime, timedelta, timezone
import hashlib
import json
from pathlib import Path
import tempfile
import unittest
from unittest import mock

import ci_checks_population as population
import ci_checks_qualification as qualification
import github_ci_time as github
import analyzer_reference


ROOT = Path(__file__).resolve().parents[1]
CATALOGUE = ROOT / "docs/ci-checks-native-census-d69.json"


def reference(root, name, value):
    path = root / name
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value) + "\n", encoding="utf-8")
    return {"path": name, "sha256": hashlib.sha256(path.read_bytes()).hexdigest()}


def observed_platforms(catalogue, profile_index=0):
    platforms = copy.deepcopy(catalogue["platforms"])
    for platform in platforms.values():
        platform["census"] = {row: copy.deepcopy(censuses[profile_index % len(censuses)])
                              for row, censuses in platform["census"].items()}
    return platforms


class NativeCatalogueTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.catalogue = json.loads(CATALOGUE.read_text(encoding="utf-8"))

    def observation(self, profile_index=0):
        return {"platforms": observed_platforms(self.catalogue, profile_index)}

    def variable_row(self, observation):
        choices = [(platform, row) for platform, item in self.catalogue["platforms"].items()
                   for row, censuses in item["census"].items() if len(censuses) > 1]
        self.assertTrue(choices, "The authentic catalogue must retain the observed CPU variation")
        platform, row = choices[0]
        return observation["platforms"][platform]["census"][row]

    def assert_refused(self, observation):
        with self.assertRaises((ValueError, KeyError, TypeError)):
            population.validate_census(observation, self.catalogue)

    def test_all_complete_authentic_profile_censuses_are_admitted(self):
        for index in (0, 1):
            with self.subTest(profile_index=index):
                population.validate_census(self.observation(index), self.catalogue)

    def test_unknown_full_profile_is_not_inferred_from_assertion_totals(self):
        for field in ("feature_words", "simd_512_base", "simd_512", "architecture", "feature_source"):
            observation = self.observation()
            profile = self.variable_row(observation)["native_host_profile"]
            if field == "feature_words":
                profile[field][0] ^= 1
            elif field.startswith("simd_"):
                profile[field] = not profile[field]
            else:
                profile[field] = "unknown"
            with self.subTest(field=field):
                self.assert_refused(observation)

    def test_complete_row_module_and_assertion_obligations_are_exact(self):
        for change in ("remove-row", "extra-row", "remove-module", "extra-module", "assertions", "passed",
                       "failed", "status", "index", "inventory", "skipped_table_audits", "external"):
            observation = self.observation()
            platform = next(iter(observation["platforms"].values()))
            census = next(iter(platform["census"].values()))
            module_name = next(iter(census["modules"]))
            module = census["modules"][module_name]
            if change == "remove-row":
                platform["census"].pop(next(iter(platform["census"])))
            elif change == "extra-row":
                platform["census"]["unexpected-row"] = copy.deepcopy(census)
            elif change == "remove-module":
                del census["modules"][module_name]
            elif change == "extra-module":
                census["modules"]["unexpected-module"] = copy.deepcopy(module)
            elif change in ("assertions", "passed", "failed", "index"):
                module[change] += 1
            elif change == "status":
                module[change] = "failure"
            elif change == "inventory":
                census[change] = census[change][:-1]
            elif change == "skipped_table_audits":
                census[change].append("unexpected-module")
            else:
                census[change] = {"unexpected": 1}
            with self.subTest(change=change):
                self.assert_refused(observation)

    def test_platform_identity_and_selected_rows_remain_strict(self):
        for field in ("identity", "selected", "platforms"):
            observation = self.observation()
            if field == "platforms":
                observation["platforms"].pop(next(iter(observation["platforms"])))
            else:
                platform = next(iter(observation["platforms"].values()))
                platform[field] = {} if field == "identity" else platform[field][:-1]
            with self.subTest(field=field):
                self.assert_refused(observation)

    def test_profile_flags_and_assertion_census_require_exact_json_types(self):
        for field in ("simd_512_base", "simd_512", "assertions", "index"):
            observation = self.observation()
            census = self.variable_row(observation)
            if field.startswith("simd_"):
                census["native_host_profile"][field] = int(census["native_host_profile"][field])
            else:
                module = next(iter(census["modules"].values()))
                module[field] = float(module[field])
            with self.subTest(field=field):
                self.assert_refused(observation)
        observation = self.observation()
        identity = next(iter(observation["platforms"].values()))["identity"]
        identity["cpu_budget"] = float(identity["cpu_budget"])
        self.assert_refused(observation)


class PopulationEpochTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.catalogue = json.loads(CATALOGUE.read_text(encoding="utf-8"))
        self.origin = datetime(2026, 10, 1, tzinfo=timezone.utc)
        prepared = population.prepare(self.root / "declaration.json")
        self.declaration = prepared["declaration"]
        receipt = dict(id=123456, issue_url="https://api.github.com/repos/buster14a/buster/issues/2610",
                       url="https://api.github.com/repos/buster14a/buster/issues/comments/123456",
                       html_url="https://github.com/buster14a/buster/issues/2610#issuecomment-123456",
                       body=prepared["publication_marker"], created_at=self.stamp(0), updated_at=self.stamp(0))
        self.campaign = dict(schema=population.SCHEMA, declaration=self.declaration,
                             publication=reference(self.root, "publication.json", receipt), attempts=[])
        self.observations = {}
        self.fixed_inventory = False
        order = [population.VARIANTS[letter] for block in population.BLOCKS for letter in block]
        for ordinal, variant in enumerate(order, 1):
            run = self.run_fixture(ordinal, variant)
            run_ref = reference(self.root, f"runs/{ordinal}.json", run)
            item = dict(variant=variant, run=dict(run_ref, path="../" + run_ref["path"]), synthetic_id=ordinal)
            inputs_ref = self.input_fixture(ordinal, item, run)
            sample_ref = reference(self.root, f"samples/{ordinal}.json", item)
            self.campaign["attempts"].append(dict(ordinal=ordinal, run=run_ref, sample=sample_ref, input_evidence=inputs_ref,
                                                   intake_completed_at=self.stamp(ordinal * 2000 + 1100)))
            self.observations[ordinal] = dict(variant=variant, run_id=run["id"], head_sha=run["head_sha"],
                                             workflow_blob_sha=run["workflow_blob_sha"], conditions={"synthetic": "constant"},
                                             platforms=observed_platforms(self.catalogue, ordinal % 2))
        self.refresh_inventory()

    def stamp(self, seconds):
        return (self.origin + timedelta(seconds=seconds)).isoformat()

    def run_fixture(self, ordinal, variant):
        created = ordinal * 2000
        elapsed = 850 if variant == "split-overlap" else 1000
        names = list(github.combination_jobs("split" if variant == "split-overlap" else "combined"))
        # Real upstream timestamp/inventory checks run on these synthetic API records.
        # Native receipt/census replay is covered by the unchanged sample tests.
        step_names = ("Combination matrix (Windows)", "Combination matrix (Linux, macOS)", "Install verified Zig",
                      "Desktop result and reproduction", "Retain desktop logs", "Workflow tool regression tests",
                      "Bootstrap wrapper regression tests", "Execution-mode matrix (Windows)", "Execution-mode matrix",
                      "Native configuration differential matrix", "Test (iOS simulator)", "Test (Android)",
                      "Validate every GitHub workflow", "Require every shard", "Verify every desktop partition exists",
                      "Build compiler and boot both architectures in all allocators",
                      "Exercise analyzer failure and coverage controls", "Compare reference analysis and aggregate all module shards")
        jobs = []
        for index, name in enumerate(names):
            if variant == "split-overlap":
                duration = 40 if index < 26 else 10
            elif name == "Windows x86-64 checks":
                duration = 100 if variant == "combined-overlap" else 90
            else:
                duration = 45 if variant == "combined-overlap" else 48
            end = elapsed if name == "CI complete" else 10 + duration
            jobs.append(dict(id=ordinal * 100 + index, name=name, run_attempt=1, status="completed", conclusion="success",
                             labels=["synthetic"], created_at=self.stamp(created + 1),
                             started_at=self.stamp(created + end - duration), completed_at=self.stamp(created + end),
                             steps=[dict(name=step, status="completed", conclusion="success") for step in step_names]))
        return dict(id=100000 + ordinal, path=".github/workflows/ci.yml", event="workflow_dispatch",
                    head_branch=qualification.COHORT_BRANCHES[qualification.PROSPECTIVE_COHORT][variant],
                    head_sha=self.catalogue["head_sha"], workflow_blob_sha=self.catalogue["workflow_blob_sha"],
                    status="completed", conclusion="success", run_attempt=1, created_at=self.stamp(created), jobs=jobs)

    def input_fixture(self, ordinal, item, run):
        prefix = f"witnesses/{ordinal}"
        desktop_names = github.SPLIT_COMBINATION_PLATFORMS if item["variant"] == "split-overlap" else github.COMBINATION_PLATFORMS
        configure, desktops, jobs = [], [], {}
        for index, job in enumerate(desktop_names):
            phase_directory = f"{prefix}/{index}/matrix-phases"
            (self.root / phase_directory).mkdir(parents=True, exist_ok=True)
            manifest = dict(schema=1, kind="cmake-configure-evidence", role="diagnostic-only", profile_requested=False,
                            profiles_captured=0, errors=[], files=[], tree_count=0, captured_bytes=0,
                            identity=dict(GITHUB_REPOSITORY="buster14a/buster", GITHUB_SHA=run["head_sha"],
                                          GITHUB_RUN_ID=str(run["id"]), GITHUB_RUN_ATTEMPT="1",
                                          RUNNER_OS=job.split(" ", 1)[0], RUNNER_ARCH="X64" if "x86-64" in job else "ARM64",
                                          ImageOS="synthetic", ImageVersion="1"))
            manifest_ref = reference(self.root, f"{prefix}/{index}/configure/manifest.json", manifest)
            configure.append(dict(job=job, manifest=dict(manifest_ref, path=f"{index}/configure/manifest.json")))
            desktops.append(dict(job=job, phase_directory="../" + phase_directory))
            jobs[job] = dict(image_os="synthetic", image_version="1")
        ninja_ref = reference(self.root, f"{prefix}/analyzer/selected-ninja.json", {"synthetic": True})
        jobs["Clang analyzer shards"] = dict(selected_tools=dict(ninja=dict(ninja_ref, path="../" + ninja_ref["path"])))
        conditions_ref = reference(self.root, f"{prefix}/conditions.json", dict(jobs=jobs))
        item.update(desktops=desktops, conditions=dict(conditions_ref, path="../" + conditions_ref["path"]))
        selection = dict(event="workflow_dispatch", requested="false", candidate_revision=run["head_sha"], reference_revision=run["head_sha"],
                         candidate_tree=self.catalogue["source_tree"], reference_tree=self.catalogue["source_tree"],
                         candidate_closure_sha256="a" * 64, reference_closure_sha256="a" * 64,
                         candidate_complete="true", reference_complete="true", candidate_manifest_sha256="b" * 64,
                         reference_manifest_sha256="b" * 64, selection="skip", reason="same-revision")
        path = self.root / prefix / "analyzer/comparison-selection.txt"
        path.write_text(analyzer_reference.SELECTION_SCHEMA + "\n" + "\n".join(key + "=" + selection[key] for key in analyzer_reference.SELECTION_KEYS) + "\n")
        selection_ref = dict(path="analyzer/comparison-selection.txt", sha256=population.digest(path))
        return reference(self.root, prefix + "/inputs.json",
                         dict(schema="buster-ci-checks-population-inputs-v1", configure=configure, analyzer_selection=selection_ref))

    def change_configure(self, index, change):
        wrapper_ref = self.campaign["attempts"][0]["input_evidence"]
        wrapper_path = self.root / wrapper_ref["path"]
        wrapper = json.loads(wrapper_path.read_text())
        manifest_ref = wrapper["configure"][index]["manifest"]
        path = wrapper_path.parent / manifest_ref["path"]
        value = json.loads(path.read_text())
        change(value)
        updated = reference(self.root, str(path.relative_to(self.root)), value)
        updated["path"] = manifest_ref["path"]
        self.replace_record(wrapper_ref, lambda wrapper: wrapper["configure"][index].update(manifest=updated))

    def change_selection(self, change, update_digest=True):
        wrapper_ref = self.campaign["attempts"][0]["input_evidence"]
        wrapper_path = self.root / wrapper_ref["path"]
        wrapper = json.loads(wrapper_path.read_text())
        path = wrapper_path.parent / wrapper["analyzer_selection"]["path"]
        path.write_text(change(path.read_text()), encoding="ascii")
        if update_digest:
            self.replace_record(wrapper_ref, lambda wrapper: wrapper["analyzer_selection"].update(sha256=population.digest(path)))

    def replace_record(self, retained_reference, change):
        path = self.root / retained_reference["path"]
        value = json.loads(path.read_text(encoding="utf-8"))
        change(value)
        refreshed = reference(self.root, str(path.relative_to(self.root)), value)
        retained_reference.update(refreshed)

    def change_declaration(self, change):
        self.replace_record(self.campaign["declaration"], change)
        self.replace_record(self.campaign["publication"],
                            lambda receipt: receipt.update(body=population.marker(self.campaign["declaration"]["sha256"])))

    def refresh_inventory(self, records=None):
        if records is None:
            fields = ("id", "head_sha", "head_branch", "created_at", "run_attempt", "status", "conclusion", "event", "path")
            runs = [json.loads((self.root / attempt["run"]["path"]).read_text()) for attempt in self.campaign["attempts"]]
            records = [{field: run[field] for field in fields} for run in runs]
        pages = []
        for index in range(max(1, (len(records) + 99) // 100)):
            response = reference(self.root, f"inventory/page-{index + 1}.json",
                                 dict(total_count=len(records), workflow_runs=records[index * 100:(index + 1) * 100]))
            pages.append(dict(page=index + 1, response=dict(response, path="../" + response["path"])))
        inventory = dict(schema="buster-ci-checks-dispatch-inventory-v1",
                         endpoint="https://api.github.com/repos/buster14a/buster/actions/workflows/ci.yml/runs",
                         event="workflow_dispatch", created_after=self.stamp(0), per_page=100,
                         collected_at=self.stamp(80000), pages=pages)
        self.campaign["dispatch_inventory"] = reference(self.root, "inventory/manifest.json", inventory)

    def inventory_records(self):
        value = json.loads((self.root / self.campaign["dispatch_inventory"]["path"]).read_text())
        records = []
        for page in value["pages"]:
            path = (self.root / self.campaign["dispatch_inventory"]["path"]).parent / page["response"]["path"]
            records.extend(json.loads(path.read_text())["workflow_runs"])
        return records

    def change_inventory_page(self, index, change):
        manifest_path = self.root / self.campaign["dispatch_inventory"]["path"]
        manifest = json.loads(manifest_path.read_text())
        page_ref = manifest["pages"][index]["response"]
        page_path = (manifest_path.parent / page_ref["path"]).resolve()
        value = json.loads(page_path.read_text())
        change(value)
        updated = reference(self.root, str(page_path.relative_to(self.root)), value)
        updated["path"] = "../" + updated["path"]
        self.replace_record(self.campaign["dispatch_inventory"], lambda value: value["pages"][index].update(response=updated))

    def unrelated_dispatches(self, count):
        records = []
        for index in range(count):
            run = self.run_fixture(1, "combined-overlap")
            fields = ("id", "head_sha", "head_branch", "created_at", "run_attempt", "status", "conclusion", "event", "path")
            record = {field: run[field] for field in fields}
            record.update(id=200000 + index, head_branch="unrelated-dispatch", created_at=self.stamp(1000 + index))
            records.append(record)
        return records

    def report(self):
        if not self.fixed_inventory:
            self.refresh_inventory()
        path = self.root / "campaign.json"
        reference(self.root, path.name, self.campaign)
        def observed(root, item, cohort_name):
            self.assertEqual(cohort_name, qualification.PROSPECTIVE_COHORT)
            return copy.deepcopy(self.observations[item["synthetic_id"]])
        with mock.patch.object(qualification, "sample", side_effect=observed):
            result = population.qualify(path)
        return result

    def assert_pending_error(self, report, stop_ordinal=None):
        self.assertEqual(report["status"], "pending")
        self.assertEqual(report["timing_status"], "pending")
        self.assertFalse(report["performance_accepted"])
        self.assertTrue(report["errors"])
        if stop_ordinal is not None:
            self.assertEqual(report["stop_ordinal"], stop_ordinal)

    def test_prepare_freezes_exact_bindings_and_never_overwrites(self):
        declaration, catalogue = population.read_declaration(self.root, self.declaration)
        prepared = population.prepare(self.root / "second-declaration.json")
        self.assertFalse(prepared["performance_accepted"])
        self.assertFalse(prepared["sampling_authorized"])
        self.assertEqual(declaration["reader_sha256"], population.reader_bindings())
        self.assertEqual(declaration["catalogue"]["sha256"], population.digest(CATALOGUE))
        self.assertEqual(catalogue, self.catalogue)
        self.assertEqual(declaration["blocks"], ["CAB", "ACB", "CBA", "BAC", "BCA", "ABC", "ABC", "CBA", "ACB", "BCA", "BAC", "CAB"])
        self.assertEqual(declaration["excluded_runs"], [37191738110, 37193669465])
        frozen_digest = population.digest(self.root / "declaration.json")
        with self.assertRaisesRegex(ValueError, "already exists"):
            population.prepare(self.root / "declaration.json")
        self.assertEqual(population.digest(self.root / "declaration.json"), frozen_digest)

    def test_complete_heterogeneous36_retains_profiles_and_exact_ratio_boundaries(self):
        report = self.report()
        self.assertEqual(report["errors"], [])
        self.assertEqual(report["timing_status"], "accepted")
        self.assertTrue(report["timing_contract_met"])
        self.assertEqual(report["status"], "pending")
        self.assertFalse(report["performance_accepted"])
        self.assertEqual(len(report["samples"]), 36)
        self.assertEqual(len(report["dispatches"]), 36)
        for sample in report["samples"]:
            self.assertIs(sample["dispatch_inputs"]["cmake_profile"], False)
            self.assertIs(sample["dispatch_inputs"]["analyzer_comparison"], False)
        self.assertEqual(report["issues"]["2119"]["time_ratio"], .90)
        self.assertEqual(report["issues"]["2120"]["time_ratio"], .85)
        self.assertTrue(all(issue["runner_seconds_ratio"] == 1.05 for issue in report["issues"].values()))
        self.assertTrue(report["pending_reviews"])
        heterogeneous = [rows for rows in report["native_populations"].values() if len(rows) > 1]
        self.assertTrue(heterogeneous)
        for profiles in report["native_populations"].values():
            self.assertEqual(sum(sum(profile["variants"].values()) for profile in profiles), 36)

    def test_each_timing_threshold_rejects_a_complete_campaign_above_boundary(self):
        for variant, job_name in (("combined-all-builds", "Windows x86-64 checks"),
                                  ("split-overlap", "CI complete"), ("split-overlap", "Workflow lint")):
            saved = copy.deepcopy(self.campaign)
            for attempt in self.campaign["attempts"]:
                ordinal = attempt["ordinal"]
                if self.observations[ordinal]["variant"] == variant:
                    def change(run):
                        job = next(job for job in run["jobs"] if job["name"] == job_name)
                        finish = github.timestamp(job["completed_at"]) + timedelta(seconds=.1)
                        job["completed_at"] = finish.isoformat()
                    self.replace_record(attempt["run"], change)
                    sample_ref = attempt["sample"]
                    self.replace_record(sample_ref, lambda item: item["run"].update(sha256=attempt["run"]["sha256"]))
            with self.subTest(variant=variant, job=job_name):
                report = self.report()
                self.assertEqual(report["errors"], [])
                self.assertEqual(report["status"], "rejected")
                self.assertEqual(report["timing_status"], "rejected")
                self.assertFalse(report["performance_accepted"])
            self.campaign = saved
            # Restore bytes whose digest was changed by the control.
            for attempt in self.campaign["attempts"]:
                ordinal = attempt["ordinal"]
                run = self.run_fixture(ordinal, self.observations[ordinal]["variant"])
                attempt["run"] = reference(self.root, f"runs/{ordinal}.json", run)
                self.replace_record(attempt["sample"], lambda item: item.update(run=dict(attempt["run"], path=f"../runs/{ordinal}.json")))

    def test_ratio_of_variant_medians_is_not_median_of_paired_ratios(self):
        observations = []
        for variant in qualification.VARIANTS:
            for index in range(12):
                windows = {"combined-overlap": 70 if index < 6 else 130,
                           "combined-all-builds": 81 if index < 6 else 99,
                           "split-overlap": 0}[variant]
                timing = dict(elapsed_seconds=850 if variant == "split-overlap" else 1000,
                              runner_seconds=1000 if variant == "combined-overlap" else 1050)
                if variant != "split-overlap":
                    timing["windows_checks_seconds"] = windows
                observations.append(dict(variant=variant, timing=timing))
        summary, issues = population.timing_verdict(observations)
        self.assertEqual(summary["combined-overlap"]["windows_checks_seconds"]["median"], 100)
        self.assertEqual(summary["combined-all-builds"]["windows_checks_seconds"]["median"], 90)
        self.assertEqual(issues["2119"]["time_ratio"], .90)
        self.assertGreater((81 / 70 + 99 / 130) / 2, .90)
        self.assertEqual(issues["2119"]["timing_status"], "accepted")

    def test35_sample_prefix_is_pending_and_preserves_every_observation(self):
        self.campaign["attempts"].pop()
        report = self.report()
        self.assert_pending_error(report)
        self.assertEqual(len(report["samples"]), 35)
        self.assertEqual(len(report["dispatches"]), 35)
        self.assertTrue(report["native_populations"])

    def test_more_than36_dispatches_exceeds_frozen_budget(self):
        extra = copy.deepcopy(self.campaign["attempts"][-1])
        extra["ordinal"] = 37
        self.campaign["attempts"].append(extra)
        report = self.report()
        self.assert_pending_error(report)
        self.assertEqual(len(report["dispatches"]), 37)
        self.assertEqual(report["samples"], [])

    def test_exhaustive_inventory_rejects_omitted_failure_and_renumbered_successful_subset(self):
        records = self.inventory_records()
        records[7]["conclusion"] = "failure"
        self.refresh_inventory(records)
        self.fixed_inventory = True
        self.campaign["attempts"].pop(7)
        for ordinal, attempt in enumerate(self.campaign["attempts"], 1):
            attempt["ordinal"] = ordinal
        report = self.report()
        self.assert_pending_error(report)
        self.assertIn("actual dispatch history", report["errors"][0])
        self.assertEqual(report["samples"], [])

    def test_inventory_cannot_select36_from37_actual_campaign_dispatches(self):
        records = self.inventory_records()
        extra = copy.deepcopy(records[-1])
        extra.update(id=100037, created_at=self.stamp(74000), conclusion="cancelled")
        self.refresh_inventory(records + [extra])
        self.fixed_inventory = True
        report = self.report()
        self.assert_pending_error(report)
        self.assertIn("actual dispatch history", report["errors"][0])
        self.assertEqual(report["samples"], [])

    def test_every_inventory_metadata_field_must_match_retained_attempt(self):
        records = self.inventory_records()
        for field in ("head_sha", "head_branch", "run_attempt", "status", "conclusion", "created_at"):
            altered = copy.deepcopy(records)
            replacement = {"head_sha": "0" * 40, "head_branch": qualification.COHORT_BRANCHES[qualification.PROSPECTIVE_COHORT]["split-overlap"], "run_attempt": 2, "status": "in_progress",
                           "conclusion": "failure", "created_at": self.stamp(4200)}[field]
            altered[1][field] = replacement
            self.refresh_inventory(altered)
            self.fixed_inventory = True
            with self.subTest(field=field):
                report = self.report()
                self.assert_pending_error(report)
                self.assertIn("differs from retained attempt", report["errors"][0])

    def test_complete_paginated_inventory_includes_other_refs_without_counting_them(self):
        records = self.inventory_records()
        self.refresh_inventory(records + self.unrelated_dispatches(105))
        self.fixed_inventory = True
        report = self.report()
        self.assertEqual(report["errors"], [])
        self.assertEqual(report["timing_status"], "accepted")
        self.assertEqual(report["dispatch_inventory"]["api_runs"], 141)
        self.assertEqual(report["dispatch_inventory"]["campaign_dispatches"], 36)
        self.assertEqual(report["dispatch_inventory"]["complete_pages"], 2)

    def test_original_raw_api_json_without_final_newline_is_supported(self):
        receipt_ref = self.campaign["publication"]
        receipt_path = self.root / receipt_ref["path"]
        receipt_path.write_bytes(receipt_path.read_bytes().rstrip(b"\n"))
        receipt_ref["sha256"] = population.digest(receipt_path)
        manifest_ref = self.campaign["dispatch_inventory"]
        self.fixed_inventory = True
        def change(manifest):
            page_ref = manifest["pages"][0]["response"]
            page_path = (self.root / manifest_ref["path"]).parent / page_ref["path"]
            page_path.write_bytes(page_path.read_bytes().rstrip(b"\n"))
            page_ref["sha256"] = population.digest(page_path)
        self.replace_record(manifest_ref, change)
        report = self.report()
        self.assertEqual(report["errors"], [])
        self.assertEqual(report["timing_status"], "accepted")

    def test_incomplete_reordered_duplicate_and_unstable_api_pages_fail_closed(self):
        records = self.inventory_records() + self.unrelated_dispatches(105)
        for change in ("missing-page", "reordered-page", "duplicate-page", "truncated-page", "changed-total", "duplicate-run"):
            self.refresh_inventory(records)
            self.fixed_inventory = True
            if change == "missing-page":
                self.replace_record(self.campaign["dispatch_inventory"], lambda value: value["pages"].pop())
            elif change == "reordered-page":
                self.replace_record(self.campaign["dispatch_inventory"], lambda value: value["pages"].reverse())
            elif change == "duplicate-page":
                self.replace_record(self.campaign["dispatch_inventory"], lambda value: value["pages"].__setitem__(1, copy.deepcopy(value["pages"][0])))
            elif change == "truncated-page":
                self.change_inventory_page(0, lambda page: page["workflow_runs"].pop())
            elif change == "changed-total":
                self.change_inventory_page(1, lambda page: page.update(total_count=142))
            else:
                self.change_inventory_page(1, lambda page: page["workflow_runs"][0].update(id=records[0]["id"]))
            with self.subTest(change=change):
                report = self.report()
                self.assert_pending_error(report)
                self.assertEqual(report["samples"], [])

    def test_inventory_capture_scope_limit_and_time_are_mandatory(self):
        records = self.inventory_records()
        for change in ("api-limit", "before-intake", "scope", "empty-pages", "outside-scope"):
            self.refresh_inventory(records)
            self.fixed_inventory = True
            if change == "api-limit":
                self.refresh_inventory(records + self.unrelated_dispatches(965))
            elif change == "before-intake":
                self.replace_record(self.campaign["dispatch_inventory"], lambda value: value.update(collected_at=self.stamp(73000)))
            elif change == "scope":
                self.replace_record(self.campaign["dispatch_inventory"], lambda value: value.update(created_after=self.stamp(1)))
            elif change == "empty-pages":
                self.replace_record(self.campaign["dispatch_inventory"], lambda value: value.update(pages=[]))
            else:
                self.change_inventory_page(0, lambda page: page["workflow_runs"][0].update(event="push"))
            with self.subTest(change=change):
                report = self.report()
                self.assert_pending_error(report)
                self.assertEqual(report["samples"], [])

    def test_api_cap_boundary_cannot_establish_exhaustive_history(self):
        campaign_records = self.inventory_records()
        self.refresh_inventory(campaign_records + self.unrelated_dispatches(963))
        self.fixed_inventory = True
        report = self.report()
        self.assertEqual(report["errors"], [])
        self.assertEqual(report["timing_status"], "accepted")
        self.assertEqual(report["dispatch_inventory"]["api_runs"], 999)
        self.assertEqual(report["dispatch_inventory"]["complete_pages"], 10)
        self.refresh_inventory(campaign_records + self.unrelated_dispatches(964))
        report = self.report()
        self.assert_pending_error(report)
        self.assertEqual(report["samples"], [])
        self.assertIn("API cap", report["errors"][0])

    def test_original_A1_B1_and_duplicate_runs_never_enter_replacement_epoch(self):
        for run_id in (37191738110, 37193669465, 100001):
            self.replace_record(self.campaign["attempts"][1]["run"], lambda run: run.update(id=run_id))
            with self.subTest(run_id=run_id):
                report = self.report()
                self.assert_pending_error(report, None if run_id == 100001 else 2)
                self.assertEqual(len(report["samples"]), 0 if run_id == 100001 else 1)

    def test_missing_ordinal_or_wrong_variant_stops_at_first_invalid_slot(self):
        for change in ("missing-ordinal", "duplicate-ordinal", "wrong-variant"):
            saved = copy.deepcopy(self.campaign)
            attempt = self.campaign["attempts"][1]
            if change == "missing-ordinal":
                del attempt["ordinal"]
            elif change == "duplicate-ordinal":
                attempt["ordinal"] = 1
            else:
                self.replace_record(attempt["run"], lambda run: run.update(head_branch=qualification.COHORT_BRANCHES[qualification.PROSPECTIVE_COHORT]["split-overlap"]))
            with self.subTest(change=change):
                report = self.report()
                self.assert_pending_error(report, 2)
            self.campaign = saved

    def test_reruns_failures_and_cancellations_stop_before_later_dispatches(self):
        for change in (dict(run_attempt=2), dict(conclusion="failure"), dict(conclusion="cancelled"),
                       dict(status="in_progress", conclusion=None)):
            self.replace_record(self.campaign["attempts"][7]["run"], lambda run: run.update({"run_attempt": 1, "status": "completed", "conclusion": "success", **change}))
            with self.subTest(change=change):
                report = self.report()
                self.assert_pending_error(report, 8)
                self.assertEqual(len(report["samples"]), 7)
                self.assertEqual(report["dispatches_after_stop"], list(range(9, 37)))

    def test_missing_intake_and_samples_cannot_be_replaced_by_later_successes(self):
        for field in ("intake_completed_at", "sample"):
            saved = self.campaign["attempts"][1][field]
            self.campaign["attempts"][1][field] = None
            with self.subTest(field=field):
                report = self.report()
                self.assert_pending_error(report, 2)
                self.assertEqual(len(report["samples"]), 1)
            self.campaign["attempts"][1][field] = saved

    def test_overlap_and_late_archive_intake_block_next_dispatch(self):
        for intake_seconds in (self.stamp(2849), self.stamp(4001)):
            self.campaign["attempts"][0]["intake_completed_at"] = intake_seconds
            with self.subTest(intake=intake_seconds):
                report = self.report()
                self.assert_pending_error(report, 1 if intake_seconds == self.stamp(2849) else 2)
        self.campaign["attempts"][0]["intake_completed_at"] = self.stamp(3100)
        self.replace_record(self.campaign["attempts"][1]["run"], lambda run: run.update(created_at=self.stamp(2849)))
        self.assert_pending_error(self.report(), 2)

    def test_same_second_completion_or_intake_cannot_prove_next_dispatch_order(self):
        second = self.campaign["attempts"][1]
        for boundary in (2850, 4000):
            self.campaign["attempts"][0]["intake_completed_at"] = self.stamp(boundary)
            self.replace_record(second["run"], lambda run: run.update(created_at=self.stamp(boundary)))
            self.replace_record(second["sample"], lambda item: item["run"].update(sha256=second["run"]["sha256"]))
            with self.subTest(boundary=boundary, relation="same-second"):
                self.assert_pending_error(self.report(), 2)
            self.replace_record(second["run"], lambda run: run.update(created_at=self.stamp(boundary + 1)))
            self.replace_record(second["sample"], lambda item: item["run"].update(sha256=second["run"]["sha256"]))
            with self.subTest(boundary=boundary, relation="later-second"):
                report = self.report()
                self.assertEqual(report["errors"], [])
                self.assertEqual(report["timing_status"], "accepted")

    def test_metadata_tail_is_excluded_from_metric_but_not_chronology(self):
        # Parser consistency control only: frozen workflow dependencies normally
        # finish this metadata job before the required workloads.
        run = self.run_fixture(1, "split-overlap")
        run["jobs"].append(dict(id=199, name=github.MAIN_REUSE_JOB, run_attempt=1, status="completed", conclusion="success",
                                created_at=self.stamp(2001), started_at=self.stamp(3400), completed_at=self.stamp(3500), steps=[]))
        timing, end = population.job_timing(run, "split-overlap")
        self.assertEqual(timing["elapsed_seconds"], 850)
        self.assertEqual(timing["runner_seconds"], 1150)
        self.assertEqual(end, github.timestamp(self.stamp(3500)))
        self.campaign["attempts"][0]["run"] = reference(self.root, "runs/1.json", run)
        self.assert_pending_error(self.report(), 1)

    def test_declaration_receipt_must_precede_first_dispatch_and_remain_original(self):
        for change in (dict(created_at=self.stamp(2000), updated_at=self.stamp(2000)),
                       dict(created_at=self.stamp(2001), updated_at=self.stamp(2001)),
                       dict(created_at=self.stamp(0), updated_at=self.stamp(1)),
                       dict(issue_url="https://api.github.com/repos/buster14a/buster/issues/2120"),
                       dict(body="CI_CHECKS_POPULATION_DECLARATION_V1 sha256=" + "0" * 64)):
            saved = copy.deepcopy(self.campaign["publication"])
            original = json.loads((self.root / saved["path"]).read_text())
            self.replace_record(self.campaign["publication"], lambda receipt: receipt.update(change))
            with self.subTest(change=change):
                self.assert_pending_error(self.report())
            self.campaign["publication"] = reference(self.root, saved["path"], original)

    def test_publication_marker_must_be_unique_and_original_retained_bytes_match_digest(self):
        receipt_ref = self.campaign["publication"]
        self.replace_record(receipt_ref, lambda receipt: receipt.update(body=receipt["body"] + "\n" + receipt["body"]))
        self.assert_pending_error(self.report())
        self.replace_record(receipt_ref, lambda receipt: receipt.update(body=receipt["body"].splitlines()[0]))
        path = self.root / receipt_ref["path"]
        path.write_text(path.read_text() + " ", encoding="utf-8")
        report = self.report()
        self.assert_pending_error(report)
        self.assertIn("digest mismatch", report["errors"][0])

    def test_declaration_source_tree_workflow_producer_catalogue_reader_pins_are_enforced(self):
        for field in ("head_sha", "workflow_blob_sha", "source_tree", "producer_blobs", "input_producer_blobs", "reader_sha256", "catalogue", "blocks", "seed", "excluded_runs"):
            original = json.loads((self.root / self.campaign["declaration"]["path"]).read_text())
            def change(declaration):
                if field in ("head_sha", "workflow_blob_sha"):
                    declaration["cohort"][field] = "0" * 40
                elif field in ("producer_blobs", "input_producer_blobs", "reader_sha256"):
                    key = next(iter(declaration[field]))
                    declaration[field][key] = "0" * len(declaration[field][key])
                elif field == "catalogue":
                    declaration[field]["sha256"] = "0" * 64
                elif field == "blocks":
                    declaration[field].reverse()
                elif field == "excluded_runs":
                    declaration[field] = []
                else:
                    declaration[field] = "0" * len(declaration[field])
            self.change_declaration(change)
            with self.subTest(field=field):
                self.assert_pending_error(self.report())
            self.change_declaration(lambda declaration: (declaration.clear(), declaration.update(original)))

    def test_declared_optional_inputs_require_literal_false_for_both_flags(self):
        original = json.loads((self.root / self.campaign["declaration"]["path"]).read_text())
        for flag in ("cmake_profile", "analyzer_comparison"):
            for value in (True, 0, None, "false"):
                self.change_declaration(lambda declaration: declaration["dispatch_inputs"].update({flag: value}))
                with self.subTest(flag=flag, value=value):
                    self.assert_pending_error(self.report())
                self.change_declaration(lambda declaration: (declaration.clear(), declaration.update(original)))

    def test_configure_witness_flags_types_and_capture_errors_are_enforced(self):
        path = self.root / "witnesses/1/0/configure/manifest.json"
        original = json.loads(path.read_text())
        changes = (dict(profile_requested=True), dict(profile_requested=0), dict(profile_requested=None),
                   dict(profile_requested="false"), dict(profiles_captured=1), dict(profiles_captured=True),
                   dict(profiles_captured=0.0), dict(errors=["retention failure"]), dict(errors=None),
                   dict(schema=True), dict(kind="other"), dict(role="other"))
        for change in changes:
            self.change_configure(0, lambda value: value.update(change))
            with self.subTest(change=change):
                self.assert_pending_error(self.report(), 1)
            self.change_configure(0, lambda value: (value.clear(), value.update(original)))

    def test_configure_witness_has_exact_source_run_attempt_repository_and_runner_identity(self):
        path = self.root / "witnesses/1/0/configure/manifest.json"
        original = json.loads(path.read_text())
        replacements = dict(GITHUB_REPOSITORY="other/repository", GITHUB_SHA="0" * 40, GITHUB_RUN_ID="9999", GITHUB_RUN_ATTEMPT="2",
                            RUNNER_OS="other", RUNNER_ARCH="other", ImageOS="other", ImageVersion="other")
        for field, value in replacements.items():
            self.change_configure(0, lambda receipt: receipt["identity"].update({field: value}))
            with self.subTest(field=field):
                self.assert_pending_error(self.report(), 1)
            self.change_configure(0, lambda value: (value.clear(), value.update(original)))
        self.change_configure(0, lambda receipt: receipt["identity"].update(GITHUB_RUN_ID=100001))
        self.assert_pending_error(self.report(), 1)

    def test_configure_witness_requires_every_exact_desktop_artifact_sibling(self):
        wrapper_ref = self.campaign["attempts"][0]["input_evidence"]
        original = json.loads((self.root / wrapper_ref["path"]).read_text())
        for change in ("missing", "duplicate", "extra", "foreign-sibling", "wrong-schema"):
            def mutate(value):
                if change == "missing":
                    value["configure"].pop()
                elif change == "duplicate":
                    value["configure"].append(copy.deepcopy(value["configure"][0]))
                elif change == "extra":
                    value["configure"].append(dict(value["configure"][0], job="unrelated desktop"))
                elif change == "foreign-sibling":
                    value["configure"][1]["manifest"] = copy.deepcopy(value["configure"][0]["manifest"])
                else:
                    value["schema"] = "other"
            self.replace_record(wrapper_ref, mutate)
            with self.subTest(change=change):
                self.assert_pending_error(self.report(), 1)
            self.replace_record(wrapper_ref, lambda value: (value.clear(), value.update(original)))

    def test_missing_or_tampered_input_receipts_stop_epoch(self):
        wrapper_ref = self.campaign["attempts"][0]["input_evidence"]
        original = json.loads((self.root / wrapper_ref["path"]).read_text())
        for change in ("missing-wrapper", "missing-configure", "configure-digest", "analyzer-digest"):
            if change == "missing-wrapper":
                self.campaign["attempts"][0]["input_evidence"] = None
            elif change == "missing-configure":
                self.replace_record(wrapper_ref, lambda value: value["configure"][0]["manifest"].update(path="missing.json"))
            elif change == "configure-digest":
                self.replace_record(wrapper_ref, lambda value: value["configure"][0]["manifest"].update(sha256="0" * 64))
            else:
                self.replace_record(wrapper_ref, lambda value: value["analyzer_selection"].update(sha256="0" * 64))
            with self.subTest(change=change):
                self.assert_pending_error(self.report(), 1)
            self.campaign["attempts"][0]["input_evidence"] = wrapper_ref
            self.replace_record(wrapper_ref, lambda value: (value.clear(), value.update(original)))

    def test_analyzer_selection_semantics_source_and_tree_are_observed_false(self):
        original = (self.root / "witnesses/1/analyzer/comparison-selection.txt").read_text()
        replacements = dict(event="push", requested="true", selection="compare", reason="requested",
                            candidate_revision="0" * 40, reference_revision="0" * 40,
                            candidate_tree="0" * 40, reference_tree="0" * 40,
                            candidate_complete="false", reference_complete="false",
                            candidate_closure_sha256="0" * 64, reference_closure_sha256="0" * 64,
                            candidate_manifest_sha256="0" * 64, reference_manifest_sha256="0" * 64)
        for field, value in replacements.items():
            def change(text):
                lines = [field + "=" + value if line.startswith(field + "=") else line for line in text.splitlines()]
                return "\n".join(lines) + "\n"
            self.change_selection(change)
            with self.subTest(field=field):
                self.assert_pending_error(self.report(), 1)
            self.change_selection(lambda text: original)

    def test_analyzer_selection_sibling_and_native_parser_framing_are_enforced(self):
        wrapper_ref = self.campaign["attempts"][0]["input_evidence"]
        wrapper = json.loads((self.root / wrapper_ref["path"]).read_text())
        original = (self.root / "witnesses/1/analyzer/comparison-selection.txt").read_text()
        copy_path = self.root / "witnesses/1/copied-selection.txt"
        copy_path.write_text(original)
        self.replace_record(wrapper_ref, lambda value: value["analyzer_selection"].update(path="copied-selection.txt", sha256=population.digest(copy_path)))
        self.assert_pending_error(self.report(), 1)
        self.replace_record(wrapper_ref, lambda value: (value.clear(), value.update(wrapper)))
        for change in (lambda text: text.rstrip("\n"), lambda text: text + "unexpected=field\n",
                       lambda text: text.replace("requested=false", "requested=0"), lambda text: "malformed\n"):
            self.change_selection(change)
            self.assert_pending_error(self.report(), 1)
            self.change_selection(lambda text: original)

    def test_every_sample_is_bound_to_run_source_workflow_and_conditions(self):
        for field in ("run_id", "head_sha", "workflow_blob_sha", "conditions"):
            original = copy.deepcopy(self.observations[8])
            self.observations[8][field] = 999999 if field == "run_id" else {"changed": True} if field == "conditions" else "0" * 40
            with self.subTest(field=field):
                self.assert_pending_error(self.report(), 8)
            self.observations[8] = original
        for observed in self.observations.values():
            observed["conditions"] = {"synthetic_boolean": True}
        self.observations[8]["conditions"] = {"synthetic_boolean": 1}
        self.assert_pending_error(self.report(), 8)

    def test_unknown_profile_stops_epoch_and_retains_full_observed_census(self):
        observation = self.observations[1]
        platform = next(iter(observation["platforms"].values()))
        census = next(iter(platform["census"].values()))
        census["native_host_profile"]["feature_words"][0] ^= 1
        report = self.report()
        self.assert_pending_error(report, 1)
        self.assertEqual(len(report["samples"]), 1)
        self.assertEqual(report["samples"][0]["platforms"], observation["platforms"])
        self.assertTrue(report["native_populations"])
        self.assertEqual(report["dispatches_after_stop"], list(range(2, 37)))


if __name__ == "__main__":
    unittest.main()
