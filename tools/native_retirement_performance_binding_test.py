#!/usr/bin/env python3
"""Offline tests for the fail-closed native-retirement binding validator."""

import copy
from contextlib import contextmanager
import csv
import hashlib
import importlib.util
import io
import json
from pathlib import Path
import re
import subprocess
import sqlite3
import sys
import tarfile
import tempfile
import unittest
from unittest import mock


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

    @contextmanager
    def _adapter_checkout(self):
        """Keep immutable-source controls separate from ephemeral rebinding."""
        commit = subprocess.run(["git", "rev-parse", "HEAD"], cwd=ROOT,
                                check=True, capture_output=True,
                                text=True).stdout.strip()
        with tempfile.TemporaryDirectory(prefix="retirement-adapter-source-") as directory:
            repository = Path(directory) / "repository"
            subprocess.run(["git", "clone", "--quiet", "--shared", "--no-checkout",
                            str(ROOT), str(repository)], check=True,
                           capture_output=True)
            subprocess.run(["git", "-C", str(repository), "checkout", "--quiet",
                            "--detach", commit], check=True, capture_output=True)
            yield repository

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
        # This is the actual schema-2 validate-shards report shape.  The
        # performance binding checks candidate and acceptance cleanliness
        # separately; a synthetic ``success`` receipt is intentionally not
        # accepted as a substitute.
        report_data = {
            "schema": 2,
            "directories": 1,
            "shards": 1,
            "rows_validated": len(census_rows),
            "groups": len(census_rows) // len(binding.ALLOCATORS),
            "compiler_revision_claim": revision_a,
            "baseline_revision_claim": revision_b,
            "compiler_sha256": candidate_binary["sha256"],
            "baseline_sha256": baseline_binary["sha256"],
            "support_contract_sha256": declaration_artifact["sha256"],
            "resource_include_sha256": "c" * 64,
            "manifest_identity_sha256": manifest_artifact["sha256"],
            "rows_identity_fields": list(binding.ROW_FIELDS),
            "rows_identity_sha256": rows_artifact["sha256"],
            "input_ledger_fields": list(binding.INPUT_FIELDS),
            "input_ledger_sha256": inputs_artifact["sha256"],
            "baseline_dispositions": {},
            "reference_dispositions": {},
            "setup_dispositions": {},
            "candidate_dispositions": {},
            "candidate_failure_rows": [],
            "reference_failure_rows": [],
            "acceptance_failure_rows": [],
            "inapplicable_rows": [],
            "fallback_defect_rows": [],
            "telemetry_defect_rows": [],
            "execution_defect_rows": [],
            "require_clean_candidate": True,
            "require_clean_acceptance": True,
            "clean_candidate": True,
            "clean_acceptance": True,
            "complete_row_partition": True,
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
        statistics = artifact("tools/throughput/retirement_stats.h",
                              (ROOT / "tools/throughput/retirement_stats.h").read_bytes())
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
            "logical_cpu": 3, "native_target": "x86_64-unknown-linux-gnu",
            "whole_host_isolation": True, "lease_protocol": binding.LEASE_PROTOCOL,
        })
        qualification = json_artifact("execution/qualification.json", {
            "schema": binding.QUALIFICATION_SCHEMA, "version": 1,
            "machine_id": "zen5-9700x-01", "profile_id": "zen5-9700x-native",
            "profile_version": "profile-v1", "qualified": True,
            "logical_cpu": 3, "native_target": "x86_64-unknown-linux-gnu",
            "whole_host_isolation": True, "lease_protocol": binding.LEASE_PROTOCOL,
        })
        aa_admission = json_artifact("execution/aa-admission.json", {
            "schema": binding.AA_SCHEMA, "version": 1,
            "machine_id": "zen5-9700x-01", "profile_id": "zen5-9700x-native",
            "profile_version": "profile-v1", "service_id": "retirement-9700x",
            "admitted": True, "native_only": True,
            "logical_cpu": 3, "native_target": "x86_64-unknown-linux-gnu",
            "baseline_source_commit": "3" * 40, "baseline_source_tree": "4" * 40,
            "lease_protocol": binding.LEASE_PROTOCOL,
        })

        # Independent admission and native-oracle records are the source of
        # row eligibility.  The canonical performance rows are joined to
        # these records below; they do not get to invent their own runtime or
        # code-section obligations.
        census_identity_index = {
            tuple(row[field] for field in binding.ROW_IDENTITY_FIELDS
                  if field != "artifact_stage"): index
            for index, row in enumerate(census_rows)
        }
        admission_records = []
        oracle_records = []
        for row in parsed_rows:
            identity = row["identity"]
            key = tuple(identity[field] for field in binding.ROW_IDENTITY_FIELDS
                        if field != "artifact_stage")
            admission_records.append({
                "row": row["row"], "census_row": census_identity_index[key],
                "identity": identity, "artifact_stage": identity["artifact_stage"],
                "requested_obligation": "compiler-wall-time-and-peak-rss",
                "status": "completed", "exit_code": 0, "timed_out": False,
                "native_compiler": True,
                "artifact_kind": ("object" if identity["artifact_stage"] == "object"
                                   else "linked-executable"),
                "artifact_bytes": 1,
                "artifact_sha256": "a" * 64,
            })
            oracle_records.append({
                "row": row["row"],
                "code_section_status": "parsed-deterministic",
                "code_section_bytes": 1, "code_section_sha256": "b" * 64,
                "runtime_oracle_status": ("passed-native"
                                           if row["eligibility"]["generated_runtime"]
                                           else "not-applicable"),
                "runtime_exit_code": (0 if row["eligibility"]["generated_runtime"] else -1),
                "native_runtime": row["eligibility"]["generated_runtime"],
            })
        admission_artifact = json_artifact("execution/admission-records.json", {
            "schema": binding.ADMISSION_SCHEMA, "version": 1,
            "source_manifest_sha256": manifest_artifact["sha256"],
            "source_rows_sha256": rows_artifact["sha256"],
            "records": admission_records,
        })
        oracle_artifact = json_artifact("execution/oracle-records.json", {
            "schema": binding.ORACLE_SCHEMA, "version": 1,
            "source_manifest_sha256": manifest_artifact["sha256"],
            "source_rows_sha256": rows_artifact["sha256"],
            "records": oracle_records,
        })
        family_counts = binding._family_member_counts(family)
        sample_rounds = 2
        sample_pairs = 60
        object_row_count = len(census_rows)
        required_records = len(parsed_rows) * sample_rounds * sample_pairs
        sample_record = {
            "record_id": "row-0/round-0/pair-0", "row": 0, "round": 0, "pair": 0,
            "measurements": {
                metric: {"baseline": 1.0, "candidate": 1.0}
                for metric in binding.METRICS if parsed_rows[0]["metrics"].get(metric, False)
            },
        }
        sample_record_bytes = (json.dumps(sample_record, sort_keys=True,
                                          separators=(",", ":")) + "\n").encode()
        input_shard = artifact("results/input-shard-000.jsonl", sample_record_bytes)
        input_manifest = json_artifact("results/input-manifest-000.json", {
            "schema": binding.RESULT_INPUT.MANIFEST_SCHEMA,
            "version": 1, "identity_field": "record_id",
            "shards": [{"identity": "result-input-shard-000",
                        "path": input_shard["path"], "bytes": input_shard["bytes"],
                        "sha256": input_shard["sha256"]}],
        })
        result_input_plan = json_artifact("results/input-plan.json", {
            "schema": binding.RESULT_INPUT_PLAN_SCHEMA, "version": 1,
            "source_manifest_sha256": manifest_artifact["sha256"],
            "source_rows_sha256": rows_artifact["sha256"],
            "object_row_count": object_row_count,
            "sample_row_count": len(parsed_rows),
            "rounds": sample_rounds,
            "identity_field": "record_id", "coordinate_schema": "row-round-pair-v1",
            "sample_population": "canonical-performance-rows-with-required-metrics",
            "eligible_population": "canonical-performance-rows",
            "pairs_per_round": sample_pairs,
            "records_per_row": sample_rounds * sample_pairs,
            "required_records": required_records,
            "max_records_per_manifest": binding.RESULT_INPUT_MAX_RECORDS,
            "manifest_count": 1,
            "manifests": [{
                "identity": "result-input-manifest-000",
                "path": input_manifest["path"], "start_record": 0,
                "records": required_records,
            }],
            "predeclared": True,
        })
        adapter_calls = []
        bootstrap_members = sorted(member for member in family["members"]
                                   if member.endswith("/aggregate") or "/slice/" in member)
        cell_members = sorted(member for member in family["members"]
                              if "/cell/" in member)
        # The full 77,184-row fixture is deliberately structural-only.  Do
        # not manufacture hundreds of thousands of later-stage call results;
        # the focused small fixture is the executable adapter test.
        if not full:
            for index, member in enumerate(family["members"]):
                metric_name = member.split("/", 1)[0]
                is_cell = "/cell/" in member
                members = cell_members if is_cell else bootstrap_members
                adapter_calls.append({
                    "member": member, "metric": binding.STATISTICAL_METRICS.index(metric_name),
                    "kind": 1 if is_cell else 0, "family_index": members.index(member),
                    "outcome": "pass", "valid": True, "resampled": not is_cell,
                    "resamples": 0 if is_cell else 100000, "tail_alpha": 0.001,
                    "round": [{"estimate": 1.0, "lower": 1.0, "upper": 1.0},
                              {"estimate": 1.0, "lower": 1.0, "upper": 1.0}],
                    "pooled": {"estimate": 1.0, "lower": 1.0, "upper": 1.0},
                })
        adapter_result = json_artifact("results/statistics-replay.json", {
            "schema": "buster-native-retirement-statistics-replay-v1", "version": 1,
            "members": adapter_calls,
        })
        adapter_input = artifact(
            "results/statistics-series.txt",
            "version=1 seed=20260913 bootstrap_members=1 cell_members=1 pairs=60 resamples=100000 frozen=1 members=0\n")
        raw_measurements_sha256 = hashlib.sha256(sample_record_bytes).hexdigest()
        invocation_sha256 = binding._family_invocation_digest(family)
        sealed_result_bundle = json_artifact("results/sealed-result.bundle", {
            "schema": binding.RESULT_BUNDLE_SCHEMA, "version": 1,
            "source_rows_sha256": rows_artifact["sha256"],
            "result_input_plan_sha256": result_input_plan["sha256"],
            "family_sha256": family["sha256"],
            "result_manifests": [{
                "identity": "result-input-manifest-000",
                "path": input_manifest["path"], "bytes": input_manifest["bytes"],
                "sha256": input_manifest["sha256"], "start_record": 0,
                "records": required_records,
                "input_bytes": input_shard["bytes"] + input_manifest["bytes"],
            }],
            "raw_measurements_sha256": raw_measurements_sha256,
            "member_invocations_sha256": invocation_sha256,
            "member_count": len(family["members"]),
            "scopes_per_member": len(binding.STATISTICAL_SCOPES),
            "adapter_input": adapter_input,
            "code_bytes_summary": {
                "rows": 0, "aggregate_ratio": 1.0,
                "per_cell_max_ratio": 1.0, "aggregate_pass": True,
                "per_cell_pass": True, "ratios_sha256": "0" * 64,
            },
        })
        seal_files = [{"name": "sealed-result", **sealed_result_bundle}]
        seal = {
            "schema": "buster-native-retirement-result-seal-v1", "version": 1,
            "files": seal_files,
            "root_sha256": binding._canonical_files_digest(seal_files),
        }
        pre_sample_plan = json_artifact("workflow/pre-sample-plan.json", {
            "schema": binding.PHASE_SCHEMA["pre_sample_plan"], "version": 1,
            "status": "frozen-before-samples",
            "support_declaration_sha256": declaration_artifact["sha256"],
            "manifest_sha256": manifest_artifact["sha256"],
            "rows_sha256": rows_artifact["sha256"], "family_sha256": family["sha256"],
            "seed": 20260913, "rounds": sample_rounds, "pairs_per_round": sample_pairs,
            "resamples": 100000,
            "bootstrap_members_per_scope": family_counts["bootstrap_members_per_scope"],
            "cell_members_per_scope": family_counts["cell_members_per_scope"],
            "result_input_plan_sha256": result_input_plan["sha256"],
        })
        post_aa_binding = json_artifact("workflow/post-aa-binding.json", {
            "schema": binding.PHASE_SCHEMA["post_aa_binding"], "version": 1,
            "status": "bound-after-aa-before-samples",
            "support_declaration_sha256": declaration_artifact["sha256"],
            "manifest_sha256": manifest_artifact["sha256"],
            "rows_sha256": rows_artifact["sha256"], "family_sha256": family["sha256"],
            "seed": 20260913, "rounds": sample_rounds, "pairs_per_round": sample_pairs,
            "resamples": 100000,
            "bootstrap_members_per_scope": family_counts["bootstrap_members_per_scope"],
            "cell_members_per_scope": family_counts["cell_members_per_scope"],
            "result_input_plan_sha256": result_input_plan["sha256"],
            "pre_sample_plan_sha256": pre_sample_plan["sha256"],
            "aa_admission_sha256": aa_admission["sha256"],
        })
        sealed_result = json_artifact("workflow/sealed-result.json", {
            "schema": binding.SEALED_RESULT_SCHEMA, "version": 1,
            "status": "sealed-for-independent-replay",
            "post_aa_binding_sha256": post_aa_binding["sha256"],
            "result_input_plan_sha256": result_input_plan["sha256"],
            "family_sha256": family["sha256"],
            "result_bundle": sealed_result_bundle, "seal": seal,
        })
        downloaded_bundle = artifact("results/downloaded-independent.bundle.tar", b"structural-only\n")
        replay_bundle = json_artifact("results/independent-replay.bundle", {
            "schema": binding.REPLAY_BUNDLE_SCHEMA, "version": 1,
            "sealed_result_sha256": sealed_result_bundle["sha256"],
            "raw_measurements_sha256": raw_measurements_sha256,
            "family_sha256": family["sha256"],
            "member_invocations_sha256": invocation_sha256,
            "member_count": len(family["members"]),
            "adapter_command": "bench_throughput retirement-replay --input SERIES_FILE --output RESULT_JSON",
            "adapter_build_command": "cc -std=c11 -O2 -Wall -Wextra -Werror",
            "adapter_toolchain_sha256": "b" * 64,
            "adapter_source_sha256": statistics["sha256"],
            "code_bytes_summary_sha256": "0" * 64,
            "publication_id": "published-retirement-bundle-v1",
            "published_bundle_sha256": downloaded_bundle["sha256"],
            "downloaded_bundle_sha256": downloaded_bundle["sha256"],
            "downloaded_bundle": downloaded_bundle,
            "adapter_result": adapter_result,
        })
        independent_replay = json_artifact("workflow/independent-replay.json", {
            "schema": binding.PHASE_SCHEMA["independent_replay"], "version": 1,
            "status": "independently-replayed",
            "sealed_result_sha256": sealed_result["sha256"],
            "replay_bundle": replay_bundle,
        })
        workflow = {
            "schema": binding.WORKFLOW_SCHEMA, "version": binding.WORKFLOW_VERSION,
            "phases": {
                "pre_sample_plan": pre_sample_plan,
                "post_aa_binding": post_aa_binding,
                "sealed_result": sealed_result,
                "independent_replay": independent_replay,
            },
            "records": {"admission": admission_artifact, "oracle": oracle_artifact,
                        "result_input_plan": result_input_plan},
        }
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
        rules = cls._rules()
        family_counts = binding._family_member_counts(family)
        rules["sampling"].update({
            "bootstrap_members_per_scope": family_counts["bootstrap_members_per_scope"],
            "cell_members_per_scope": family_counts["cell_members_per_scope"],
        })
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
            "rules": rules,
            "workflow": workflow,
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
                         "resamples": 100000,
                         "bootstrap_members_per_scope": 1,
                         "cell_members_per_scope": 1,
                         "frozen_before_samples": True,
                         "warmups_per_variant": 2,
                         "block_order": "one-AB-and-one-BA-pair",
                         "fixed_order": "seeded-cell-order-and-first-order",
                         "optional_stopping": False, "outlier_deletion": False,
                         "retain_all_samples": True},
            "aggregation": {"ratio": "candidate-over-baseline",
                             "wall_time": "median-of-two-pair-block-geometric-means",
                             "peak_rss": "median-of-two-pair-block-geometric-means",
                             "code_bytes": "exact-code-section-sum-ratio",
                             "runtime": "median-of-two-pair-block-geometric-means",
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
        # Nested #615 manifests and sealed/replay bundles are validated by the
        # workflow phase checker rather than appearing as top-level binding
        # descriptors.  Materialize every fixture byte artifact for that
        # checker; production bundles carry the same nested descriptors.
        for path, data in contents.items():
            target = directory / path
            if not target.exists():
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_bytes(data)

    def assert_rejected(self, record):
        with tempfile.TemporaryDirectory(prefix="retirement-binding-invalid-") as directory:
            path = self.write_record(Path(directory), record)
            with self.assertRaises(ValueError):
                binding.validate(path)

    def test_complete_record_structural_contract_is_accepted(self):
        # The 77,184-row closure is intentionally structural-only here.  A
        # real evidence bundle must carry and stream every bounded result
        # partition; this fixture must not fabricate 9M+ #615 records.
        record, _contents = self.make_record(full=True)
        with tempfile.TemporaryDirectory(prefix="retirement-binding-") as directory:
            root = Path(directory)
            path = self.write_record(root, record)
            structural = binding.validate(path)
            self.assertEqual(structural["proof"], "structural-only")
            self.assertFalse(structural["rows_recomputed"])

    def test_bounded_result_partition_streams_small_complete_fixture(self):
        # Exercise the real #615 descriptor-relative verifier with a tiny
        # one-row/two-round/60-pair partition.  The full closure test above
        # remains structural so it does not allocate the production result
        # population in a unit test.
        record, contents = self.make_record()
        rows_artifact = record["support"]["files"][
            binding.SUPPORT_FILE_ROLES.index("performance_rows")]
        parsed, _axes, _family = binding._performance_rows(
            contents[rows_artifact["path"]])
        row = parsed[0]
        row_ordinals = {row["row"]: 0}
        row_by_id = {row["row"]: row}
        rounds, pairs = 2, 60
        lines = []
        for round_number in range(rounds):
            for pair in range(pairs):
                lines.append(json.dumps({
                    "record_id": f"row-{row['row']}/round-{round_number}/pair-{pair}",
                    "row": row["row"], "round": round_number, "pair": pair,
                    "measurements": {
                        metric: {"baseline": (1 if metric in ("generated_code_bytes", "compiler_peak_rss") else 1.0),
                                  "candidate": (1 if metric in ("generated_code_bytes", "compiler_peak_rss") else 1.0)}
                        for metric in binding.METRICS if row["metrics"].get(metric, False)
                    },
                }, sort_keys=True, separators=(",", ":")) + "\n")
        shard_bytes = "".join(lines).encode()
        with tempfile.TemporaryDirectory(prefix="retirement-result-input-") as directory:
            root = Path(directory)
            shard = root / "shard-000.jsonl"
            manifest = root / "manifest.json"
            shard.write_bytes(shard_bytes)
            manifest.write_text(json.dumps({
                "schema": binding.RESULT_INPUT.MANIFEST_SCHEMA, "version": 1,
                "identity_field": "record_id",
                "shards": [{"identity": "shard-000", "path": shard.name,
                            "bytes": len(shard_bytes),
                            "sha256": hashlib.sha256(shard_bytes).hexdigest()}],
            }, sort_keys=True, separators=(",", ":")) + "\n", encoding="utf-8")
            seen = [0]
            digest = hashlib.sha256()
            receipt = binding.RESULT_INPUT.verify(
                root, manifest.name,
                binding.RESULT_INPUT.Limits(max_records=binding.RESULT_INPUT_MAX_RECORDS),
                record_consumer=lambda _shard, _index, value: binding._consume_result_record(
                    value, row_ordinals, row_by_id, rounds, pairs, 0, seen, digest))
            self.assertEqual(receipt["records"], rounds * pairs)
            self.assertEqual(seen[0], rounds * pairs)
            self.assertEqual(receipt["identity_field"], "record_id")

    def test_trusted_retirement_adapter_replays_small_series(self):
        # Compile the reviewed in-tree adapter and exercise its actual #619
        # CLI with one aggregate and one exact-cell member for each metric.
        # This is intentionally bounded while still covering both call kinds,
        # all three metrics, and the one-call/all-scopes output contract.
        with tempfile.TemporaryDirectory(prefix="retirement-adapter-") as directory:
            root = Path(directory)
            executable, binary_digest, source_digest, toolchain_digest, build_command = \
                binding._compile_trusted_retirement_adapter(root)
            self.assertEqual(len(binary_digest), 64)
            self.assertEqual(len(source_digest), 64)
            self.assertEqual(len(toolchain_digest), 64)
            self.assertIn("-std=c11", build_command)
            metrics = [("compiler_peak_rss", 1, 1.02, 1.05),
                       ("compiler_wall_time", 0, 1.02, 1.05),
                       ("generated_runtime", 2, 1.03, 1.03)]
            lines = [
                "version=1 seed=20260913 bootstrap_members=3 cell_members=3 "
                "pairs=60 resamples=100000 frozen=1 members=6\n"
            ]
            bootstrap_names = sorted(name + "/aggregate" for name, _index, _a, _c in metrics)
            cell_names = sorted(name + "/cell/row=0" for name, _index, _a, _c in metrics)
            for metric_name, metric_index, aggregate_limit, cell_limit in metrics:
                for kind, suffix, cells, resamples, limit in (
                        (0, "/aggregate", 1, 100000, aggregate_limit),
                        (1, "/cell/row=0", 1, 0, cell_limit)):
                    member = metric_name + suffix
                    family_index = ((cell_names if kind else bootstrap_names).index(member))
                    lines.append(
                        f"member={member} metric={metric_index} kind={kind} family={family_index} "
                        f"cells={cells} pairs=60 resamples={resamples} limit={limit}\n")
                    lines.extend("ratio=1\n" for _ in range(120))
                    lines.append("end\n")
            series = root / "series.txt"
            series.write_text("".join(lines), encoding="utf-8")
            output = root / "result.json"
            replay = subprocess.run(
                [str(executable), "retirement-replay", "--input", str(series),
                 "--output", str(output)], check=False, capture_output=True, text=True)
            self.assertEqual(replay.returncode, 0, replay.stderr)
            result = json.loads(output.read_text(encoding="utf-8"))
            self.assertEqual(result["schema"],
                             "buster-native-retirement-statistics-replay-v1")
            self.assertEqual(len(result["members"]), 6)
            self.assertTrue(all(item["valid"] for item in result["members"]))
            self.assertEqual({item["resamples"] for item in result["members"]}, {0, 100000})
            oversized = root / "oversized.series"
            oversized.write_text(
                "version=1 seed=20260913 bootstrap_members=1 cell_members=1 "
                "pairs=60 resamples=4294967295 frozen=1 members=2\n",
                encoding="utf-8")
            rejected = subprocess.run(
                [str(executable), "retirement-replay", "--input", str(oversized),
                 "--output", str(root / "oversized.json")],
                check=False, capture_output=True, text=True)
            self.assertNotEqual(rejected.returncode, 0)

    def test_independent_archive_round_trip_is_streamed_and_bound(self):
        payload = b"sealed payload\n"
        expected = [{"name": "closure.payload", "path": "closure/payload",
                     "bytes": len(payload), "sha256": hashlib.sha256(payload).hexdigest()}]
        manifest = {
            "schema": "buster-native-retirement-independent-bundle-manifest-v1",
            "version": 1, "publication_id": "publication-v1",
            "files": [{"path": item["path"], "bytes": item["bytes"],
                       "sha256": item["sha256"]} for item in expected],
            "root_sha256": binding._canonical_files_digest([
                {"name": item["path"], "path": item["path"],
                 "bytes": item["bytes"], "sha256": item["sha256"]}
                for item in expected]),
        }
        with tempfile.TemporaryDirectory(prefix="retirement-archive-") as directory:
            root = Path(directory)
            archive_path = root / "downloaded.tar"
            manifest_bytes = (json.dumps(manifest, sort_keys=True,
                                         separators=(",", ":")) + "\n").encode()
            with tarfile.open(archive_path, "w") as archive:
                manifest_info = tarfile.TarInfo("bundle-manifest.json")
                manifest_info.size = len(manifest_bytes)
                archive.addfile(manifest_info, io.BytesIO(manifest_bytes))
                payload_info = tarfile.TarInfo("closure/payload")
                payload_info.size = len(payload)
                archive.addfile(payload_info, io.BytesIO(payload))
            bundle = {"path": archive_path.name, "bytes": archive_path.stat().st_size,
                      "sha256": hashlib.sha256(archive_path.read_bytes()).hexdigest()}
            original_data = archive_path.read_bytes()
            extracted = binding._extract_downloaded_bundle(
                root, bundle, "publication-v1", expected, root / "replay")
            self.assertEqual((extracted / "closure/payload").read_bytes(), payload)

            replacement_path = root / "replacement.tar"
            with tarfile.open(replacement_path, "w") as replacement:
                manifest_info = tarfile.TarInfo("bundle-manifest.json")
                manifest_info.size = len(manifest_bytes)
                manifest_info.mtime = 1
                replacement.addfile(manifest_info, io.BytesIO(manifest_bytes))
                payload_info = tarfile.TarInfo("closure/payload")
                payload_info.size = len(payload)
                payload_info.mtime = 1
                replacement.addfile(payload_info, io.BytesIO(payload))
            self.assertEqual(archive_path.stat().st_size, replacement_path.stat().st_size)
            original_check = binding._check_evidence

            def replace_after_check(check_root, artifact, name):
                original_check(check_root, artifact, name)
                if name == "independent_replay_bundle.downloaded_bundle":
                    archive_path.write_bytes(replacement_path.read_bytes())

            with mock.patch.object(binding, "_check_evidence",
                                   side_effect=replace_after_check):
                with self.assertRaises(ValueError):
                    binding._extract_downloaded_bundle(
                        root, bundle, "publication-v1", expected, root / "race-replay")

            archive_path.write_bytes(original_data)
            original_open = binding._open_verified_archive

            def open_then_mutate(check_root, artifact, name):
                stream = original_open(check_root, artifact, name)
                archive_path.write_bytes(replacement_path.read_bytes())
                return stream

            with mock.patch.object(binding, "_open_verified_archive",
                                   side_effect=open_then_mutate):
                extracted = binding._extract_downloaded_bundle(
                    root, bundle, "publication-v1", expected, root / "in-place-replay")
            self.assertEqual((extracted / "closure/payload").read_bytes(), payload)

    def test_independent_archive_rejects_unsafe_member_without_leaking(self):
        payload = b"sealed payload\n"
        expected = [{"name": "closure.payload", "path": "closure/payload",
                     "bytes": len(payload), "sha256": hashlib.sha256(payload).hexdigest()}]
        manifest = {
            "schema": "buster-native-retirement-independent-bundle-manifest-v1",
            "version": 1, "publication_id": "publication-v1",
            "files": [{"path": item["path"], "bytes": item["bytes"],
                       "sha256": item["sha256"]} for item in expected],
            "root_sha256": binding._canonical_files_digest([
                {"name": item["path"], "path": item["path"],
                 "bytes": item["bytes"], "sha256": item["sha256"]}
                for item in expected]),
        }
        with tempfile.TemporaryDirectory(prefix="retirement-archive-invalid-") as directory:
            root = Path(directory)
            archive_path = root / "unsafe.tar"
            manifest_bytes = (json.dumps(manifest, sort_keys=True,
                                         separators=(",", ":")) + "\n").encode()
            with tarfile.open(archive_path, "w") as archive:
                manifest_info = tarfile.TarInfo("bundle-manifest.json")
                manifest_info.size = len(manifest_bytes)
                archive.addfile(manifest_info, io.BytesIO(manifest_bytes))
                unsafe = tarfile.TarInfo("../outside")
                unsafe.size = len(payload)
                archive.addfile(unsafe, io.BytesIO(payload))
            bundle = {"path": archive_path.name, "bytes": archive_path.stat().st_size,
                      "sha256": hashlib.sha256(archive_path.read_bytes()).hexdigest()}
            with self.assertRaises(ValueError):
                binding._extract_downloaded_bundle(
                    root, bundle, "publication-v1", expected, root / "replay")

    @staticmethod
    def _series_join_fixture():
        identity = {
            "fixture": "tests/basic_c_operations.c",
            "target": "x86_64-unknown-linux-gnu", "target_abi": "systemv-x86_64",
            "cpu": "baseline", "cpu_features": "baseline", "allocator": "none",
            "frontend_lowering": "direct-ssa", "PIC": "0",
            "fixture_recipe": "compiler-default", "compile_obligation": "object",
            "link_obligation": "semantic-gate-509", "execution_obligation": "semantic-gate-509",
            "diagnostic_obligation": "none", "argv_evidence": "groups/0/none.argv",
            "artifact_stage": "object",
        }
        metrics = {metric: True for metric in binding.METRICS}
        parsed = [{"row": 0, "identity": identity, "metrics": metrics,
                   "eligibility": {
                       "compiler_wall_time": True, "compiler_peak_rss": True,
                       "generated_code_bytes": True, "generated_runtime": True,
                       "runtime_oracle": "independent-native-executable-oracle",
                       "code_section": "deterministic-code-section",
                   }}]
        family = binding._derive_statistical_family(parsed)
        rules = BindingTests._rules()
        counts = binding._family_member_counts(family)
        rules["sampling"].update({
            "bootstrap_members_per_scope": counts["bootstrap_members_per_scope"],
            "cell_members_per_scope": counts["cell_members_per_scope"],
        })
        return parsed, family, rules

    def _build_small_evidence_fixture(self):
        """Build one bounded, fully wired workflow for the evidence-path test.

        The production support declaration remains the 77,184-object-row
        census; the support-output function is patched only in this test so
        the workflow/seal/replay composition can execute on one object row,
        one native link row, and 240 streamed #615 records.  The independent
        schema-2 validator has its own 23-case suite and is not replaced by
        this bounded wiring check.
        """
        record, contents = self.make_record()

        def put(path, data):
            if isinstance(data, str):
                data = data.encode("utf-8")
            contents[path] = data
            return {"path": path, "bytes": len(data),
                    "sha256": hashlib.sha256(data).hexdigest()}

        def json_data(value):
            return (json.dumps(value, sort_keys=True, separators=(",", ":"))
                    + "\n").encode("utf-8")

        harness_commit = subprocess.run(
            ["git", "rev-parse", "HEAD"], cwd=ROOT, check=True,
            capture_output=True, text=True).stdout.strip()
        harness_tree = subprocess.run(
            ["git", "rev-parse", "HEAD^{tree}"], cwd=ROOT, check=True,
            capture_output=True, text=True).stdout.strip()
        record["measurement"]["harness_source_commit"] = harness_commit
        record["measurement"]["harness_source_tree"] = harness_tree
        relation_descriptor = record["provenance"]["relation_receipt"]
        relation = json.loads(contents[relation_descriptor["path"]].decode("utf-8"))
        relation["harness"].update({"source_commit": harness_commit,
                                     "source_tree": harness_tree})
        relation_descriptor.update(
            put(relation_descriptor["path"], json_data(relation)))

        support_files = record["support"]["files"]
        performance_rows_descriptor = support_files[
            binding.SUPPORT_FILE_ROLES.index("performance_rows")]
        rows_record = json.loads(contents[performance_rows_descriptor["path"]].decode())
        object_row = copy.deepcopy(rows_record["rows"][0])
        object_row["row"] = 0
        object_row["eligibility"] = {
            "compiler_wall_time": True, "compiler_peak_rss": True,
            "generated_code_bytes": True, "generated_runtime": False,
            "runtime_oracle": "not-applicable",
            "code_section": "deterministic-code-section",
        }
        runtime_row = copy.deepcopy(object_row)
        runtime_row["row"] = 1
        runtime_row["identity"]["artifact_stage"] = "link"
        runtime_row["eligibility"].update({
            "generated_runtime": True,
            "runtime_oracle": "independent-native-executable-oracle",
        })
        rows_record["rows"] = [object_row, runtime_row]
        rows_data = json_data(rows_record)
        performance_rows_descriptor.update(put(performance_rows_descriptor["path"], rows_data))
        parsed, axes, family = binding._performance_rows(rows_data)

        performance_descriptor = support_files[
            binding.SUPPORT_FILE_ROLES.index("performance_declaration")]
        performance = json.loads(contents[performance_descriptor["path"]].decode())
        performance.update({"performance_rows_sha256": performance_rows_descriptor["sha256"],
                            "required_row_count": 2, "axes": axes,
                            "statistical_family": family})
        performance_descriptor.update(
            put(performance_descriptor["path"], json_data(performance)))
        record["support"]["root_sha256"] = binding._support_root_digest(
            support_files, record["support"]["validator"], record["support"]["closure"])
        record["population"].update({
            "required_row_count": 2,
            "required_rows_sha256": performance_rows_descriptor["sha256"],
            "axes": axes,
            "statistical_family": family,
            "source_digests": binding._artifact_digest_map(record["support"]),
        })
        counts = binding._family_member_counts(family)
        record["rules"]["sampling"].update({
            "bootstrap_members_per_scope": counts["bootstrap_members_per_scope"],
            "cell_members_per_scope": counts["cell_members_per_scope"],
        })

        admission_descriptor = record["workflow"]["records"]["admission"]
        admission = json.loads(contents[admission_descriptor["path"]].decode())
        admission_template = copy.deepcopy(admission["records"][0])
        admission["records"] = []
        for performance_row in parsed:
            item = copy.deepcopy(admission_template)
            stage = performance_row["identity"]["artifact_stage"]
            item.update({"row": performance_row["row"], "census_row": 0,
                         "identity": performance_row["identity"],
                         "artifact_stage": stage,
                         "artifact_kind": ("object" if stage == "object"
                                           else "linked-executable")})
            admission["records"].append(item)
        admission_descriptor.update(put(admission_descriptor["path"], json_data(admission)))
        oracle_descriptor = record["workflow"]["records"]["oracle"]
        oracle = json.loads(contents[oracle_descriptor["path"]].decode())
        oracle_template = copy.deepcopy(oracle["records"][0])
        oracle["records"] = []
        for performance_row in parsed:
            runtime = performance_row["metrics"]["generated_runtime"]
            item = copy.deepcopy(oracle_template)
            item.update({
                "row": performance_row["row"],
                "runtime_oracle_status": ("passed-native" if runtime
                                            else "not-applicable"),
                "runtime_exit_code": 0 if runtime else -1,
                "native_runtime": runtime,
            })
            oracle["records"].append(item)
        oracle_descriptor.update(put(oracle_descriptor["path"], json_data(oracle)))

        # Stream both canonical rows over both rounds and all 60 pairs.
        measurement_lines = []
        for performance_row in parsed:
            for round_number in range(2):
                for pair in range(60):
                    value = {
                        "record_id": (f"row-{performance_row['row']}/"
                                      f"round-{round_number}/pair-{pair}"),
                        "row": performance_row["row"], "round": round_number,
                        "pair": pair,
                        "measurements": {
                            metric: {
                                "baseline": (1 if metric in ("generated_code_bytes",
                                                              "compiler_peak_rss") else 1.0),
                                "candidate": (1 if metric in ("generated_code_bytes",
                                                               "compiler_peak_rss") else 1.0),
                            }
                            for metric in binding.METRICS
                            if performance_row["metrics"][metric]
                        },
                    }
                    measurement_lines.append(json_data(value))
        shard_data = b"".join(measurement_lines)
        shard_descriptor = put("results/input-shard-000.jsonl", shard_data)
        manifest_value = {
            "schema": binding.RESULT_INPUT.MANIFEST_SCHEMA, "version": 1,
            "identity_field": "record_id",
            "shards": [{"identity": "result-input-shard-000",
                        **shard_descriptor}],
        }
        manifest_data = json_data(manifest_value)
        manifest_descriptor = put("results/input-manifest-000.json", manifest_data)
        plan_value = {
            "schema": binding.RESULT_INPUT_PLAN_SCHEMA, "version": 1,
            "source_manifest_sha256": support_files[
                binding.SUPPORT_FILE_ROLES.index("manifest")]["sha256"],
            "source_rows_sha256": support_files[
                binding.SUPPORT_FILE_ROLES.index("rows")]["sha256"],
            "object_row_count": 1, "sample_row_count": 2,
            "rounds": 2, "identity_field": "record_id",
            "coordinate_schema": "row-round-pair-v1",
            "sample_population": "canonical-performance-rows-with-required-metrics",
            "eligible_population": "canonical-performance-rows",
            "pairs_per_round": 60, "records_per_row": 120,
            "required_records": 240,
            "max_records_per_manifest": binding.RESULT_INPUT_MAX_RECORDS,
            "manifest_count": 1,
            "manifests": [{"identity": "result-input-manifest-000",
                           "path": manifest_descriptor["path"], "start_record": 0,
                           "records": 240}],
            "predeclared": True,
        }
        plan_descriptor = record["workflow"]["records"]["result_input_plan"]
        plan_descriptor.update(put(plan_descriptor["path"], json_data(plan_value)))

        # Construct every scope-free logical member and ask the reviewed C
        # adapter to produce the actual result bytes for this small fixture.
        bootstrap_index, cell_index = binding._family_member_indexes(family)
        series_lines = [
            f"version=1 seed={record['rules']['sampling']['seed']} "
            f"bootstrap_members={counts['bootstrap_members_per_scope']} "
            f"cell_members={counts['cell_members_per_scope']} pairs=60 "
            f"resamples={record['rules']['sampling']['resamples']} frozen=1 "
            f"members={len(family['members'])}\n"
        ]
        for member in family["members"]:
            metric_name = member.split("/", 1)[0]
            is_cell = "/cell/" in member
            index = cell_index[member] if is_cell else bootstrap_index[member]
            limit = binding.CELL_THRESHOLDS[metric_name] if is_cell \
                else binding.AGGREGATE_THRESHOLDS[metric_name]
            if is_cell:
                selected_rows = [int(member.rsplit("=", 1)[1])]
            elif member.endswith("/aggregate"):
                selected_rows = [item["row"] for item in parsed
                                 if item["metrics"][metric_name]]
            else:
                dimension, selected = member.split("/slice/", 1)[1].split("=", 1)
                selected_rows = [item["row"] for item in parsed
                                 if item["metrics"][metric_name]
                                 and str(item["identity"][dimension]) == selected]
            series_lines.append(
                f"member={member} metric={binding.STATISTICAL_METRICS.index(metric_name)} "
                f"kind={1 if is_cell else 0} family={index} "
                f"cells={len(selected_rows)} pairs=60 "
                f"resamples={0 if is_cell else 100000} limit={limit}\n")
            series_lines.extend("ratio=1.0\n" for _ in range(120 * len(selected_rows)))
            series_lines.append("end\n")
        series_data = "".join(series_lines).encode("utf-8")
        adapter_input_descriptor = put("results/statistics-series.txt", series_data)
        with tempfile.TemporaryDirectory(prefix="retirement-e2e-adapter-") as adapter_directory:
            adapter_directory = Path(adapter_directory)
            trusted_input = adapter_directory / "series.txt"
            trusted_output = adapter_directory / "result.json"
            trusted_input.write_bytes(series_data)
            executable, source_digest, source_closure_digest, toolchain_digest, build_command = \
                binding._compile_trusted_retirement_adapter(adapter_directory)
            process = subprocess.run(
                [str(executable), "retirement-replay", "--input", str(trusted_input),
                 "--output", str(trusted_output)],
                check=False, capture_output=True, text=True,
                cwd=ROOT)
            self.assertEqual(process.returncode, 0, process.stderr)
            adapter_result_data = trusted_output.read_bytes()
        adapter_result_descriptor = put("results/statistics-replay.json", adapter_result_data)

        connection = sqlite3.connect(":memory:")
        try:
            connection.execute(
                "CREATE TABLE samples(row_id INTEGER, round_id INTEGER, pair_id INTEGER, "
                "metric TEXT, baseline TEXT, candidate TEXT)")
            for performance_row in parsed:
                for round_number in range(2):
                    for pair in range(60):
                        connection.execute(
                            "INSERT INTO samples VALUES (?, ?, ?, 'generated_code_bytes', '1', '1')",
                            (performance_row["row"], round_number, pair))
            connection.commit()
            code_summary = binding._code_bytes_summary(
                parsed, connection, 2, 60)
        finally:
            connection.close()
        raw_measurements_digest = hashlib.sha256(shard_data).hexdigest()
        result_bundle_value = {
            "schema": binding.RESULT_BUNDLE_SCHEMA, "version": 1,
            "source_rows_sha256": support_files[
                binding.SUPPORT_FILE_ROLES.index("rows")]["sha256"],
            "result_input_plan_sha256": plan_descriptor["sha256"],
            "family_sha256": family["sha256"],
            "result_manifests": [{
                "identity": "result-input-manifest-000", **manifest_descriptor,
                "start_record": 0, "records": 240,
                "input_bytes": len(shard_data) + len(manifest_data),
            }],
            "raw_measurements_sha256": raw_measurements_digest,
            "member_invocations_sha256": binding._family_invocation_digest(family),
            "member_count": len(family["members"]),
            "scopes_per_member": len(binding.STATISTICAL_SCOPES),
            "adapter_input": adapter_input_descriptor,
            "code_bytes_summary": code_summary,
        }
        result_bundle_descriptor = put("results/sealed-result.bundle",
                                      json_data(result_bundle_value))

        def phase(path, value):
            return put(path, json_data(value))

        pre_value = {
            "schema": binding.PHASE_SCHEMA["pre_sample_plan"], "version": 1,
            "status": "frozen-before-samples",
            "support_declaration_sha256": support_files[0]["sha256"],
            "manifest_sha256": support_files[2]["sha256"],
            "rows_sha256": support_files[4]["sha256"],
            "family_sha256": family["sha256"],
            "seed": record["rules"]["sampling"]["seed"], "rounds": 2,
            "pairs_per_round": 60, "resamples": 100000,
            "bootstrap_members_per_scope": counts["bootstrap_members_per_scope"],
            "cell_members_per_scope": counts["cell_members_per_scope"],
            "result_input_plan_sha256": plan_descriptor["sha256"],
        }
        pre_descriptor = phase("workflow/pre-sample-plan.json", pre_value)
        record["workflow"]["phases"]["pre_sample_plan"] = pre_descriptor
        post_value = dict(pre_value)
        post_value.update({
            "schema": binding.PHASE_SCHEMA["post_aa_binding"],
            "status": "bound-after-aa-before-samples",
            "pre_sample_plan_sha256": pre_descriptor["sha256"],
            "aa_admission_sha256": record["execution"]["host"][
                "aa_admission_receipt"]["sha256"],
        })
        post_descriptor = phase("workflow/post-aa-binding.json", post_value)
        record["workflow"]["phases"]["post_aa_binding"] = post_descriptor

        # Synthetic service receipt, with its digest supplied independently by
        # this test caller. This exercises the full invocation gate without
        # presenting fixture data as deployed-service or performance evidence.
        from native_retirement_performance_identity_test import InvocationEvidenceTests
        with tempfile.TemporaryDirectory(prefix="retirement-e2e-execution-") as execution_directory:
            execution_root = Path(execution_directory)
            for name, data in contents.items():
                target = execution_root / name
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_bytes(data)
            sample_map = {}
            for line in shard_data.splitlines():
                item = json.loads(line)
                sample_map[(item["row"], item["round"], item["pair"])] = item["measurements"]
            execution_plan, execution_receipt, _receipt, events, _digest = (
                InvocationEvidenceTests.attach_execution(execution_root, record, parsed, sample_map))
            pre_value["execution_plan"] = execution_plan
            pre_descriptor = phase("workflow/pre-sample-plan.json", pre_value)
            record["workflow"]["phases"]["pre_sample_plan"] = pre_descriptor
            post_value.update({"execution_plan": execution_plan,
                               "pre_sample_plan_sha256": pre_descriptor["sha256"]})
            post_descriptor = phase("workflow/post-aa-binding.json", post_value)
            record["workflow"]["phases"]["post_aa_binding"] = post_descriptor
            execution_receipt["context_sha256"] = binding._canonical_json_digest(
                binding._execution_context(record, raw_measurements_digest))
            execution_descriptor = InvocationEvidenceTests.write_transcript(
                execution_root, execution_receipt, events)
            for name in (execution_plan["path"], execution_descriptor["path"],
                         "execution/invocations.jsonl"):
                contents[name] = (execution_root / name).read_bytes()
            result_bundle_value["execution_receipt"] = execution_descriptor
            result_bundle_descriptor = put("results/sealed-result.bundle", json_data(result_bundle_value))

        with tempfile.TemporaryDirectory(prefix="retirement-e2e-seal-") as seal_directory:
            seal_root = Path(seal_directory)
            for path, data in contents.items():
                target = seal_root / path
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_bytes(data)
            seal_files = binding._sealed_closure_files(
                seal_root, record, None, record["workflow"]["records"],
                record["workflow"]["phases"], plan_value,
                result_bundle_descriptor, result_bundle_value,
                adapter_result_descriptor)
        seal_value = {
            "schema": "buster-native-retirement-result-seal-v1", "version": 1,
            "files": seal_files,
            "root_sha256": binding._canonical_files_digest(seal_files),
        }
        sealed_value = {
            "schema": binding.SEALED_RESULT_SCHEMA, "version": 1,
            "status": "sealed-for-independent-replay",
            "post_aa_binding_sha256": post_descriptor["sha256"],
            "result_input_plan_sha256": plan_descriptor["sha256"],
            "family_sha256": family["sha256"],
            "result_bundle": result_bundle_descriptor, "seal": seal_value,
        }
        sealed_descriptor = phase("workflow/sealed-result.json", sealed_value)
        record["workflow"]["phases"]["sealed_result"] = sealed_descriptor

        publication_id = "published-retirement-bundle-v2"
        downloaded_files = seal_files + [{
            "name": "workflow.phases.sealed_result",
            "path": sealed_descriptor["path"], "bytes": sealed_descriptor["bytes"],
            "sha256": sealed_descriptor["sha256"],
        }]
        archive_manifest_files = [
            {key: item[key] for key in ("path", "bytes", "sha256")}
            for item in downloaded_files
        ]
        archive_manifest = {
            "schema": "buster-native-retirement-independent-bundle-manifest-v1",
            "version": 1, "publication_id": publication_id,
            "files": archive_manifest_files,
            "root_sha256": binding._canonical_files_digest(
                [{"name": item["path"], **item} for item in archive_manifest_files]),
        }
        archive_buffer = io.BytesIO()
        with tarfile.open(fileobj=archive_buffer, mode="w") as archive:
            manifest_bytes = json_data(archive_manifest)
            manifest_info = tarfile.TarInfo("bundle-manifest.json")
            manifest_info.size = len(manifest_bytes)
            archive.addfile(manifest_info, io.BytesIO(manifest_bytes))
            for item in downloaded_files:
                payload = contents[item["path"]]
                info = tarfile.TarInfo(item["path"])
                info.size = len(payload)
                archive.addfile(info, io.BytesIO(payload))
        downloaded_data = archive_buffer.getvalue()
        downloaded_descriptor = put("results/downloaded-independent.bundle.tar", downloaded_data)
        publication_value = {
            "schema": binding.PUBLICATION_SCHEMA, "version": 1,
            "publisher": "native-retirement-evidence-v1", "release": "retirement-v1",
            "run_id": "run-20260914-e2e", "service_id": record["execution"]["service"]["id"],
            "publication_id": publication_id,
            "sealed_result_sha256": result_bundle_descriptor["sha256"],
            "published_bundle_sha256": downloaded_descriptor["sha256"],
            "downloaded_bundle_sha256": downloaded_descriptor["sha256"],
            "replay_result": "independently-replayed",
        }
        publication_descriptor = put("results/performance-publication.json",
                                     json_data(publication_value))
        replay_value = {
            "schema": binding.REPLAY_BUNDLE_SCHEMA, "version": 1,
            "sealed_result_sha256": result_bundle_descriptor["sha256"],
            "raw_measurements_sha256": raw_measurements_digest,
            "family_sha256": family["sha256"],
            "member_invocations_sha256": binding._family_invocation_digest(family),
            "member_count": len(family["members"]),
            "adapter_command": "bench_throughput retirement-replay --input SERIES_FILE --output RESULT_JSON",
            "adapter_build_command": build_command,
            "adapter_toolchain_sha256": toolchain_digest,
            "adapter_source_sha256": source_closure_digest,
            "code_bytes_summary_sha256": binding._canonical_json_digest(code_summary),
            "publication_id": publication_id,
            "published_bundle_sha256": downloaded_descriptor["sha256"],
            "downloaded_bundle_sha256": downloaded_descriptor["sha256"],
            "downloaded_bundle": downloaded_descriptor,
            "adapter_result": adapter_result_descriptor,
            "publication_receipt": publication_descriptor,
        }
        replay_descriptor = put("results/independent-replay.bundle",
                               json_data(replay_value))
        independent_value = {
            "schema": binding.PHASE_SCHEMA["independent_replay"], "version": 1,
            "status": "independently-replayed",
            "sealed_result_sha256": sealed_descriptor["sha256"],
            "replay_bundle": replay_descriptor,
            "publication_receipt": publication_descriptor,
        }
        independent_descriptor = phase("workflow/independent-replay.json",
                                       independent_value)
        record["workflow"]["phases"]["independent_replay"] = independent_descriptor

        census_rows_path = support_files[binding.SUPPORT_FILE_ROLES.index("rows")]["path"]
        census_rows = binding._tsv(contents[census_rows_path], binding.ROW_FIELDS,
                                   "small census rows")
        support_output = {
            "manifest": {}, "inputs": [], "rows": census_rows[:1],
            "dependencies": [], "environment": [], "sources": {},
            "axes": axes, "family": family,
            "support_declaration_sha256": support_files[0]["sha256"],
            "manifest_sha256": support_files[2]["sha256"],
            "rows_sha256": support_files[4]["sha256"],
            "performance_rows_sha256": performance_rows_descriptor["sha256"],
            "validator_report_sha256": support_files[8]["sha256"],
            "object_row_count": 1, "group_count": 1,
        }
        return record, contents, support_output

    def test_bounded_validate_evidence_path_replays_sealed_workflow(self):
        with self._adapter_checkout() as repository, \
                mock.patch.object(binding, "__file__", str(
                    repository / "tools" / "native_retirement_performance_binding.py")):
            record, contents, support_output = self._build_small_evidence_fixture()
            trusted_receipt = hashlib.sha256(contents["execution/invocation-receipt.json"]).hexdigest()
            with tempfile.TemporaryDirectory(prefix="retirement-binding-e2e-") as directory:
                root = Path(directory)
                path = self.write_record(root, record)
                evidence = root / "evidence"
                self.write_evidence(evidence, record, contents)
                with mock.patch.object(binding, "_check_support_output",
                                       return_value=support_output), \
                        mock.patch.object(binding, "_population",
                                          return_value=record["population"]):
                    result = binding.validate(path, evidence, trusted_execution_receipt_sha256=trusted_receipt)
                self.assertEqual(result["proof"],
                                 "evidence-and-receipts-checked-without-independent-git")
                self.assertTrue(result["rows_recomputed"])
                self.assertTrue(result["invocations_checked"])
                # The success path must not be merely a descriptor check: mutate a
                # sealed output byte and retain the original descriptor to prove
                # the sealed result's content/address binding is exercised.
                tampered = evidence / "results" / "statistics-replay.json"
                tampered.write_bytes(tampered.read_bytes() + b"\n")
                with mock.patch.object(binding, "_check_support_output",
                                       return_value=support_output), \
                        mock.patch.object(binding, "_population",
                                          return_value=record["population"]):
                    with self.assertRaises(ValueError):
                        binding.validate(path, evidence, trusted_execution_receipt_sha256=trusted_receipt)

    def test_adapter_series_join_rejects_widened_limit_and_raw_mismatch(self):
        parsed, family, rules = self._series_join_fixture()
        with tempfile.TemporaryDirectory(prefix="retirement-series-join-") as directory:
            root = Path(directory)
            import sqlite3
            connection = sqlite3.connect(root / "samples.sqlite3")
            try:
                connection.execute(
                    "CREATE TABLE samples(row_id INTEGER, round_id INTEGER, pair_id INTEGER, "
                    "metric TEXT, baseline TEXT, candidate TEXT)")
                for metric in binding.STATISTICAL_METRICS:
                    for round_number in range(2):
                        for pair in range(60):
                            connection.execute(
                                "INSERT INTO samples VALUES (?, ?, ?, ?, ?, ?)",
                                (0, round_number, pair, metric, "1.0", "1.0"))
                connection.commit()
                members = []
                bootstrap_members = sorted(item for item in family["members"]
                                           if item.endswith("/aggregate") or "/slice/" in item)
                cell_members = sorted(item for item in family["members"]
                                      if "/cell/" in item)
                for member in family["members"]:
                    metric = member.split("/", 1)[0]
                    cell = "/cell/" in member
                    limit = binding.CELL_THRESHOLDS[metric] if cell else binding.AGGREGATE_THRESHOLDS[metric]
                    index = cell_members if cell else bootstrap_members
                    members.append((member, binding.STATISTICAL_METRICS.index(metric),
                                    1 if cell else 0, index.index(member),
                                    1, 0 if cell else 100000, str(limit)))

                def write_series(path, changed_member=None, changed_ratio=False,
                                 omit_member=None):
                    selected = [item for item in members if item[0] != omit_member]
                    lines = [
                        f"version=1 seed={rules['sampling']['seed']} "
                        f"bootstrap_members={rules['sampling']['bootstrap_members_per_scope']} "
                        f"cell_members={rules['sampling']['cell_members_per_scope']} "
                        f"pairs=60 resamples=100000 frozen=1 members={len(selected)}\n"
                    ]
                    for member, metric, kind, index, cells, resamples, limit in selected:
                        if member == changed_member:
                            limit = "9.0"
                        lines.append(
                            f"member={member} metric={metric} kind={kind} family={index} "
                            f"cells={cells} pairs=60 resamples={resamples} limit={limit}\n")
                        for sample_index in range(120):
                            ratio = "1.1" if changed_ratio and sample_index == 0 else "1.0"
                            lines.append(f"ratio={ratio}\n")
                        lines.append("end\n")
                    path.write_text("".join(lines), encoding="utf-8")

                valid = root / "valid.series"
                write_series(valid)
                artifact = {"path": valid.name, "bytes": valid.stat().st_size,
                            "sha256": hashlib.sha256(valid.read_bytes()).hexdigest()}
                binding._check_adapter_series(root, artifact, family, parsed, rules, connection)
                widened = root / "widened.series"
                write_series(widened, changed_member=members[0][0])
                widened_artifact = {"path": widened.name, "bytes": widened.stat().st_size,
                                    "sha256": hashlib.sha256(widened.read_bytes()).hexdigest()}
                with self.assertRaises(ValueError):
                    binding._check_adapter_series(root, widened_artifact, family, parsed,
                                                  rules, connection)
                mismatch = root / "mismatch.series"
                write_series(mismatch, changed_ratio=True)
                mismatch_artifact = {"path": mismatch.name, "bytes": mismatch.stat().st_size,
                                     "sha256": hashlib.sha256(mismatch.read_bytes()).hexdigest()}
                with self.assertRaises(ValueError):
                    binding._check_adapter_series(root, mismatch_artifact, family, parsed,
                                                  rules, connection)
                omitted = root / "omitted.series"
                write_series(omitted, omit_member=members[-1][0])
                omitted_artifact = {"path": omitted.name, "bytes": omitted.stat().st_size,
                                    "sha256": hashlib.sha256(omitted.read_bytes()).hexdigest()}
                with self.assertRaises(ValueError):
                    binding._check_adapter_series(root, omitted_artifact, family, parsed,
                                                  rules, connection)
            finally:
                connection.close()

    def test_code_byte_summary_uses_integer_per_cell_gates(self):
        parsed, _family, _rules = self._series_join_fixture()
        import sqlite3
        connection = sqlite3.connect(":memory:")
        try:
            connection.execute(
                "CREATE TABLE samples(row_id INTEGER, round_id INTEGER, pair_id INTEGER, "
                "metric TEXT, baseline TEXT, candidate TEXT)")
            for round_number in range(2):
                for pair in range(60):
                    connection.execute(
                        "INSERT INTO samples VALUES (?, ?, ?, ?, ?, ?)",
                        (0, round_number, pair, "generated_code_bytes", "100", "102"))
            connection.commit()
            summary = binding._code_bytes_summary(parsed, connection, 2, 60)
            self.assertFalse(summary["per_cell_pass"])
            self.assertFalse(summary["aggregate_pass"])
        finally:
            connection.close()

    def test_maximum_result_capacity_is_predeclared_without_coordinate_sets(self):
        required = 77184 * 2 * 256
        cap = binding.RESULT_INPUT_MAX_RECORDS
        self.assertEqual(required, 39518208)
        self.assertEqual(required, binding.RESULT_INPUT_MAX_TOTAL_RECORDS)
        self.assertEqual((required + cap - 1) // cap, 3)
        self.assertGreater(required, cap)
        # The validator's join is ordinal arithmetic; this assertion keeps
        # the adversarial maximum a scalar calculation rather than allocating
        # the 39.5M-coordinate population in a test process.
        row_ordinal, round_number, pair = 77183, 1, 255
        self.assertEqual(((row_ordinal * 2 + round_number) * 256 + pair), required - 1)

    def test_presample_partition_rejects_postmeasurement_descriptors(self):
        with tempfile.TemporaryDirectory(prefix="retirement-plan-cycle-") as directory:
            root = Path(directory)
            rules = self._rules()
            plan = {
                "schema": binding.RESULT_INPUT_PLAN_SCHEMA, "version": 1,
                "source_manifest_sha256": "a" * 64, "source_rows_sha256": "b" * 64,
                "identity_field": "record_id", "coordinate_schema": "row-round-pair-v1",
                "sample_population": "canonical-performance-rows-with-required-metrics",
                "eligible_population": "canonical-performance-rows",
                "object_row_count": 1, "sample_row_count": 1,
                "rounds": 2, "pairs_per_round": 60, "records_per_row": 120,
                "required_records": 120,
                "max_records_per_manifest": binding.RESULT_INPUT_MAX_RECORDS,
                "manifest_count": 1,
                "manifests": [{"identity": "manifest-0", "path": "results/manifest-0.json",
                               "start_record": 0, "records": 120}],
                "predeclared": True,
            }

            def check(candidate):
                data = (json.dumps(candidate, sort_keys=True, separators=(",", ":")) + "\n").encode()
                target = root / "plan.json"
                target.write_bytes(data)
                descriptor = {"path": target.name, "bytes": len(data),
                              "sha256": hashlib.sha256(data).hexdigest()}
                return binding._result_input_plan(
                    root, descriptor,
                    {"manifest_sha256": "a" * 64, "rows_sha256": "b" * 64,
                     "object_row_count": 1}, {}, rules)

            accepted = check(plan)
            self.assertEqual(accepted["manifests"], plan["manifests"])
            for field, value in (("bytes", 1), ("sha256", "c" * 64), ("input_bytes", 1)):
                candidate = copy.deepcopy(plan)
                candidate["manifests"][0][field] = value
                with self.subTest(field=field), self.assertRaises(ValueError):
                    check(candidate)

    def test_sealed_manifest_must_match_frozen_partition(self):
        plan = {"manifests": [{"identity": "manifest-0",
                                "path": "results/manifest-0.json",
                                "start_record": 0, "records": 120}]}
        descriptor = {"identity": "manifest-0", "path": "results/manifest-0.json",
                      "bytes": 1, "sha256": "a" * 64,
                      "start_record": 0, "records": 120, "input_bytes": 1}
        self.assertEqual(binding._result_manifest_descriptors([descriptor], plan),
                         [descriptor])
        for field, value in (("identity", "manifest-1"), ("path", "results/other.json"),
                             ("start_record", 1), ("records", 119)):
            candidate = copy.deepcopy(descriptor)
            candidate[field] = value
            with self.subTest(field=field), self.assertRaises(ValueError):
                binding._result_manifest_descriptors([candidate], plan)

    def test_full_population_plan_fits_ceiling_without_dropping_stage_rows(self):
        sample_rows = 77184 + 2
        required = sample_rows * 2 * 254
        cap = binding.RESULT_INPUT_MAX_RECORDS
        tail = required - 2 * cap
        self.assertLessEqual(required, binding.RESULT_INPUT_MAX_TOTAL_RECORDS)
        self.assertGreater(sample_rows * 2 * 256,
                           binding.RESULT_INPUT_MAX_TOTAL_RECORDS)
        with tempfile.TemporaryDirectory(prefix="retirement-cap-plan-") as directory:
            root = Path(directory)
            rules = self._rules()
            rules["sampling"]["pairs_per_round"] = 254
            support_output = {"manifest_sha256": "a" * 64,
                              "rows_sha256": "b" * 64,
                              "object_row_count": 77184}
            manifests = [{
                "identity": f"manifest-{index}", "path": f"results/manifest-{index}.json",
                "start_record": start, "records": records,
            } for index, (start, records) in enumerate(
                ((0, cap), (cap, cap), (2 * cap, tail)))]

            def check(candidate):
                data = (json.dumps(candidate, sort_keys=True, separators=(",", ":")) + "\n").encode()
                plan_path = root / "results/input-plan.json"
                plan_path.parent.mkdir(parents=True, exist_ok=True)
                plan_path.write_bytes(data)
                artifact = {"path": "results/input-plan.json", "bytes": len(data),
                            "sha256": hashlib.sha256(data).hexdigest()}
                return binding._result_input_plan(root, artifact, support_output,
                                                  {}, rules)

            plan = {
                "schema": binding.RESULT_INPUT_PLAN_SCHEMA, "version": 1,
                "source_manifest_sha256": "a" * 64, "source_rows_sha256": "b" * 64,
                "identity_field": "record_id", "coordinate_schema": "row-round-pair-v1",
                "sample_population": "canonical-performance-rows-with-required-metrics",
                "eligible_population": "canonical-performance-rows",
                "object_row_count": 77184, "sample_row_count": sample_rows,
                "rounds": 2, "pairs_per_round": 254, "records_per_row": 508,
                "required_records": required, "max_records_per_manifest": cap,
                "manifest_count": 3, "manifests": manifests, "predeclared": True,
            }
            self.assertEqual(check(plan)["manifest_count"], 3)
            for bad_count in (2, 4):
                candidate = copy.deepcopy(plan)
                candidate["manifest_count"] = bad_count
                candidate["manifests"] = candidate["manifests"][:bad_count]
                with self.assertRaises(ValueError):
                    check(candidate)
            for bad_start in (2 * cap - 1, 2 * cap + 1):
                candidate = copy.deepcopy(plan)
                candidate["manifests"][2]["start_record"] = bad_start
                with self.assertRaises(ValueError):
                    check(candidate)
            candidate = copy.deepcopy(plan)
            candidate["manifests"][0]["records"] = cap - 1
            with self.assertRaises(ValueError):
                check(candidate)
            candidate = copy.deepcopy(plan)
            candidate["pairs_per_round"] = 256
            candidate["records_per_row"] = 512
            candidate["required_records"] = sample_rows * 2 * 256
            rules["sampling"]["pairs_per_round"] = 256
            with self.assertRaises(ValueError):
                check(candidate)

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

    def test_statistical_family_rejects_overlong_c_member_identity(self):
        identity = {field: "x" for field in binding.ROW_IDENTITY_FIELDS}
        identity.update({"target": "x" * 128, "cpu": "baseline",
                         "allocator": "none", "frontend_lowering": "direct-ssa",
                         "PIC": "0", "artifact_stage": "object"})
        row = {"row": 0, "identity": identity,
               "metrics": {metric: True for metric in binding.STATISTICAL_METRICS}}
        with self.assertRaises(ValueError):
            binding._derive_statistical_family([row])

    def test_workflow_watches_the_complete_trusted_adapter_source_closure(self):
        workflow = (ROOT / ".github" / "workflows" /
                    "native-retirement-contract.yml").read_text(encoding="utf-8")
        closure = [
            "tools/throughput/**",
            "src/buster/lib/arena.c", "src/buster/lib/arena.h",
            "src/buster/lib/base.h", "src/buster/lib/file.c",
            "src/buster/lib/file.h", "src/buster/lib/hash.c",
            "src/buster/lib/hash.h", "src/buster/lib/integer.c",
            "src/buster/lib/integer.h", "src/buster/lib/os.c",
            "src/buster/lib/os.h", "src/buster/lib/os_internal.h",
            "src/buster/lib/string.c", "src/buster/lib/string.h",
            "src/buster/lib/system_headers.h", "src/buster/lib/time.c",
            "src/buster/lib/time.h",
        ]
        for path in closure:
            with self.subTest(path=path):
                self.assertRegex(workflow, rf"(?m)^\s+- {re.escape(path)}\s*$")

    def test_trusted_adapter_rejects_unbound_source_identity(self):
        with self._adapter_checkout() as repository:
            commit = subprocess.run(["git", "rev-parse", "HEAD"], cwd=repository,
                                    check=True, capture_output=True,
                                    text=True).stdout.strip()
            tree = subprocess.run(["git", "rev-parse", "HEAD^{tree}"], cwd=repository,
                                  check=True, capture_output=True,
                                  text=True).stdout.strip()
            with tempfile.TemporaryDirectory(prefix="retirement-adapter-identity-") as directory:
                with self.assertRaises(ValueError):
                    binding._compile_trusted_retirement_adapter(
                        directory, repository, "0" * 40, tree)
                with self.assertRaises(ValueError):
                    binding._compile_trusted_retirement_adapter(
                        directory, repository, commit, "f" * 40)

    def test_trusted_adapter_allows_untracked_evidence_but_rejects_tracked_drift(self):
        with self._adapter_checkout() as repository:
            commit = subprocess.run(["git", "rev-parse", "HEAD"], cwd=repository,
                                    check=True, capture_output=True,
                                    text=True).stdout.strip()
            tree = subprocess.run(["git", "rev-parse", "HEAD^{tree}"], cwd=repository,
                                  check=True, capture_output=True,
                                  text=True).stdout.strip()
            source = repository / "tools" / "throughput" / "throughput.c"
            original = source.read_bytes()
            with tempfile.TemporaryDirectory(prefix="retirement-untracked-evidence-",
                                             dir=repository) as evidence:
                Path(evidence, "result.json").write_text("{}\n", encoding="utf-8")
                with tempfile.TemporaryDirectory(prefix="retirement-adapter-clean-") as directory:
                    executable, _binary, _source, _toolchain, _command = \
                        binding._compile_trusted_retirement_adapter(
                            directory, repository, commit, tree)
                    self.assertTrue(executable.is_file())
                try:
                    source.write_bytes(original + b"\n#error tracked drift must fail\n")
                    with tempfile.TemporaryDirectory(prefix="retirement-adapter-dirty-") as directory:
                        with self.assertRaises(ValueError):
                            binding._compile_trusted_retirement_adapter(
                                directory, repository, commit, tree)
                finally:
                    source.write_bytes(original)

    def test_trusted_adapter_materializes_verified_commit_after_checkout_race(self):
        with self._adapter_checkout() as repository:
            commit = subprocess.run(["git", "rev-parse", "HEAD"], cwd=repository,
                                    check=True, capture_output=True,
                                    text=True).stdout.strip()
            tree = subprocess.run(["git", "rev-parse", "HEAD^{tree}"], cwd=repository,
                                  check=True, capture_output=True,
                                  text=True).stdout.strip()
            source = repository / "tools" / "throughput" / "throughput.c"
            original = source.read_bytes()
            original_git_run = binding._git_run

            def mutate_after_tree(repository, arguments, name):
                result = original_git_run(repository, arguments, name)
                if arguments == ["rev-parse", "HEAD^{tree}"]:
                    source.write_bytes(original + b"\n#error uncommitted source race\n")
                return result

            try:
                with tempfile.TemporaryDirectory(prefix="retirement-adapter-race-") as directory:
                    with mock.patch.object(binding, "_git_run",
                                           side_effect=mutate_after_tree):
                        executable, _binary, source_digest, _toolchain, _command = \
                            binding._compile_trusted_retirement_adapter(
                                directory, repository, commit, tree)
                    self.assertTrue(executable.is_file())
                    self.assertEqual(len(source_digest), 64)
            finally:
                source.write_bytes(original)

    def test_trusted_adapter_ignores_git_replacement_objects(self):
        commit = subprocess.run(["git", "rev-parse", "HEAD"], cwd=ROOT,
                                check=True, capture_output=True,
                                text=True).stdout.strip()
        tree = subprocess.run(["git", "rev-parse", "HEAD^{tree}"], cwd=ROOT,
                              check=True, capture_output=True,
                              text=True).stdout.strip()
        with tempfile.TemporaryDirectory(prefix="retirement-adapter-replace-") as directory:
            repository = Path(directory) / "repository"
            subprocess.run(["git", "clone", "--quiet", "--no-local", str(ROOT),
                            str(repository)], check=True, capture_output=True)
            source = repository / "tools" / "throughput" / "throughput.c"
            original = source.read_bytes()
            original_blob = subprocess.run(
                ["git", "-C", str(repository), "rev-parse",
                 "HEAD:tools/throughput/throughput.c"], check=True,
                capture_output=True, text=True).stdout.strip()
            replacement = subprocess.run(
                ["git", "-C", str(repository), "hash-object", "-w", "--stdin"],
                input=original + b"\n#error replacement object must not be consumed\n",
                check=True, capture_output=True).stdout.decode("ascii").strip()
            subprocess.run(["git", "-C", str(repository), "replace", original_blob,
                            replacement], check=True, capture_output=True)
            try:
                with tempfile.TemporaryDirectory(prefix="retirement-adapter-replace-build-") as build:
                    executable, _binary, source_digest, _toolchain, _command = \
                        binding._compile_trusted_retirement_adapter(
                            build, repository, commit, tree)
                    self.assertTrue(executable.is_file())
                    self.assertEqual(len(source_digest), 64)
            finally:
                subprocess.run(["git", "-C", str(repository), "replace", "-d",
                                original_blob], check=True, capture_output=True)

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
