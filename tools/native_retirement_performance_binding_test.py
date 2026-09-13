#!/usr/bin/env python3
"""Offline tests for the fail-closed native-retirement binding validator."""

import copy
import csv
import hashlib
import importlib.util
import io
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location(
    "native_retirement_performance_binding",
    ROOT / "tools" / "native_retirement_performance_binding.py")
binding = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(binding)


class BindingTests(unittest.TestCase):
    """Exercise structural checks and one complete #508-shaped evidence set."""

    _full_record = None
    _full_contents = None

    @staticmethod
    def _artifact(contents, path, data):
        if isinstance(data, str):
            data = data.encode("utf-8")
        contents[path] = data
        return {"path": path, "bytes": len(data),
                "sha256": hashlib.sha256(data).hexdigest()}

    @staticmethod
    def _tsv(fields, rows):
        stream = io.StringIO(newline="")
        writer = csv.DictWriter(stream, fieldnames=fields, delimiter="\t",
                                lineterminator="\n")
        writer.writeheader()
        writer.writerows(rows)
        return stream.getvalue().encode("utf-8")

    @classmethod
    def _build_record(cls, full=False):
        contents = {}
        artifact = lambda path, data: cls._artifact(contents, path, data)

        if full:
            declaration_data = (ROOT / binding.SUPPORT_DECLARATION_PATH).read_bytes()
            declaration = list(csv.DictReader(
                io.StringIO(declaration_data.decode("utf-8")), delimiter="\t"))
            fixtures = [row["path"] for row in declaration if row["role"] == "subject"]
            targets = [
                "x86_64-unknown-linux-gnu", "aarch64-unknown-linux-gnu",
                "x86_64-pc-windows-msvc", "aarch64-pc-windows-msvc",
                "x86_64-apple-macos", "aarch64-apple-macos",
                "x86_64-linux-android", "aarch64-linux-android",
                "x86_64-apple-ios", "aarch64-apple-ios",
                "x86_64-unknown-uefi", "aarch64-unknown-uefi",
            ]
        else:
            declaration = [{
                "path": "tests/basic_c_operations.c", "role": "subject",
                "compile_obligation": "supported-object-zero-fallback", "bytes": "1",
                "sha256": "a" * 64,
            }]
            declaration_data = cls._tsv(binding.SUPPORT_DECLARATION_FIELDS, declaration)
            fixtures = ["tests/basic_c_operations.c"]
            targets = ["x86_64-unknown-linux-gnu", "aarch64-unknown-linux-gnu"]
        declaration_artifact = artifact(binding.SUPPORT_DECLARATION_PATH, declaration_data)

        input_rows = []
        for row in declaration:
            input_rows.append({
                "path": row["path"], "role": row["role"],
                "compile_obligation": row["compile_obligation"], "bytes": row["bytes"],
                "buster_hash_64": "1", "sha256": row["sha256"],
                "fixture_recipe": "compiler-default", "fixture_flags": "",
            })
        inputs_data = cls._tsv(binding.INPUT_FIELDS, input_rows)
        inputs_artifact = artifact("census/inputs.tsv", inputs_data)

        census_rows = []
        row_number = 0
        for fixture in fixtures:
            for target in targets:
                for frontend in binding.FRONTENDS:
                    for pic in binding.PIC:
                        for allocator in binding.ALLOCATORS:
                            group = row_number // len(binding.ALLOCATORS)
                            census_rows.append({
                                "row": str(row_number), "group": str(group),
                                "fixture": fixture, "target": target,
                                "target_abi": binding.TARGET_ABIS[target], "cpu": "baseline",
                                "cpu_features": "baseline", "allocator": allocator,
                                "frontend_lowering": frontend, "PIC": pic, "selected": "1",
                                "fixture_recipe": "compiler-default",
                                "compile_obligation": "supported-object-zero-fallback",
                                "link_obligation": "semantic-gate-509",
                                "execution_obligation": "semantic-gate-509",
                                "diagnostic_obligation": "none",
                                "argv_evidence": f"groups/{group}/{allocator}.argv",
                            })
                            row_number += 1
        rows_data = cls._tsv(binding.ROW_FIELDS, census_rows)
        rows_artifact = artifact("census/rows.tsv", rows_data)
        baseline_binary = artifact("subjects/baseline-ide", "baseline binary\n")
        candidate_binary = artifact("subjects/candidate-ide", "candidate binary\n")

        dependencies_data = cls._tsv(binding.DEPENDENCY_FIELDS, [{
            "kind": "resource-header", "path": "include/header.h", "bytes": "1",
            "sha256": "b" * 64,
        }])
        dependencies_artifact = artifact("census/dependencies.tsv", dependencies_data)
        environment_rows = [
            {"name": "LC_ALL", "present": "1", "value": "C"},
            {"name": "LANG", "present": "1", "value": "C"},
            {"name": "TZ", "present": "1", "value": "UTC"},
        ]
        environment_data = cls._tsv(binding.ENVIRONMENT_FIELDS, environment_rows)
        environment_artifact = artifact("census/environment.tsv", environment_data)

        revision_a = "a" * 40
        revision_b = "b" * 40
        manifest_data = (
            "version=2\nkind=object-coverage\nidentity_hash=sha256\n"
            f"support_contract={binding.SUPPORT_DECLARATION_PATH}\n"
            f"support_contract_sha256={declaration_artifact['sha256']}\n"
            f"inputs={len(input_rows)}\nrows={len(census_rows)}\nmanifest_only=0\n"
            "environment=explicit-replacement-in-environment.tsv\n"
            "unfrozen_dependencies=none-for-object-census\n"
            "sysroot=none\nsystem_include=none\n"
            f"resource_include_sha256={'c' * 64}\n"
            f"compiler_hash=1\ncompiler_bytes={candidate_binary['bytes']}\n"
            f"compiler_sha256={candidate_binary['sha256']}\n"
            f"baseline_hash=2\nbaseline_bytes={baseline_binary['bytes']}\n"
            f"baseline_sha256={baseline_binary['sha256']}\n"
            "cpu=baseline\n"
            f"compiler_revision_claim={revision_a}\nbaseline_revision_claim={revision_b}\n"
        ).encode("utf-8")
        manifest_artifact = artifact("census/manifest.txt", manifest_data)

        validator_source = artifact("tools/native_retirement_contract.py",
                                    "independent validator source\n")
        validator_commit = "7" * 40
        validator_tree = "8" * 40
        report_data = {
            "schema": binding.VALIDATOR_REPORT_SCHEMA,
            "version": binding.VALIDATOR_REPORT_VERSION,
            "success": True,
            "validator_source_commit": validator_commit,
            "validator_source_tree": validator_tree,
            "support_declaration_sha256": declaration_artifact["sha256"],
            "manifest_sha256": manifest_artifact["sha256"],
            "inputs_sha256": inputs_artifact["sha256"],
            "rows_sha256": rows_artifact["sha256"],
            "dependencies_sha256": dependencies_artifact["sha256"],
            "environment_sha256": environment_artifact["sha256"],
            "inputs": len(input_rows), "groups": len(census_rows) // len(binding.ALLOCATORS),
            "rows": len(census_rows),
        }
        report_bytes = (json.dumps(report_data, sort_keys=True, separators=(",", ":"))
                        + "\n").encode("utf-8")
        validator_report_artifact = artifact("census/validator-report.json", report_bytes)

        support_file_artifacts = {
            "support_declaration": declaration_artifact, "manifest": manifest_artifact,
            "inputs": inputs_artifact, "rows": rows_artifact,
            "dependencies": dependencies_artifact, "environment": environment_artifact,
            "validator_report": validator_report_artifact,
        }
        row_sources = {role: support_file_artifacts[role]["sha256"]
                       for role in binding.ROW_SOURCE_ROLES}
        performance_rows = []
        for census_row in census_rows:
            identity = {field: census_row[field] for field in binding.ROW_IDENTITY_FIELDS
                        if field != "artifact_stage"}
            stage = "object"
            performance_rows.append({
                "row": len(performance_rows),
                "identity": {**identity, "artifact_stage": stage},
                "eligibility": {
                    "compiler_wall_time": True, "compiler_peak_rss": True,
                    "generated_code_bytes": True, "generated_runtime": stage != "object",
                    "runtime_oracle": ("independent-native-executable-oracle"
                                        if stage != "object" else "not-applicable"),
                    "code_section": "deterministic-code-section",
                },
            })
        # #508 emits object rows.  The performance declaration adds one
        # representative link and self-host stage so the stage axis is
        # explicit without inflating this offline fixture threefold.
        for stage, census_row in zip(binding.STAGES[1:], census_rows[:2]):
            identity = {field: census_row[field] for field in binding.ROW_IDENTITY_FIELDS
                        if field != "artifact_stage"}
            performance_rows.append({
                "row": len(performance_rows),
                "identity": {**identity, "artifact_stage": stage},
                "eligibility": {
                    "compiler_wall_time": True, "compiler_peak_rss": True,
                    "generated_code_bytes": True, "generated_runtime": True,
                    "runtime_oracle": "independent-native-executable-oracle",
                    "code_section": "deterministic-code-section",
                },
            })
        rows_record = {
            "schema": binding.ROW_SCHEMA, "version": binding.ROW_VERSION,
            "row_identity_fields": list(binding.ROW_IDENTITY_FIELDS),
            "sources": row_sources, "rows": performance_rows,
        }
        performance_rows_data = (
            json.dumps(rows_record, sort_keys=True, separators=(",", ":"),
                       ensure_ascii=False) + "\n").encode("utf-8")
        parsed_rows, axes, family = binding._performance_rows(performance_rows_data)
        performance_rows_artifact = artifact("performance/rows.json", performance_rows_data)
        performance_data = {
            "schema": binding.PERFORMANCE_DECLARATION_SCHEMA,
            "version": binding.PERFORMANCE_DECLARATION_VERSION,
            "support_declaration_sha256": declaration_artifact["sha256"],
            "manifest_sha256": manifest_artifact["sha256"],
            "inputs_sha256": inputs_artifact["sha256"], "rows_sha256": rows_artifact["sha256"],
            "dependencies_sha256": dependencies_artifact["sha256"],
            "environment_sha256": environment_artifact["sha256"],
            "validator_report_sha256": validator_report_artifact["sha256"],
            "performance_rows_sha256": performance_rows_artifact["sha256"],
            "object_row_count": len(census_rows), "required_row_count": len(parsed_rows),
            "axes": axes, "row_identity_fields": list(binding.ROW_IDENTITY_FIELDS),
            "statistical_family": family,
            "runtime_eligibility": "independent-native-executable-oracle-only",
            "code_section_eligibility": "deterministic-code-section-payload-only",
        }
        performance_data_bytes = (
            json.dumps(performance_data, sort_keys=True, separators=(",", ":"))
            + "\n").encode("utf-8")
        performance_declaration_artifact = artifact("performance/declaration.json",
                                                    performance_data_bytes)

        support_files = []
        for name in binding.SUPPORT_FILE_ROLES:
            source = {
                "support_declaration": declaration_artifact,
                "performance_declaration": performance_declaration_artifact,
                "manifest": manifest_artifact, "inputs": inputs_artifact,
                "rows": rows_artifact, "dependencies": dependencies_artifact,
                "environment": environment_artifact,
                "performance_rows": performance_rows_artifact,
                "validator_report": validator_report_artifact,
            }[name]
            item = dict(source)
            item["name"] = name
            support_files.append(item)

        closure = {}
        closure_items = []
        for kind in binding.REQUIRED_WORK_CLOSURE_KINDS:
            closure_artifact = artifact(f"work/{kind}.closure", f"#508 {kind} closure\n")
            closure[kind] = {"name": f"native-retirement-{kind}",
                             "artifact": closure_artifact}
            closure_items.append({"kind": kind, "name": closure[kind]["name"],
                                  "artifact": closure_artifact})
        support = {
            "schema": "native-retirement-support-v1", "version": "1",
            "root_sha256": "0" * 64,
            "files": support_files,
            "validator": {"name": "native_retirement_contract.py", "version": 1,
                          "source_commit": validator_commit, "source_tree": validator_tree,
                          "source": validator_source},
            "closure": closure,
        }
        support["root_sha256"] = binding._support_root_digest(
            support_files, support["validator"], closure)
        requested_work_items = sorted(closure_items,
                                      key=lambda item: (item["kind"], item["name"]))
        requested_closure = {"manifest_sha256": manifest_artifact["sha256"]}
        for kind in binding.REQUIRED_WORK_CLOSURE_KINDS:
            item = closure[kind]
            requested_closure[kind] = {
                "name": item["name"], "bytes": item["artifact"]["bytes"],
                "sha256": item["artifact"]["sha256"],
            }

        def json_artifact(path, value):
            return artifact(path, (json.dumps(value, sort_keys=True,
                                               separators=(",", ":")) + "\n").encode())

        baseline_snapshot = json_artifact("subjects/baseline-source.json", {
            "schema": binding.SOURCE_SNAPSHOT_SCHEMA, "version": 1,
            "source_commit": "3" * 40, "source_tree": "4" * 40,
            "snapshot_kind": "git-tree-with-submodules-and-generated-inputs",
        })
        candidate_snapshot = json_artifact("subjects/candidate-source.json", {
            "schema": binding.SOURCE_SNAPSHOT_SCHEMA, "version": 1,
            "source_commit": "5" * 40, "source_tree": "6" * 40,
            "snapshot_kind": "git-tree-with-submodules-and-generated-inputs",
        })
        compiler_binary = artifact("producer/clang", "trusted clang\n")
        resource_directory = artifact("producer/resource.tar", "resource closure\n")
        build_configuration = artifact("producer/build-config.json", "release unity\n")
        build_flags = artifact("producer/build-flags.txt", "-O2 -Werror\n")

        def build_receipt(role, snapshot, binary):
            value = {
                "schema": binding.BUILD_RECEIPT_SCHEMA, "version": 1,
                "source_commit": "3" * 40 if role == "baseline" else "5" * 40,
                "source_tree": "4" * 40 if role == "baseline" else "6" * 40,
                "source_snapshot_sha256": snapshot["sha256"], "binary_sha256": binary["sha256"],
                "compiler_binary_sha256": compiler_binary["sha256"],
                "resource_directory_sha256": resource_directory["sha256"],
                "configuration_sha256": build_configuration["sha256"],
                "flags_sha256": build_flags["sha256"],
                "relation": "git-source-snapshot-to-trusted-build-to-binary",
            }
            return json_artifact(f"subjects/{role}-build.json", value)

        baseline_receipt = build_receipt("baseline", baseline_snapshot, baseline_binary)
        candidate_receipt = build_receipt("candidate", candidate_snapshot, candidate_binary)
        harness_binary = artifact("measurement/harness", "native harness\n")
        statistics = artifact("measurement/statistics.py", "paired statistics\n")
        service_recipe = json_artifact("execution/service-recipe", {
            "schema": binding.SERVICE_RECEIPT_SCHEMA, "version": 1,
            "service_id": "retirement-9700x", "service_version": "service-v1",
            "whole_job": True, "phases": ["preparation", "build", "tests",
                                             "measurement", "finalization", "cleanup"],
            "supervisor_authoritative": True, "lease_protocol": binding.LEASE_PROTOCOL,
            "cgroup_cleanup": True, "descendant_cleanup": True,
        })
        profile_descriptor = json_artifact("execution/profile.json", {
            "schema": binding.PROFILE_SCHEMA, "version": 1,
            "profile_id": "zen5-9700x-native", "profile_version": "profile-v1",
            "machine_id": "zen5-9700x-01", "native_only": True,
            "whole_host_isolation": True, "lease_protocol": binding.LEASE_PROTOCOL,
        })
        qualification = json_artifact("execution/qualification.json", {
            "schema": binding.QUALIFICATION_SCHEMA, "version": 1,
            "machine_id": "zen5-9700x-01", "profile_id": "zen5-9700x-native",
            "profile_version": "profile-v1", "qualified": True,
            "whole_host_isolation": True, "lease_protocol": binding.LEASE_PROTOCOL,
        })
        aa_admission = json_artifact("execution/aa-admission.json", {
            "schema": binding.AA_SCHEMA, "version": 1,
            "machine_id": "zen5-9700x-01", "profile_id": "zen5-9700x-native",
            "profile_version": "profile-v1", "service_id": "retirement-9700x",
            "admitted": True, "native_only": True,
            "baseline_source_commit": "3" * 40, "baseline_source_tree": "4" * 40,
            "lease_protocol": binding.LEASE_PROTOCOL,
        })
        lease_receipt = json_artifact("execution/lease-receipt.json", {
            "schema": binding.LEASE_RECEIPT_SCHEMA, "version": 1,
            "authority": binding.LEASE_AUTHORITY, "access": binding.LEASE_ACCESS,
            "cleanup": binding.LEASE_CLEANUP, "owner": "server-supervisor",
            "candidate_can_access": False, "cloexec_before_candidate": True,
            "cgroup_cleanup": True, "descendant_cleanup": True,
        })
        contract_source = artifact("docs/native-retirement-performance-contract.md",
                                   "bound contract source\n")
        relation = {
            "schema": binding.PROVENANCE_SCHEMA, "version": binding.PROVENANCE_VERSION,
            "baseline": {"source_commit": "3" * 40, "source_tree": "4" * 40,
                          "source_snapshot_sha256": baseline_snapshot["sha256"],
                          "binary_sha256": baseline_binary["sha256"],
                          "build_receipt_sha256": baseline_receipt["sha256"]},
            "candidate": {"source_commit": "5" * 40, "source_tree": "6" * 40,
                           "source_snapshot_sha256": candidate_snapshot["sha256"],
                           "binary_sha256": candidate_binary["sha256"],
                           "build_receipt_sha256": candidate_receipt["sha256"]},
            "toolchain": {"compiler_binary_sha256": compiler_binary["sha256"],
                           "resource_directory_sha256": resource_directory["sha256"],
                           "configuration_sha256": build_configuration["sha256"],
                           "flags_sha256": build_flags["sha256"]},
            "harness": {"source_commit": "7" * 40, "source_tree": "8" * 40,
                        "binary_sha256": harness_binary["sha256"],
                        "statistics_sha256": statistics["sha256"]},
        }
        relation_receipt = json_artifact("provenance/relations.json", relation)
        replay_bundle = artifact("provenance/replay-bundle.tar", "#510 bundle\n")
        census_receipt = json_artifact("provenance/census-replay.json", {
            "schema": "buster-native-retirement-census-replay-v1", "success": True,
            "release": "retirement-v1", "github_run_id": "123",
            "rows_validated": binding.SUPPORT_OBJECT_ROW_COUNT,
            "inputs_validated": binding.SUPPORT_INPUT_COUNT, "joined_files": 8,
            "joined_tree_sha256": "d" * 64,
            "candidate_binary_sha256": candidate_binary["sha256"],
            "archived_direct_oracle_sha256": "e" * 64,
            "rebuilt_direct_oracle_sha256": "e" * 64,
            "rebuilt_matches_archived": True,
            "validator_report_sha256": validator_report_artifact["sha256"],
        })
        strict_receipt = json_artifact("provenance/strict-replay.json", {
            "schema": "buster-native-retirement-strict-replay-v1", "success": True,
            "release": "retirement-v1", "github_run_id": "123",
            "archive_sha256": "f" * 64, "archive_size": 1,
            "candidate_binary_sha256": candidate_binary["sha256"],
            "recorded_summary": "pass", "replayed_summary": "pass",
            "cases": 1, "configurations": 432,
        })
        replay_receipt = json_artifact("provenance/replay.json", {
            "schema": binding.REPLAY_SCHEMA, "version": binding.REPLAY_VERSION,
            "publisher": "native-retirement-evidence-v1",
            "bundle_sha256": replay_bundle["sha256"],
            "published_bundle_sha256": replay_bundle["sha256"],
            "downloaded_bundle_sha256": replay_bundle["sha256"],
            "census_receipt_sha256": census_receipt["sha256"],
            "strict_receipt_sha256": strict_receipt["sha256"],
            "contract_source_commit": "1" * 40, "contract_source_tree": "2" * 40,
            "candidate_source_commit": "5" * 40, "candidate_source_tree": "6" * 40,
            "replayed": True, "result": "identity-and-evidence-replayed",
        })

        baseline = {
            "role": "direct-baseline", "source_commit": "3" * 40,
            "source_tree": "4" * 40, "source_snapshot": baseline_snapshot,
            "binary": baseline_binary, "build_receipt": baseline_receipt,
            "dispatch": "direct-native", "stage": "pre-cutover",
        }
        candidate = {
            "role": "mir-candidate", "source_commit": "5" * 40,
            "source_tree": "6" * 40, "source_snapshot": candidate_snapshot,
            "binary": candidate_binary, "build_receipt": candidate_receipt,
            "dispatch": "mir-only", "stage": "post-deletion",
        }
        record = {
            "schema": binding.SCHEMA, "decision_id": binding.DECISION_ID,
            "contract": {"source_commit": "1" * 40, "source_tree": "2" * 40,
                          "source": contract_source},
            "support": support,
            "requested_work": {
                "schema": "native-retirement-requested-work-v1", "version": 1,
                "root_sha256": binding._canonical_work_digest(requested_work_items),
                "items": requested_work_items, "closure": requested_closure,
            },
            "population": {
                "required_row_count": len(parsed_rows),
                "required_rows_sha256": performance_rows_artifact["sha256"],
                "axes": axes, "row_identity_fields": list(binding.ROW_IDENTITY_FIELDS),
                "statistical_family": family,
                "source_digests": {role: support_files[index]["sha256"]
                                   for index, role in enumerate(binding.SUPPORT_FILE_ROLES)
                                   if role in binding.PERFORMANCE_SOURCE_ROLES},
            },
            "subjects": {"baseline": baseline, "candidate": candidate},
            "producer": {
                "toolchain": {"name": "clang", "version": "18.1.3",
                               "compiler_binary": compiler_binary,
                               "resource_directory": resource_directory},
                "build": {"configuration": build_configuration, "flags": build_flags,
                          "mode": "Release", "unity": True,
                          "warnings_as_errors": True, "sanitizers": False,
                          "profiling": False, "allocation_hooks": False},
            },
            "measurement": {"harness_source_commit": "7" * 40,
                            "harness_source_tree": "8" * 40,
                            "harness_binary": harness_binary,
                            "statistics_implementation": statistics},
            "execution": {
                "service": {"id": "retirement-9700x", "version": "service-v1",
                            "recipe": service_recipe},
                "host": {"machine_id": "zen5-9700x-01",
                         "qualification_receipt": qualification,
                         "aa_admission_receipt": aa_admission},
                "profile": {"id": "zen5-9700x-native", "version": "profile-v1",
                            "machine_id": "zen5-9700x-01", "descriptor": profile_descriptor,
                            "digest": profile_descriptor["sha256"]},
                "job_ownership": binding.LEASE_PROTOCOL,
                "lease": {"authority": binding.LEASE_AUTHORITY, "access": binding.LEASE_ACCESS,
                          "cleanup": binding.LEASE_CLEANUP, "receipt": lease_receipt},
                "native_execution": "native-only",
            },
            "provenance": {"schema": binding.PROVENANCE_SCHEMA,
                           "version": binding.PROVENANCE_VERSION,
                           "relation_receipt": relation_receipt,
                           "replay_receipt": replay_receipt,
                           "replay_bundle": replay_bundle,
                           "census_receipt": census_receipt,
                           "strict_receipt": strict_receipt},
            "rules": cls._rules(),
        }
        return record, contents

    @staticmethod
    def _rules():
        return {
            "thresholds": {
                "aggregate": {"compiler_wall_time": 1.02, "compiler_peak_rss": 1.02,
                               "generated_code_bytes": 1.01, "generated_runtime": 1.03},
                "per_cell": {"compiler_wall_time": 1.05, "compiler_peak_rss": 1.05,
                              "generated_code_bytes": 1.01, "generated_runtime": 1.03},
            },
            "sampling": {"seed": 20260913, "rounds": 2, "pairs_per_round": 60,
                         "warmups_per_variant": 2,
                         "block_order": "one-AB-and-one-BA-pair",
                         "fixed_order": "seeded-cell-order-and-first-order",
                         "optional_stopping": False, "outlier_deletion": False,
                         "retain_all_samples": True},
            "aggregation": {"ratio": "candidate-over-baseline",
                             "wall_time": "geometric-mean-cell-ratios",
                             "peak_rss": "geometric-mean-cell-ratios",
                             "code_bytes": "exact-code-section-sum-ratio",
                             "runtime": "geometric-mean-cell-ratios",
                             "cell_weight": "one-equal-weight-per-required-cell",
                             "denominator": "requested-work-from-manifest",
                             "scope": "both-rounds-and-pooled-analysis",
                             "runtime_eligibility": "independent-native-executable-oracle-only",
                             "code_bytes_scope": "deterministic-code-section-payload-only"},
            "uncertainty": {
                "confidence": "one-sided-95-percent-upper-bound", "simultaneous": True,
                "family_correction": "Bonferroni",
                "resampling": {"method": "paired-block-bootstrap", "resamples": 100000,
                                "block_unit": "paired-round-block", "seeded": True},
                "invalid_data": "fail-closed",
                "family": {"scopes": list(binding.STATISTICAL_SCOPES),
                           "dimensions": list(binding.STATISTICAL_DIMENSIONS),
                           "metrics": list(binding.STATISTICAL_METRICS),
                           "simultaneous": True,
                           "pic_slices": "approved-as-required-slices"},
            },
            "outcomes": {"allowed": list(binding.OUTCOMES),
                         "pass_requires": "all-identities-rows-oracles-rounds-bounds-and-code-bytes",
                         "only_pass_accepts": True},
        }

    def make_record(self, full=False):
        if full:
            if self.__class__._full_record is None:
                self.__class__._full_record, self.__class__._full_contents = \
                    self._build_record(full=True)
            return copy.deepcopy(self.__class__._full_record), dict(self.__class__._full_contents)
        return self._build_record(full=False)

    @staticmethod
    def write_record(directory, record):
        path = directory / "performance-binding.json"
        path.write_text(json.dumps(record, indent=2) + "\n", encoding="utf-8")
        return path

    @staticmethod
    def write_evidence(directory, record, contents):
        artifacts = binding._all_artifacts(record)
        artifacts.append(("contract.source", record["contract"]["source"]))
        for _name, artifact in artifacts:
            target = directory / artifact["path"]
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(contents[artifact["path"]])

    def assert_rejected(self, record):
        with tempfile.TemporaryDirectory(prefix="retirement-binding-invalid-") as directory:
            path = self.write_record(Path(directory), record)
            with self.assertRaises(ValueError):
                binding.validate(path)

    def test_complete_record_and_evidence_are_accepted(self):
        record, contents = self.make_record(full=True)
        with tempfile.TemporaryDirectory(prefix="retirement-binding-") as directory:
            root = Path(directory)
            path = self.write_record(root, record)
            self.write_evidence(root / "evidence", record, contents)
            structural = binding.validate(path)
            self.assertEqual(structural["proof"], "structural-only")
            self.assertFalse(structural["rows_recomputed"])
            checked = binding.validate(path, root / "evidence")
            self.assertEqual(checked["candidate_stage"], "post-deletion")
            self.assertGreaterEqual(checked["required_rows"], binding.SUPPORT_MIN_STAGE_ROW_COUNT)
            self.assertTrue(checked["evidence_checked"])
            self.assertTrue(checked["rows_recomputed"])
            self.assertTrue(checked["provenance_checked"])
            self.assertTrue(checked["support_checked"])
            self.assertTrue(checked["execution_checked"])
            self.assertTrue(checked["bundle_checked"])
            self.assertFalse(checked["git_checked"])
            self.assertEqual(checked["proof"],
                             "evidence-and-receipts-checked-without-independent-git")

    def test_mutable_or_short_identity_is_rejected(self):
        record, _contents = self.make_record()
        for value in ("main", "UNBOUND", "a" * 39):
            candidate = copy.deepcopy(record)
            candidate["subjects"]["candidate"]["source_commit"] = value
            with self.subTest(value=value):
                self.assert_rejected(candidate)

    def test_git_commit_tree_ids_are_resolved_not_self_attested(self):
        # A syntactically valid 555/666 pair is still not an immutable identity.
        record, _contents = self.make_record()
        fake = {
            "contract": {"source_commit": "5" * 40, "source_tree": "6" * 40},
            "subjects": {
                "baseline": {"source_commit": "5" * 40, "source_tree": "6" * 40},
                "candidate": {"source_commit": "5" * 40, "source_tree": "6" * 40},
            },
            "measurement": {"harness_source_commit": "5" * 40,
                            "harness_source_tree": "6" * 40},
            "support": {"validator": {"source_commit": "5" * 40,
                                        "source_tree": "6" * 40}},
        }
        with tempfile.TemporaryDirectory(prefix="retirement-binding-git-") as directory:
            repository = Path(directory)
            subprocess.run(["git", "init", "--quiet", str(repository)], check=True)
            with self.assertRaises(ValueError):
                binding._check_git_identities(repository, fake)

    def test_threshold_changes_are_rejected(self):
        record, _contents = self.make_record()
        record["rules"]["thresholds"]["aggregate"]["compiler_wall_time"] = 1.03
        self.assert_rejected(record)

    def test_incomplete_population_and_unknown_fields_are_rejected(self):
        record, _contents = self.make_record()
        del record["support"]["files"][-1]
        self.assert_rejected(record)
        record, _contents = self.make_record()
        record["rules"]["unexpected"] = True
        self.assert_rejected(record)

    def test_two_dummy_work_items_are_rejected(self):
        record, _contents = self.make_record()
        record["requested_work"]["items"] = [
            {"kind": "source", "name": "dummy", "artifact": {
                "path": "work/dummy-source", "bytes": 1, "sha256": "a" * 64}},
            {"kind": "workload-input", "name": "dummy", "artifact": {
                "path": "work/dummy-workload", "bytes": 1, "sha256": "b" * 64}},
        ]
        record["requested_work"]["root_sha256"] = binding._canonical_work_digest(
            record["requested_work"]["items"])
        self.assert_rejected(record)

    def test_complete_dummy_closure_is_rejected(self):
        record, _contents = self.make_record()
        items = []
        for kind in binding.REQUIRED_WORK_CLOSURE_KINDS:
            items.append({"kind": kind, "name": "dummy-" + kind, "artifact": {
                "path": "work/dummy-" + kind, "bytes": 1,
                "sha256": hashlib.sha256(kind.encode()).hexdigest()}})
        record["requested_work"]["items"] = items
        record["requested_work"]["root_sha256"] = binding._canonical_work_digest(items)
        for kind in binding.REQUIRED_WORK_CLOSURE_KINDS:
            record["requested_work"]["closure"][kind] = {
                "name": "dummy-" + kind, "bytes": 1,
                "sha256": hashlib.sha256(kind.encode()).hexdigest()}
        self.assert_rejected(record)

    def test_pairs_are_even_bounded_and_seeded(self):
        record, _contents = self.make_record()
        for pairs in (61, 257):
            candidate = copy.deepcopy(record)
            candidate["rules"]["sampling"]["pairs_per_round"] = pairs
            with self.subTest(pairs=pairs):
                self.assert_rejected(candidate)
        candidate = copy.deepcopy(record)
        candidate["rules"]["sampling"]["seed"] = 0
        self.assert_rejected(candidate)

    def test_round_family_and_pic_policy_are_explicit(self):
        record, _contents = self.make_record()
        record["rules"]["uncertainty"]["family"]["pic_slices"] = "diagnostic-only"
        self.assert_rejected(record)
        record, _contents = self.make_record()
        record["rules"]["uncertainty"]["family"]["scopes"] = ["pooled"]
        self.assert_rejected(record)

    def test_inherited_lease_protocol_is_rejected(self):
        record, _contents = self.make_record()
        record["execution"]["job_ownership"] = "server-owned-whole-job-inherited-lease-v1"
        self.assert_rejected(record)

    def test_row_count_is_recomputed_not_trusted(self):
        record, contents = self.make_record(full=True)
        record["population"]["required_row_count"] = 999999
        with tempfile.TemporaryDirectory(prefix="retirement-binding-row-count-") as directory:
            root = Path(directory)
            path = self.write_record(root, record)
            self.write_evidence(root / "evidence", record, contents)
            with self.assertRaises(ValueError):
                binding.validate(path, root / "evidence")

    def test_one_line_fake_row_count_is_rejected(self):
        record, contents = self.make_record()
        index = binding.SUPPORT_FILE_ROLES.index("performance_rows")
        rows_path = record["support"]["files"][index]["path"]
        rows_record = json.loads(contents[rows_path].decode("utf-8"))
        rows_record["rows"] = rows_record["rows"][:1]
        rows_record["row_count"] = 999999
        rows_data = (json.dumps(rows_record, sort_keys=True, separators=(",", ":"))
                     + "\n").encode("utf-8")
        contents[rows_path] = rows_data
        performance_rows = record["support"]["files"][index]
        performance_rows["bytes"] = len(rows_data)
        performance_rows["sha256"] = hashlib.sha256(rows_data).hexdigest()
        record["support"]["root_sha256"] = binding._canonical_files_digest(
            record["support"]["files"])
        record["population"]["required_rows_sha256"] = performance_rows["sha256"]
        with tempfile.TemporaryDirectory(prefix="retirement-binding-one-line-") as directory:
            root = Path(directory)
            path = self.write_record(root, record)
            self.write_evidence(root / "evidence", record, contents)
            with self.assertRaises(ValueError):
                binding.validate(path, root / "evidence")

    def test_provenance_receipt_content_is_checked(self):
        record, contents = self.make_record(full=True)
        relation_path = record["provenance"]["relation_receipt"]["path"]
        relation = json.loads(contents[relation_path].decode("utf-8"))
        relation["candidate"]["binary_sha256"] = "f" * 64
        contents[relation_path] = (json.dumps(relation, sort_keys=True,
                                               separators=(",", ":")) + "\n").encode("utf-8")
        record["provenance"]["relation_receipt"]["bytes"] = len(contents[relation_path])
        record["provenance"]["relation_receipt"]["sha256"] = hashlib.sha256(
            contents[relation_path]).hexdigest()
        with tempfile.TemporaryDirectory(prefix="retirement-binding-provenance-") as directory:
            root = Path(directory)
            path = self.write_record(root, record)
            self.write_evidence(root / "evidence", record, contents)
            with self.assertRaises(ValueError):
                binding.validate(path, root / "evidence")

    def test_replay_bundle_digest_is_not_self_attested(self):
        record, contents = self.make_record(full=True)
        replay_path = record["provenance"]["replay_receipt"]["path"]
        replay = json.loads(contents[replay_path].decode("utf-8"))
        replay["downloaded_bundle_sha256"] = "9" * 64
        contents[replay_path] = (json.dumps(replay, sort_keys=True,
                                             separators=(",", ":")) + "\n").encode("utf-8")
        record["provenance"]["replay_receipt"]["bytes"] = len(contents[replay_path])
        record["provenance"]["replay_receipt"]["sha256"] = hashlib.sha256(
            contents[replay_path]).hexdigest()
        with tempfile.TemporaryDirectory(prefix="retirement-binding-replay-") as directory:
            root = Path(directory)
            path = self.write_record(root, record)
            self.write_evidence(root / "evidence", record, contents)
            with self.assertRaises(ValueError):
                binding.validate(path, root / "evidence")

    def test_evidence_tampering_is_rejected(self):
        record, contents = self.make_record()
        with tempfile.TemporaryDirectory(prefix="retirement-binding-tamper-") as directory:
            root = Path(directory)
            path = self.write_record(root, record)
            evidence = root / "evidence"
            self.write_evidence(evidence, record, contents)
            evidence.joinpath(record["subjects"]["candidate"]["binary"]["path"]).write_text(
                "tampered\n", encoding="utf-8")
            with self.assertRaises(ValueError):
                binding.validate(path, evidence)


if __name__ == "__main__":
    unittest.main()
