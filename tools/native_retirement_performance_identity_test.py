#!/usr/bin/env python3
"""Unmocked checked-in support identity and current-population budget guards.

These tests do not construct acceptance evidence.  The support-output probe
must reach, and reject, an intentionally manifest-only census after verifying
the real declaration.  Partition tests exercise the current object population
plus the required stage-row lower bound, not a proposed experiment schedule.
"""

import copy
import csv
import hashlib
import io
import json
import os
import shutil
import subprocess
import sys
from pathlib import Path
import tempfile
import unittest
from unittest import mock

import native_retirement_performance_binding as binding


ROOT = Path(__file__).resolve().parents[1]


class PerformanceIdentityTests(unittest.TestCase):
    @staticmethod
    def descriptor(path, data):
        return {"path": path, "bytes": len(data),
                "sha256": hashlib.sha256(data).hexdigest()}

    def setUp(self):
        self.declaration = (ROOT / binding.SUPPORT_DECLARATION_PATH).read_bytes()
        self.rows = list(csv.DictReader(
            io.StringIO(self.declaration.decode("utf-8")), delimiter="\t"))
        self.subjects = sum(row["role"] == "subject" for row in self.rows)
        self.object_rows = (self.subjects * len(binding.TARGETS)
                            * len(binding.FRONTENDS) * len(binding.PIC)
                            * len(binding.ALLOCATORS))

    def test_checked_in_support_bytes_match_reviewed_pin(self):
        self.assertIn(hashlib.sha256(self.declaration).hexdigest(),
                      (binding.SUPPORT_DECLARATION_SHA256,
                       binding.NEXT_SUPPORT_DECLARATION_SHA256))

    def test_support_counts_follow_checked_in_population(self):
        self.assertEqual(binding._approved_support_counts(),
                         (len(self.rows), self.subjects,
                          self.object_rows // len(binding.ALLOCATORS),
                          self.object_rows))
        self.assertEqual(binding.SUPPORT_OBJECT_ROW_COUNT, self.object_rows)
        self.assertEqual(binding.SUPPORT_MIN_STAGE_ROW_COUNT,
                         self.object_rows + len(binding.STAGES) - 1)

    def support_probe(self, root, data=None):
        """Use real support bytes and an explicitly non-accepting next stage."""
        data = self.declaration if data is None else data
        declaration_path = root / binding.SUPPORT_DECLARATION_PATH
        declaration_path.parent.mkdir(parents=True, exist_ok=True)
        declaration_path.write_bytes(data)
        declaration = self.descriptor(binding.SUPPORT_DECLARATION_PATH, data)
        # All required keys are present so the next rejection is specifically
        # manifest_only=1, rather than a missing-key or malformed-TSV failure.
        manifest = {
            "version": "2", "kind": "object-coverage", "identity_hash": "sha256",
            "support_contract": binding.SUPPORT_DECLARATION_PATH,
            "support_contract_sha256": declaration["sha256"],
            "inputs": str(len(self.rows)), "rows": str(self.object_rows),
            "manifest_only": "1", "environment": "explicit-replacement-in-environment.tsv",
            "unfrozen_dependencies": "none-for-object-census", "sysroot": "none",
            "system_include": "none", "resource_include_sha256": "a" * 64,
            "compiler_revision_claim": "a" * 40, "baseline_revision_claim": "b" * 40,
            "compiler_hash": "1", "compiler_bytes": "1", "compiler_sha256": "c" * 64,
            "baseline_hash": "2", "baseline_bytes": "1", "baseline_sha256": "d" * 64,
            "cpu": "baseline",
        }
        manifest_data = "".join(f"{key}={value}\n" for key, value in manifest.items()).encode()
        manifest_path = root / "manifest-only.txt"
        manifest_path.write_bytes(manifest_data)
        files = [None] * len(binding.SUPPORT_FILE_ROLES)
        files[binding.SUPPORT_FILE_ROLES.index("support_declaration")] = declaration
        files[binding.SUPPORT_FILE_ROLES.index("manifest")] = self.descriptor(
            manifest_path.name, manifest_data)
        return {"support": {"files": files}}

    def test_support_output_checks_real_declaration_before_rejecting_manifest_only(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            record = self.support_probe(root)
            with self.assertRaisesRegex(ValueError, "not an executed, SHA-256 census"):
                binding._check_support_output(root, record, None)

    def test_same_population_rehashed_drift_is_not_approval(self):
        changed = bytearray(self.declaration)
        # Change one digest nibble, preserving every row, field and byte count.
        index = len(changed) - 2
        self.assertIn(chr(changed[index]), "0123456789abcdef")
        changed[index] = ord("0") if changed[index] != ord("0") else ord("1")
        self.assertEqual(len(changed), len(self.declaration))
        self.assertEqual(bytes(changed).count(b"\n"), self.declaration.count(b"\n"))
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            record = self.support_probe(root, bytes(changed))
            with self.assertRaisesRegex(ValueError, "not the approved immutable input"):
                binding._check_support_output(root, record, None)

    def test_support_output_rejects_byte_tampering_under_approved_descriptor(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            record = self.support_probe(root)
            path = root / binding.SUPPORT_DECLARATION_PATH
            path.write_bytes(self.declaration[:-1] + b" ")
            with self.assertRaisesRegex(ValueError, "digest does not match evidence"):
                binding._check_support_output(root, record, None)

    def test_support_output_rejects_wrong_path_and_size(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            record = self.support_probe(root)
            descriptor = record["support"]["files"][
                binding.SUPPORT_FILE_ROLES.index("support_declaration")]
            for field, value, message in (
                    ("path", "other.tsv", "not the frozen declaration"),
                    ("bytes", descriptor["bytes"] - 1, "byte count does not match evidence")):
                candidate = copy.deepcopy(record)
                candidate["support"]["files"][
                    binding.SUPPORT_FILE_ROLES.index("support_declaration")][field] = value
                with self.subTest(field=field), self.assertRaisesRegex(ValueError, message):
                    binding._check_support_output(root, candidate, None)

    def check_plan(self, root, pairs, sample_rows, groups):
        support = {"manifest_sha256": "a" * 64, "rows_sha256": "b" * 64,
                   "object_row_count": self.object_rows}
        plan = InvocationEvidenceTests.result_input_plan(
            support["manifest_sha256"], support["rows_sha256"], self.object_rows,
            sample_rows, groups, pairs=pairs)
        data = (json.dumps(plan, sort_keys=True, separators=(",", ":")) + "\n").encode()
        path = root / "plan.json"
        path.write_bytes(data)
        rules = {"sampling": {"rounds": 2, "pairs_per_round": pairs}}
        return binding._result_input_plan(root, self.descriptor(path.name, data),
                                          support, {}, rules)

    def native_population_bounds(self):
        """(A1) Upper bounds of the native-host timed population and groups.

        Timed rows are at most every subject in the 16 native-host
        configurations (before authenticated skips) plus the stage floor.
        Object groups are the configurations times the distinct x86 recipe/CPU
        profiles that #508 assigns to the subjects.
        """
        import native_retirement_contract as census
        subjects = [row["path"] for row in self.rows if row["role"] == "subject"]
        configurations = len(binding.FRONTENDS) * len(binding.PIC) * len(binding.ALLOCATORS)
        recipes = {(census.expected_fixture_recipe(path)[0],
                    census.expected_cpu(path, binding.NATIVE_TIMED_TARGET, "baseline"))
                   for path in subjects}
        sample_rows = len(subjects) * configurations + len(binding.STAGES) - 1
        return sample_rows, configurations * len(recipes)

    def test_native_population_fits_one_partition_per_population(self):
        self.assertEqual(binding.RESULT_INPUT_MAX_TOTAL_RECORDS, 39_518_208)
        sample_rows, groups = self.native_population_bounds()
        self.assertEqual(groups, 80)
        # The contract's 254-pair collection maximum still fits, each
        # population in one partition; so does the 60-pair minimum.
        for pairs in (60, 254):
            with tempfile.TemporaryDirectory() as directory:
                plan = self.check_plan(Path(directory), pairs, sample_rows, groups)
                populations = plan["populations"]
                self.assertEqual(populations["rows"]["sample_count"], sample_rows)
                self.assertEqual(populations["batches"]["sample_count"], groups)
                self.assertEqual([populations[kind]["manifest_count"]
                                  for kind in ("rows", "batches")], [1, 1])
                self.assertLessEqual(sample_rows * 2 * pairs, binding.RESULT_INPUT_MAX_RECORDS)

    def test_current_population_over_cap_cannot_be_rescued_by_more_shards(self):
        # The immutable ceiling is kept: the full census at 256 pairs, even
        # ignoring the native projection, is rejected before any sample.
        sample_rows = self.object_rows + len(binding.STAGES) - 1
        required = sample_rows * 2 * 256
        self.assertGreater(required, binding.RESULT_INPUT_MAX_TOTAL_RECORDS)
        with tempfile.TemporaryDirectory() as directory:
            with self.assertRaisesRegex(ValueError, "immutable total-record ceiling"):
                self.check_plan(Path(directory), 256, sample_rows, 1)


class ReviewBoundaryTests(unittest.TestCase):
    """Small execution-boundary regressions, not performance acceptance."""

    def test_sampling_seed_matches_native_uint64_domain(self):
        # Reuse the existing approved policy fixture without constructing its
        # large census or its synthetic performance evidence.
        import native_retirement_performance_binding_test as fixtures
        for seed in (1, (1 << 64) - 1):
            rules = fixtures.BindingTests._rules()
            rules["sampling"]["seed"] = seed
            with self.subTest(seed=seed):
                binding._rules(rules)
        for seed in (0, -1, False, True, 1 << 64, (1 << 64) + 1):
            rules = fixtures.BindingTests._rules()
            rules["sampling"]["seed"] = seed
            with self.subTest(seed=seed), self.assertRaises(ValueError):
                binding._rules(rules)

    @unittest.skipIf(os.name == "nt", "POSIX command-name dispatcher fixture")
    def test_adapter_compiler_preserves_command_name_sensitive_symlink(self):
        real_compiler = shutil.which("clang") or shutil.which("cc")
        if real_compiler is None:
            self.skipTest("a real C compiler is required")
        real_compiler = os.path.abspath(real_compiler)
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "source"
            throughput = source / "tools" / "throughput"
            throughput.mkdir(parents=True)
            (throughput / "throughput.c").write_text(
                'extern int fixture_value(void);\n'
                'int main(void) { return fixture_value() == 7 ? 0 : 1; }\n')
            (throughput / "shared.c").write_text(
                'int fixture_value(void) { return 7; }\n')
            dispatcher = root / "compiler-manager"
            compiler = root / "clang"
            # Only the lookup is controlled: version discovery, source reads,
            # compilation and the produced native executable are all real.
            dispatcher.write_text(
                "#!" + sys.executable + "\n"
                "import os, sys\n"
                "if os.path.basename(sys.argv[0]) != 'clang':\n"
                "    print('compiler-manager help')\n"
                "    sys.exit(0)\n"
                "os.execv(" + repr(real_compiler) + ", [" +
                repr(real_compiler) + "] + sys.argv[1:])\n")
            dispatcher.chmod(0o755)
            compiler.symlink_to(dispatcher.name)
            with mock.patch.object(binding.shutil, "which", return_value=str(compiler)):
                executable, binary_sha, _source_sha, _toolchain_sha, command = (
                    binding._compile_trusted_retirement_adapter(
                        root, repository_root=source))
            self.assertTrue(command.startswith("clang "))
            self.assertEqual(binary_sha, hashlib.sha256(executable.read_bytes()).hexdigest())
            result = subprocess.run([str(executable)], check=False, capture_output=True)
            self.assertEqual(result.returncode, 0, result.stderr)


class InvocationEvidenceTests(unittest.TestCase):
    """Synthetic authenticated-receipt boundary tests, NOT host acceptance."""

    @staticmethod
    def put(root, path, value):
        data = (value if isinstance(value, bytes) else
                (json.dumps(value, sort_keys=True, separators=(",", ":")) + "\n").encode())
        target = root / path
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(data)
        return {"path": path, "bytes": len(data), "sha256": hashlib.sha256(data).hexdigest()}

    EMPTY_SHA256 = hashlib.sha256(b"").hexdigest()

    @staticmethod
    def digest(text):
        return hashlib.sha256(text.encode()).hexdigest()

    @classmethod
    def rejection_control(cls, fixture="tests/rejection-control.c"):
        """A frozen, status-checked (never timed) rejection control input."""
        return {"fixture": fixture, "row": None, "status": "rejected", "exit_contribution": 1,
                "diagnostic_sha256": cls.digest(f"diagnostic/{fixture}"),
                "object_sha256": None}

    @classmethod
    def execution_plan(cls, root, record, parsed, controls=None):
        """Build a v3 plan: row contracts plus derived batch group contracts."""
        sampling = record["rules"]["sampling"]
        oracle_path = record["workflow"]["records"]["oracle"]["path"]
        oracle_by_row = {item["row"]: item for item in
                         json.loads((root / oracle_path).read_text())["records"]}
        controls = controls or {}
        groups = binding._batch_groups(parsed)
        group_of_row = {row: group["group"] for group in groups for row in group["rows"]}
        group_contracts = []
        for group in groups:
            object_group = group["kind"] == binding.OBJECT_BATCH_GROUP
            group_controls = copy.deepcopy(controls.get(group["group"], []))
            exit_status = 1 if any(item["exit_contribution"] for item in group_controls) else 0
            sides = {}
            for side in ("baseline", "candidate"):
                command = (cls.digest(f"batch/{group['group']}/{side}") if object_group
                           else cls.digest(f"compile/{group['rows'][0]}/{side}"))
                sides[side] = {"command_sha256": command, "exit_status": exit_status}
            group_contracts.append({
                "group": group["group"], "kind": group["kind"],
                "configuration": {field: group["identity"][field]
                                  for field in ("allocator", "frontend_lowering", "PIC")},
                "recipe": {field: group["identity"][field]
                           for field in ("fixture_recipe", "cpu", "cpu_features")},
                "members": [{"row": row,
                             "diagnostic_sha256": cls.EMPTY_SHA256 if object_group else None}
                            for row in group["rows"]],
                "controls": group_controls, **sides})
        contracts = []
        for row in parsed:
            oracle = oracle_by_row[row["row"]]
            timed = binding._timed(row)
            group = group_of_row.get(row["row"]) if timed else None
            compile_eligible = row["metrics"]["compiler_wall_time"]
            observed = oracle["code_section_status"] == "parsed-deterministic"
            contract = {"row": row["row"], "group": group,
                        "identity_sha256": binding._canonical_json_digest(row["identity"]),
                        "oracle_sha256": binding._canonical_json_digest(oracle)}
            for side in ("baseline", "candidate"):
                artifact = (cls.digest(f"artifact/{row['row']}/{side}")
                            if compile_eligible else None)
                code_bytes = oracle["code_section_bytes"] if observed else None
                contract[side] = {
                    "compiler_command_sha256": (group_contracts[group][side]["command_sha256"]
                                                if timed else None),
                    "artifact_sha256": artifact,
                    "reproduction_sha256": artifact if observed and not timed else None,
                    "code_section_sha256": (None if code_bytes is None
                                            else cls.EMPTY_SHA256 if code_bytes == 0
                                            else oracle["code_section_sha256"]),
                    "code_section_bytes": code_bytes,
                    "runtime_command_sha256": ("c" * 64 if row["metrics"]["generated_runtime"]
                                               else None),
                    "runtime_output_sha256": ("d" * 64 if row["metrics"]["generated_runtime"]
                                              else None),
                }
            contracts.append(contract)
        plan = {"schema": binding.EXECUTION_PLAN_SCHEMA, "version": 1,
                "schedule": binding.EXECUTION_SCHEDULE, "cpu": 3,
                "native_target": binding.NATIVE_TIMED_TARGET,
                "performance_rows_sha256": binding._support_file(
                    record["support"], "performance_rows")["sha256"],
                "rows": contracts, "groups": group_contracts}
        for key in ("seed", "rounds", "pairs_per_round", "warmups_per_variant"):
            plan[key] = sampling[key]
        return plan

    @classmethod
    def metrics_records(cls, contract, fixtures, plan_rows, variant, started_ns, intervals,
                        arenas):
        """Per-input metrics records for one batch, members then controls."""
        records = []
        cursor = started_ns + 1
        inputs = [(member["row"], member["diagnostic_sha256"], "compiled", 0,
                   plan_rows[member["row"]][variant]["artifact_sha256"], True)
                  for member in contract["members"]]
        inputs.extend((control["row"], control["diagnostic_sha256"], control["status"],
                       control["exit_contribution"], control["object_sha256"], False)
                      for control in contract["controls"])
        for index, (row, diagnostic, status, contribution, obj, member) in enumerate(inputs):
            interval = intervals[row] if member else 10
            records.append({
                "input": index, "fixture": fixtures[index], "row": row, "status": status,
                "exit_contribution": contribution, "diagnostic_sha256": diagnostic,
                "started_ns": cursor, "finished_ns": cursor + interval,
                "phase_ns": {"backend": interval // 2, "frontend": interval // 4},
                "arena_high_water_bytes": arenas[row] if member else 0,
                "object_sha256": obj,
                "code_sections": [{"bytes": 100, "name": ".text"}] if member else [],
                "code_functions": [{"bytes": 100, "name": "main"}] if member else [],
            })
            cursor += interval
        return records

    @classmethod
    def attach_execution(cls, root, record, parsed, samples, batch_samples=None,
                         controls=None, mutate_metrics=None):
        """Attach a complete test-only v3 plan and batch transcript to a fixture.

        ``samples`` maps (row, round, pair) to metric -> {baseline, candidate};
        ``batch_samples`` maps (group, round, pair) the same way for the batch
        process pair.  Warmups reuse coordinate (unit, 0, 0).  Frozen oracle
        outputs are test values, never deployment evidence.
        ``mutate_metrics(sequence, records)`` may corrupt one metrics artifact.
        """
        sampling = record["rules"]["sampling"]
        plan = cls.execution_plan(root, record, parsed, controls)
        plan_descriptor = cls.put(root, "execution/invocation-plan.json", plan)
        plan_rows = {item["row"]: item for item in plan["rows"]}
        group_contracts = {item["group"]: item for item in plan["groups"]}
        row_by_id = {row["row"]: row for row in parsed}
        groups = {group["group"]: group for group in binding._batch_groups(parsed)}
        events = []
        last_end = 100
        job_id = "synthetic-job"
        attempt = 1
        boot_id = "synthetic-boot"
        for identity in binding._execution_schedule(parsed, sampling):
            event = dict(identity)
            variant = event["variant"]
            sample = event["phase"] == "sample"
            pid = 2000 + event["sequence"]
            process_start_token = f"synthetic-process-{event['sequence']}"
            started = last_end + 1
            metrics_artifact = None
            if event["kind"] == "compiler":
                group = groups[event["group"]]
                contract = group_contracts[group["group"]]
                if group["kind"] == binding.OBJECT_BATCH_GROUP:
                    key = ((group["group"], event["round"], event["pair"]) if sample
                           else (group["group"], 0, 0))
                    process = batch_samples[key]
                    seconds = process["compiler_batch_wall_time"][variant]
                    rss = process["compiler_batch_peak_rss"][variant]
                    intervals, arenas = {}, {}
                    for row in group["rows"]:
                        row_key = ((row, event["round"], event["pair"]) if sample else (row, 0, 0))
                        intervals[row] = int(round(
                            samples[row_key]["compiler_wall_time"][variant] * 1_000_000_000))
                        arenas[row] = samples[row_key]["compiler_peak_memory"][variant]
                    fixtures = ([row_by_id[row]["identity"]["fixture"] for row in group["rows"]]
                                + [item["fixture"] for item in contract["controls"]])
                    metrics = cls.metrics_records(contract, fixtures, plan_rows, variant,
                                                  started, intervals, arenas)
                    output = binding._batch_output_digest(
                        [item["object_sha256"] for item in metrics])
                    if mutate_metrics is not None:
                        mutate_metrics(event["sequence"], metrics)
                    data = b"".join((json.dumps(item, sort_keys=True, separators=(",", ":"))
                                     + "\n").encode() for item in metrics)
                    metrics_artifact = cls.put(
                        root, f"execution/metrics/batch-{event['sequence']:06d}.jsonl", data)
                else:
                    row = group["rows"][0]
                    key = (row, event["round"], event["pair"]) if sample else (row, 0, 0)
                    seconds = samples[key]["compiler_wall_time"][variant]
                    rss = samples[key]["compiler_peak_memory"][variant]
                    output = binding._batch_output_digest(
                        [plan_rows[row][variant]["artifact_sha256"]])
                exit_code = contract[variant]["exit_status"]
                executable = record["subjects"][variant]["binary"]["sha256"]
                command = contract[variant]["command_sha256"]
            else:
                side = plan_rows[event["row"]][variant]
                key = ((event["row"], event["round"], event["pair"]) if sample
                       else (event["row"], 0, 0))
                seconds = samples[key]["generated_runtime"][variant]
                rss = None
                output = side["runtime_output_sha256"]
                exit_code = 0
                executable = side["artifact_sha256"]
                command = side["runtime_command_sha256"]
            duration_ns = int(round(seconds * 1_000_000_000))
            event.update({
                "pid": pid, "process_start_token": process_start_token,
                "process_instance_sha256": binding._process_instance_digest(
                    job_id, attempt, boot_id, pid, process_start_token),
                "cpu": 3, "started_ns": started, "finished_ns": started + duration_ns,
                "exit_code": exit_code, "signal": 0, "timed_out": False, "cancelled": False,
                "executable_sha256": executable, "command_sha256": command,
                "output_sha256": output, "code_section_sha256": None,
                "code_section_bytes": None, "wall_seconds": seconds,
                "peak_rss_bytes": rss, "metrics_artifact": metrics_artifact,
            })
            last_end = event["finished_ns"]
            events.append(event)
        raw_digest = "9" * 64
        receipt = {"schema": binding.EXECUTION_RECEIPT_SCHEMA, "version": 1,
                   "context_sha256": binding._canonical_json_digest(binding._execution_context(record, raw_digest)),
                   "execution_plan_sha256": plan_descriptor["sha256"], "job_id": job_id,
                   "attempt": attempt, "boot_id": boot_id, "bound_at_ns": 100,
                   "completed_at_ns": last_end + 1, "invocations": len(events), "shards": []}
        descriptor = cls.write_transcript(root, receipt, events)
        return plan_descriptor, receipt, descriptor, events, raw_digest

    @classmethod
    def write_transcript(cls, root, receipt, events):
        data = b"".join((json.dumps(event, sort_keys=True, separators=(",", ":")) + "\n").encode()
                        for event in events)
        shard = cls.put(root, "execution/invocations.jsonl", data)
        receipt["shards"] = [{**shard, "records": len(events)}]
        return cls.put(root, "execution/invocation-receipt.json", receipt)

    @classmethod
    def code_records(cls, root, plan_descriptor, parsed, path="execution/code-records.jsonl",
                     mutate=None):
        """Write the per-row code record set from a plan's frozen code facts."""
        plan = json.loads((root / plan_descriptor["path"]).read_text())
        by_row = {item["row"]: item for item in plan["rows"]}
        lines = []
        for row in sorted(parsed, key=lambda item: item["row"]):
            if not binding._code_observed(row):
                continue
            value = {"row": row["row"]}
            for side in ("baseline", "candidate"):
                frozen = by_row[row["row"]][side]
                value[side] = {"artifact_sha256": frozen["artifact_sha256"],
                               "code_section_bytes": frozen["code_section_bytes"],
                               "code_section_sha256": frozen["code_section_sha256"],
                               "reproduction_sha256": frozen["artifact_sha256"]}
            if mutate is not None:
                mutate(value)
            lines.append(value)
        data = b"".join((json.dumps(item, sort_keys=True, separators=(",", ":")) + "\n").encode()
                        for item in lines)
        descriptor = cls.put(root, path, data)
        return {**descriptor, "records": len(lines)}

    @staticmethod
    def result_input_plan(source_manifest, source_rows, object_rows, row_count, group_count,
                          rounds=2, pairs=60, row_path="results/input.json",
                          batch_path="results/batch-input.json"):
        """A v3 result-input plan with one row and one batch partition set."""
        cap = binding.RESULT_INPUT_MAX_RECORDS
        populations = {}
        for kind, count, path in (("rows", row_count, row_path),
                                  ("batches", group_count, batch_path)):
            required = count * rounds * pairs
            manifests = [{"identity": f"{kind}-{index}",
                          "path": path if index == 0 else f"{path}.{index}",
                          "start_record": start, "records": min(cap, required - start)}
                         for index, start in enumerate(range(0, required, cap))]
            populations[kind] = dict(binding.RESULT_INPUT_POPULATIONS[kind],
                                     sample_count=count, records_per_unit=rounds * pairs,
                                     required_records=required,
                                     manifest_count=len(manifests), manifests=manifests)
        return {"schema": binding.RESULT_INPUT_PLAN_SCHEMA, "version": 1,
                "source_manifest_sha256": source_manifest, "source_rows_sha256": source_rows,
                "identity_field": "record_id", "timed_target": binding.NATIVE_TIMED_TARGET,
                "object_row_count": object_rows, "rounds": rounds, "pairs_per_round": pairs,
                "max_records_per_manifest": cap, "populations": populations,
                "predeclared": True}

    @staticmethod
    def create_sample_tables(db):
        db.execute("CREATE TABLE samples(row_id INTEGER, round_id INTEGER, pair_id INTEGER, "
                   "metric TEXT, baseline TEXT, candidate TEXT, "
                   "PRIMARY KEY(row_id, round_id, pair_id, metric))")
        db.execute("CREATE TABLE batch_samples(group_id INTEGER, round_id INTEGER, "
                   "pair_id INTEGER, metric TEXT, baseline TEXT, candidate TEXT, "
                   "PRIMARY KEY(group_id, round_id, pair_id, metric))")

    @staticmethod
    def synthetic_samples(rows, db=None):
        """Deterministic per-row and per-batch samples with exact nanosecond seconds."""
        samples, batch_samples = {}, {}
        for row in rows:
            for round_id in range(2):
                for pair in range(60):
                    metrics = {}
                    for metric in binding.ROW_SAMPLE_METRICS:
                        if not row["metrics"].get(metric, False):
                            continue
                        values = {}
                        for side_index, side in enumerate(("baseline", "candidate")):
                            if metric in ("compiler_wall_time", "generated_runtime"):
                                values[side] = (1_000_000 + 1000 * row["row"] + 100 * side_index
                                                + 60 * round_id + pair) / 1e9
                            else:
                                values[side] = 100 + side_index
                        metrics[metric] = values
                        if db is not None:
                            db.execute("INSERT INTO samples VALUES (?, ?, ?, ?, ?, ?)",
                                       (row["row"], round_id, pair, metric,
                                        str(values["baseline"]), str(values["candidate"])))
                    samples[(row["row"], round_id, pair)] = metrics
        for group in binding._object_groups(binding._batch_groups(rows)):
            for round_id in range(2):
                for pair in range(60):
                    metrics = {
                        "compiler_batch_wall_time": {
                            side: (10_000_000 + 1000 * group["group"] + 100 * index
                                   + 60 * round_id + pair) / 1e9
                            for index, side in enumerate(("baseline", "candidate"))},
                        "compiler_batch_peak_rss": {
                            side: 4096 + index
                            for index, side in enumerate(("baseline", "candidate"))},
                    }
                    if db is not None:
                        for metric, values in metrics.items():
                            db.execute("INSERT INTO batch_samples VALUES (?, ?, ?, ?, ?, ?)",
                                       (group["group"], round_id, pair, metric,
                                        str(values["baseline"]), str(values["candidate"])))
                    batch_samples[(group["group"], round_id, pair)] = metrics
        return samples, batch_samples

    def setUp(self):
        import sqlite3
        from native_retirement_performance_binding_test import BindingTests
        self.directory = tempfile.TemporaryDirectory(prefix="invocation-regression-")
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)
        self.db = sqlite3.connect(":memory:")
        self.addCleanup(self.db.close)
        self.create_sample_tables(self.db)
        self.rules = BindingTests._rules()
        template = BindingTests._series_join_fixture()[0][0]
        # Row 0 is a native link row (singleton group 0 with runtime); row 1 is
        # a native object row (object batch group 1 with one rejection control).
        self.rows = []
        for i in range(2):
            row = copy.deepcopy(template)
            row["row"] = i
            row["identity"]["fixture"] = f"tests/invocation-{i}.c"
            row["identity"]["artifact_stage"] = "link" if i == 0 else "object"
            row["metrics"]["generated_runtime"] = i == 0
            row["eligibility"]["generated_runtime"] = i == 0
            row["eligibility"]["runtime_oracle"] = (
                "independent-native-executable-oracle" if i == 0 else "not-applicable")
            self.rows.append(row)
        self.family = binding._derive_statistical_family(self.rows)
        self.rules["sampling"].update(binding._family_member_counts(self.family))
        artifact = {"path": "placeholder", "bytes": 1, "sha256": "a" * 64}
        self.oracle_records = [
            {"row": i, "code_section_status": "parsed-deterministic",
             "code_section_bytes": 100, "code_section_sha256": "b" * 64,
             "runtime_oracle_status": "passed-native" if i == 0 else "not-applicable",
             "runtime_exit_code": 0 if i == 0 else -1, "native_runtime": i == 0}
            for i in range(2)]
        oracle = self.put(self.root, "execution/oracle.json", {"records": self.oracle_records})
        self.record = {
            "subjects": {side: {"binary": {**artifact, "sha256": str(i + 1) * 64}}
                         for i, side in enumerate(("baseline", "candidate"))},
            "support": {"root_sha256": "3" * 64,
                        "files": [{"name": role, **artifact} for role in binding.SUPPORT_FILE_ROLES]},
            "measurement": {"harness_binary": artifact}, "execution": {"service": "synthetic"},
            "rules": self.rules,
            "workflow": {"phases": {name: {**artifact, "sha256": str(i + 4) * 64}
                                      for i, name in enumerate(("pre_sample_plan", "post_aa_binding"))},
                         "records": {"admission": artifact, "oracle": oracle}},
        }
        self.controls = {1: [self.rejection_control()]}
        self.samples, self.batch_samples = self.synthetic_samples(self.rows, self.db)
        self.attach()

    def attach(self, **options):
        (self.plan, self.receipt, self.descriptor, self.events, self.raw_digest) = \
            self.attach_execution(self.root, self.record, self.rows, self.samples,
                                  self.batch_samples, options.pop("controls", self.controls),
                                  **options)

    def check(self, descriptor=None, trusted=None):
        descriptor = descriptor or self.descriptor
        return binding._check_execution_transcript(
            self.root, descriptor, self.plan, self.record, self.rows,
            self.rules["sampling"], self.db, self.raw_digest,
            trusted if trusted is not None else descriptor["sha256"],
            3, "x86_64-unknown-linux-gnu")

    def rewrite_plan(self, change):
        """Apply ``change`` to the frozen plan and rebind the receipt to it."""
        plan = json.loads((self.root / self.plan["path"]).read_text())
        change(plan)
        self.plan = self.put(self.root, self.plan["path"], plan)
        self.receipt["execution_plan_sha256"] = self.plan["sha256"]
        self.descriptor = self.write_transcript(self.root, self.receipt, self.events)

    def test_complete_warmups_and_native_runtime_join(self):
        # (A1) (G + U) * 2 * (warmups + rounds * pairs): one singleton stage
        # group, one object batch group, and one runtime-eligible row.
        result = self.check()
        self.assertEqual(result["invocations"], (2 + 1) * 2 * (2 + 2 * 60))
        batches = [event for event in self.events
                   if event["kind"] == "compiler" and event["group"] == 1]
        self.assertEqual(len(batches), 2 * (2 + 2 * 60))
        self.assertTrue(all(event["metrics_artifact"] is not None and event["exit_code"] == 1
                            for event in batches))
        self.assertTrue(all(event["row"] is None and event["code_section_bytes"] is None
                            for event in self.events if event["kind"] == "compiler"))

    def add_untimed_rows(self):
        """Append a retained non-object control and a cross-target object row."""
        template = self.rows[1]
        control = copy.deepcopy(template)
        control["row"] = 2
        control["identity"]["fixture"] = "tests/untimed-control.c"
        control["identity"]["compile_obligation"] = "registered-non-object-control"
        for metric in binding.ROW_METRICS:
            control["metrics"][metric] = False
        for field in ("compiler_wall_time", "compiler_peak_rss", "generated_code_bytes",
                      "generated_runtime"):
            control["eligibility"][field] = False
        control["eligibility"]["code_section"] = "not-applicable"
        cross = copy.deepcopy(template)
        cross["row"] = 3
        cross["identity"]["fixture"] = "tests/invocation-cross.c"
        cross["identity"]["target"] = "aarch64-unknown-linux-gnu"
        cross["identity"]["target_abi"] = "aapcs64"
        self.rows.extend((control, cross))
        self.oracle_records.extend((
            {"row": 2, "code_section_status": "not-applicable",
             "code_section_bytes": None, "code_section_sha256": None,
             "runtime_oracle_status": "not-applicable",
             "runtime_exit_code": None, "native_runtime": False},
            {"row": 3, "code_section_status": "parsed-deterministic",
             "code_section_bytes": 100, "code_section_sha256": "b" * 64,
             "runtime_oracle_status": "not-applicable",
             "runtime_exit_code": -1, "native_runtime": False}))
        oracle_path = self.record["workflow"]["records"]["oracle"]["path"]
        self.record["workflow"]["records"]["oracle"] = self.put(
            self.root, oracle_path, {"records": self.oracle_records})

    def test_mixed_untimed_empty_code_and_cross_target_rows_replay_complete_transcript(self):
        # The zero-code row still runs its compiler; the retained control and
        # the cross-target row are never timed, and neither joins a group.
        empty = hashlib.sha256(b"").hexdigest()
        self.add_untimed_rows()
        zero = self.rows[1]
        zero["metrics"]["generated_code_bytes"] = False
        zero["eligibility"]["generated_code_bytes"] = False
        zero["eligibility"]["code_section"] = "deterministic-zero-baseline-code-section"
        self.oracle_records[1].update(code_section_bytes=0, code_section_sha256=empty)
        oracle_path = self.record["workflow"]["records"]["oracle"]["path"]
        self.record["workflow"]["records"]["oracle"] = self.put(
            self.root, oracle_path, {"records": self.oracle_records})
        self.attach()
        self.assertEqual(self.check()["invocations"], (2 + 1) * 2 * (2 + 2 * 60))
        self.assertEqual([group["rows"] for group in binding._batch_groups(self.rows)],
                         [[0], [1]])
        self.assertFalse({2, 3} & {event["row"] for event in self.events})
        plan = json.loads((self.root / self.plan["path"]).read_text())
        rows = {item["row"]: item for item in plan["rows"]}
        self.assertEqual((rows[1]["baseline"]["code_section_bytes"],
                          rows[1]["baseline"]["code_section_sha256"]), (0, empty))
        self.assertIsNone(rows[3]["group"])
        self.assertIsNone(rows[3]["baseline"]["compiler_command_sha256"])
        self.assertEqual(rows[3]["baseline"]["reproduction_sha256"],
                         rows[3]["baseline"]["artifact_sha256"])
        self.assertTrue(all(value is None for value in rows[2]["candidate"].values()))

    def test_missing_or_self_selected_trust_root_is_rejected(self):
        for trusted in (None, "0" * 64, "main"):
            with self.subTest(trusted=trusted), self.assertRaises(ValueError):
                binding._check_execution_transcript(
                    self.root, self.descriptor, self.plan, self.record, self.rows,
                    self.rules["sampling"], self.db, self.raw_digest, trusted,
                    3, "x86_64-unknown-linux-gnu")
        original_trust = self.descriptor["sha256"]
        self.receipt["job_id"] = "another-job"
        changed = self.write_transcript(self.root, self.receipt, self.events)
        with self.assertRaisesRegex(ValueError, "independently trusted"):
            self.check(changed, original_trust)

    def test_receipt_bytes_are_authenticated_before_json_parsing(self):
        path = self.root / self.descriptor["path"]
        path.write_bytes(b"x" * self.descriptor["bytes"])
        with mock.patch.object(binding.json, "loads", wraps=json.loads) as loads:
            with self.assertRaisesRegex(ValueError, "bytes do not match the independently trusted"):
                self.check()
            loads.assert_not_called()

    def test_fresh_supervisor_process_instance_is_required(self):
        events = copy.deepcopy(self.events)
        events[0]["pid"] = 42
        with self.assertRaisesRegex(ValueError, "not supervisor-bound"):
            self.check(self.write_transcript(self.root, self.receipt, events))

        events = copy.deepcopy(self.events)
        for event in events:
            event["pid"] = 42
            event["process_start_token"] = "persistent-worker"
            event["process_instance_sha256"] = binding._process_instance_digest(
                self.receipt["job_id"], self.receipt["attempt"],
                self.receipt["boot_id"], 42, "persistent-worker")
        with self.assertRaisesRegex(ValueError, "reuses a process instance"):
            self.check(self.write_transcript(self.root, self.receipt, events))

    def test_execution_plan_cpu_must_match_admitted_profile(self):
        plan = json.loads((self.root / self.plan["path"]).read_text())
        plan["cpu"] = 999999
        plan_descriptor = self.put(self.root, self.plan["path"], plan)
        events = copy.deepcopy(self.events)
        for event in events:
            event["cpu"] = 999999
        receipt = copy.deepcopy(self.receipt)
        receipt["execution_plan_sha256"] = plan_descriptor["sha256"]
        descriptor = self.write_transcript(self.root, receipt, events)
        with self.assertRaisesRegex(ValueError, "CPU differs from the admitted host profile"):
            binding._check_execution_transcript(
                self.root, descriptor, plan_descriptor, self.record, self.rows,
                self.rules["sampling"], self.db, self.raw_digest,
                descriptor["sha256"], 3, "x86_64-unknown-linux-gnu")

    def test_wrong_order_missing_duplicate_and_extra_invocations_reject(self):
        cases = [self.events[1:], self.events + [self.events[-1]],
                 [self.events[1], self.events[0]] + self.events[2:],
                 [self.events[0], self.events[0]] + self.events[2:]]
        for index, events in enumerate(cases):
            with self.subTest(case=index), self.assertRaises(ValueError):
                self.check(self.write_transcript(self.root, self.receipt, events))

    def batch_index(self, phase="sample"):
        return next(index for index, event in enumerate(self.events)
                    if event["kind"] == "compiler" and event["group"] == 1
                    and event["phase"] == phase)

    def test_rehashed_failed_runtime_and_warmup_invocations_reject(self):
        runtime = next(i for i, event in enumerate(self.events)
                       if event["kind"] == "runtime" and event["phase"] == "sample")
        for index in (0, runtime):
            for field, bad in (("exit_code", 1), ("exit_code", False), ("signal", 9),
                               ("timed_out", True), ("cancelled", True)):
                events = copy.deepcopy(self.events)
                events[index][field] = bad
                with self.subTest(index=index, field=field, bad=bad), self.assertRaises(ValueError):
                    self.check(self.write_transcript(self.root, self.receipt, events))
        # A batch with a frozen rejection control must exit with its frozen
        # nonzero status; a clean exit contradicts the control's oracle.
        for phase in ("warmup", "sample"):
            for field, bad in (("exit_code", 0), ("signal", 9), ("timed_out", True)):
                events = copy.deepcopy(self.events)
                events[self.batch_index(phase)][field] = bad
                with self.subTest(phase=phase, field=field), \
                        self.assertRaisesRegex(ValueError, "did not complete successfully"):
                    self.check(self.write_transcript(self.root, self.receipt, events))

    def test_mismatched_binary_command_oracle_and_sections_reject(self):
        for index in (0, self.batch_index()):
            for field, bad in (("executable_sha256", "0" * 64), ("command_sha256", "0" * 64),
                               ("output_sha256", "0" * 64), ("code_section_sha256", "0" * 64),
                               ("code_section_bytes", 2), ("cpu", 4)):
                events = copy.deepcopy(self.events)
                events[index][field] = bad
                with self.subTest(index=index, field=field), self.assertRaises(ValueError):
                    self.check(self.write_transcript(self.root, self.receipt, events))

    def test_workflow_checks_actual_invocations_before_calling_statistics(self):
        # Exercise the production workflow, real #615 streaming of both result
        # populations, and the receipt join. The sole sentinel is the NEXT
        # stage: this deliberately does not stand in for the full replay test.
        support = {"support_declaration_sha256": "a" * 64,
                   "manifest_sha256": "b" * 64, "rows_sha256": "c" * 64,
                   "object_row_count": len(self.rows)}

        def records(prefix, samples):
            return b"".join((json.dumps({
                "record_id": f"{prefix}-{unit}/round-{round_id}/pair-{pair}",
                prefix: unit, "round": round_id, "pair": pair, "measurements": measurements,
            }, sort_keys=True, separators=(",", ":")) + "\n").encode()
                for (unit, round_id, pair), measurements in sorted(samples.items()))

        data = records("row", self.samples)
        batch_data = records("group", self.batch_samples)
        shard = self.put(self.root, "results/numeric.jsonl", data)
        manifest = self.put(self.root, "results/input.json", {
            "schema": binding.RESULT_INPUT.MANIFEST_SCHEMA, "version": 1,
            "identity_field": "record_id", "shards": [{"identity": "numeric", **shard}]})
        batch_shard = self.put(self.root, "results/batch-numeric.jsonl", batch_data)
        batch_manifest = self.put(self.root, "results/batch-input.json", {
            "schema": binding.RESULT_INPUT.MANIFEST_SCHEMA, "version": 1,
            "identity_field": "record_id",
            "shards": [{"identity": "batch-numeric", **batch_shard}]})
        plan = self.put(self.root, "results/input-plan.json", self.result_input_plan(
            support["manifest_sha256"], support["rows_sha256"], 2, 2, 1))
        self.record["workflow"]["records"]["result_input_plan"] = plan
        self.record["execution"]["host"] = {"aa_admission_receipt": {"sha256": "7" * 64}}
        pre = {"schema": binding.PHASE_SCHEMA["pre_sample_plan"], "version": 1,
               "status": "frozen-before-samples", "support_declaration_sha256": "a" * 64,
               "manifest_sha256": "b" * 64, "rows_sha256": "c" * 64,
               "family_sha256": self.family["sha256"], "result_input_plan_sha256": plan["sha256"],
               "execution_plan": self.plan}
        for key in ("seed", "rounds", "pairs_per_round", "resamples",
                    "bootstrap_members_per_scope", "cell_members_per_scope"):
            pre[key] = self.rules["sampling"][key]
        phases = self.record["workflow"]["phases"]
        phases["pre_sample_plan"] = self.put(self.root, "workflow/pre.json", pre)
        post = dict(pre, schema=binding.PHASE_SCHEMA["post_aa_binding"],
                    status="bound-after-aa-before-samples",
                    pre_sample_plan_sha256=phases["pre_sample_plan"]["sha256"],
                    aa_admission_sha256="7" * 64)
        phases["post_aa_binding"] = self.put(self.root, "workflow/post.json", post)
        self.raw_digest = hashlib.sha256(data + batch_data).hexdigest()
        self.receipt["context_sha256"] = binding._canonical_json_digest(
            binding._execution_context(self.record, self.raw_digest))
        self.descriptor = self.write_transcript(self.root, self.receipt, self.events)
        adapter = self.put(self.root, "results/series.txt", b"not reached by this boundary test\n")
        code_records = self.code_records(self.root, self.plan, self.rows)
        result = {"schema": binding.RESULT_BUNDLE_SCHEMA, "version": 1,
                  "source_rows_sha256": "c" * 64, "result_input_plan_sha256": plan["sha256"],
                  "family_sha256": self.family["sha256"], "result_manifests": [{
                      "identity": "rows-0", **manifest, "start_record": 0, "records": 240,
                      "input_bytes": manifest["bytes"] + shard["bytes"]}],
                  "batch_result_manifests": [{
                      "identity": "batches-0", **batch_manifest, "start_record": 0,
                      "records": 120,
                      "input_bytes": batch_manifest["bytes"] + batch_shard["bytes"]}],
                  "raw_measurements_sha256": self.raw_digest,
                  "member_invocations_sha256": binding._family_invocation_digest(self.family),
                  "member_count": len(self.family["members"]), "scopes_per_member": 3,
                  "adapter_input": adapter, "execution_receipt": self.descriptor,
                  "code_records": code_records,
                  "code_bytes_summary": binding._code_bytes_summary(
                      self.rows, {0: (100, 100), 1: (100, 100)})}
        result_descriptor = self.put(self.root, "results/bundle.json", result)
        sealed = {"schema": binding.SEALED_RESULT_SCHEMA, "version": 1,
                  "status": "sealed-for-independent-replay",
                  "post_aa_binding_sha256": phases["post_aa_binding"]["sha256"],
                  "result_input_plan_sha256": plan["sha256"], "family_sha256": self.family["sha256"],
                  "result_bundle": result_descriptor, "seal": {}}
        phases["sealed_result"] = self.put(self.root, "workflow/sealed.json", sealed)

        def run(trust):
            return binding._check_workflow_evidence(
                self.root, self.record, self.record["workflow"], support,
                (self.rows, {}, self.family), {}, self.rules,
                trusted_execution_receipt_sha256=trust,
                admitted_cpu=3, native_target="x86_64-unknown-linux-gnu")

        with mock.patch.object(binding, "_check_adapter_series",
                               side_effect=RuntimeError("statistics boundary reached")) as statistics:
            with self.assertRaisesRegex(ValueError, "independently obtained"):
                run(None)
            statistics.assert_not_called()
            with self.assertRaisesRegex(RuntimeError, "statistics boundary reached"):
                run(self.descriptor["sha256"])
            statistics.assert_called_once()
            statistics.reset_mock()
            # Batch cell population mismatch: the plan must cover every object
            # batch group, not a producer-chosen count.
            self.record["workflow"]["records"]["result_input_plan"] = self.put(
                self.root, "results/wrong-batch-plan.json", self.result_input_plan(
                    support["manifest_sha256"], support["rows_sha256"], 2, 2, 2))
            with self.assertRaisesRegex(ValueError, "batch population is not every object batch group"):
                run(self.descriptor["sha256"])
            self.record["workflow"]["records"]["result_input_plan"] = plan
            # A missing batch metric in the sealed group records fails closed.
            broken = json.loads(batch_data.splitlines()[0])
            del broken["measurements"]["compiler_batch_peak_rss"]
            broken_data = (json.dumps(broken, sort_keys=True, separators=(",", ":"))
                           + "\n").encode() + b"".join(
                               line + b"\n" for line in batch_data.splitlines()[1:])
            self.put(self.root, "results/batch-numeric.jsonl", broken_data)
            broken_shard = {"identity": "batch-numeric", "path": "results/batch-numeric.jsonl",
                            "bytes": len(broken_data),
                            "sha256": hashlib.sha256(broken_data).hexdigest()}
            broken_manifest = self.put(self.root, "results/batch-input.json", {
                "schema": binding.RESULT_INPUT.MANIFEST_SCHEMA, "version": 1,
                "identity_field": "record_id", "shards": [broken_shard]})
            result["batch_result_manifests"][0].update(
                broken_manifest, input_bytes=broken_manifest["bytes"] + broken_shard["bytes"])
            sealed["result_bundle"] = self.put(self.root, "results/bundle.json", result)
            phases["sealed_result"] = self.put(self.root, "workflow/sealed.json", sealed)
            with self.assertRaisesRegex(ValueError, "compiler_batch_peak_rss"):
                run(self.descriptor["sha256"])
            statistics.assert_not_called()
            self.put(self.root, "results/batch-numeric.jsonl", batch_data)
            self.put(self.root, "results/batch-input.json", {
                "schema": binding.RESULT_INPUT.MANIFEST_SCHEMA, "version": 1,
                "identity_field": "record_id",
                "shards": [{"identity": "batch-numeric", **batch_shard}]})
            result["batch_result_manifests"][0].update(
                batch_manifest, input_bytes=batch_manifest["bytes"] + batch_shard["bytes"])
            events = copy.deepcopy(self.events)
            events[0]["exit_code"] = 1
            changed = self.write_transcript(self.root, self.receipt, events)
            result["execution_receipt"] = changed
            sealed["result_bundle"] = self.put(self.root, "results/bundle.json", result)
            phases["sealed_result"] = self.put(self.root, "workflow/sealed.json", sealed)
            with self.assertRaisesRegex(ValueError, "did not complete successfully"):
                run(changed["sha256"])
            statistics.assert_not_called()

    def test_rehashed_failed_or_non_native_oracles_reject_before_statistics(self):
        original = copy.deepcopy(self.oracle_records)
        oracle_path = self.record["workflow"]["records"]["oracle"]["path"]
        for field, bad in (("runtime_oracle_status", "not-applicable"),
                           ("runtime_exit_code", 1), ("runtime_exit_code", False),
                           ("native_runtime", False), ("code_section_status", "unknown")):
            oracle = copy.deepcopy(original)
            oracle[0][field] = bad
            self.record["workflow"]["records"]["oracle"] = self.put(
                self.root, oracle_path, {"records": oracle})
            self.attach()
            with self.subTest(field=field, bad=bad), self.assertRaisesRegex(ValueError, "oracle"):
                self.check()

    def test_runtime_applicability_is_derived_from_frozen_row_obligation(self):
        rows = copy.deepcopy(self.rows)
        rows[0]["identity"]["execution_obligation"] = "unavailable-platform-control"
        plan, receipt, descriptor, _events, raw_digest = self.attach_execution(
            self.root, self.record, rows, self.samples, self.batch_samples, self.controls)
        with self.assertRaisesRegex(ValueError, "frozen row obligation"):
            binding._check_execution_transcript(
                self.root, descriptor, plan, self.record, rows,
                self.rules["sampling"], self.db, raw_digest,
                descriptor["sha256"], 3, "x86_64-unknown-linux-gnu")

    def reset_samples(self):
        self.db.execute("DELETE FROM samples")
        self.db.execute("DELETE FROM batch_samples")
        self.synthetic_samples(self.rows, self.db)

    def test_positive_but_wrong_result_measurement_rejects(self):
        # Row samples join to singleton processes and per-input records;
        # batch samples join to the batch process's own wall time and RSS.
        for table, metric, change in (
                ("samples", "compiler_peak_memory", "baseline='1000'"),
                ("samples", "compiler_wall_time", "candidate='0.5'"),
                ("batch_samples", "compiler_batch_peak_rss", "baseline='1000'"),
                ("batch_samples", "compiler_batch_wall_time", "candidate='0.5'")):
            self.reset_samples()
            self.db.execute(f"UPDATE {table} SET {change} WHERE metric=?", (metric,))
            with self.subTest(metric=metric), \
                    self.assertRaisesRegex(ValueError, "not the authenticated invocation"):
                self.check()
        # One nanosecond of decimal-seconds error is the only per-input slack.
        self.reset_samples()
        self.db.execute("UPDATE samples SET candidate=? WHERE row_id=1 AND round_id=0 "
                        "AND pair_id=0 AND metric='compiler_wall_time'",
                        (str(float(self.samples[(1, 0, 0)]["compiler_wall_time"]["candidate"])
                             + 2e-9),))
        with self.assertRaisesRegex(ValueError, "per-input record"):
            self.check()

    def test_overlapping_processes_and_false_clock_measurement_reject(self):
        for field, bad in (("started_ns", 99), ("finished_ns", 10), ("wall_seconds", 50),
                           ("row", False), ("group", None), ("sequence", False),
                           ("peak_rss_bytes", True)):
            events = copy.deepcopy(self.events)
            events[0][field] = bad
            with self.subTest(field=field), self.assertRaises(ValueError):
                self.check(self.write_transcript(self.root, self.receipt, events))

    def batch_sequences(self):
        return [event["sequence"] for event in self.events
                if event["kind"] == "compiler" and event["group"] == 1]

    def assert_metrics_rejected(self, mutate, message, sequences=None):
        for target in sequences or (self.batch_sequences()[0], self.batch_sequences()[-1]):
            def corrupt(sequence, records, target=target):
                if sequence == target:
                    mutate(records)
            self.attach(mutate_metrics=corrupt)
            with self.subTest(sequence=target, message=message), \
                    self.assertRaisesRegex(ValueError, message):
                self.check()

    def test_cross_target_rows_are_never_timed(self):
        self.add_untimed_rows()
        self.attach()
        self.check()
        for field, value in (("compiler_command_sha256", "e" * 64),):
            def change(plan, field=field, value=value):
                row = next(item for item in plan["rows"] if item["row"] == 3)
                row["baseline"][field] = value
            self.rewrite_plan(change)
            with self.assertRaisesRegex(ValueError, "cross-target rows are never timed"):
                self.check()
        # A cross-target row cannot be moved into a batch group either.
        self.attach()

        def join_group(plan):
            next(item for item in plan["rows"] if item["row"] == 3)["group"] = 1
        self.rewrite_plan(join_group)
        with self.assertRaisesRegex(ValueError, "derived batch group"):
            self.check()

    def test_batch_objects_must_reproduce_their_frozen_artifacts(self):
        # Determinism: every warmup and sample batch must emit each fixture's
        # object byte-identical to its frozen artifact.
        def mismatch(records):
            records[0]["object_sha256"] = "f" * 64
        self.assert_metrics_rejected(mismatch, "nondeterminism")

    def test_per_input_intervals_are_ordered_and_inside_the_batch(self):
        def overlap(records):
            records[1]["started_ns"] = records[0]["finished_ns"] - 1
            records[1]["finished_ns"] = records[1]["started_ns"] + 10

        def early(records):
            records[0]["started_ns"] -= 2

        def late(records):
            records[-1]["finished_ns"] += 10 ** 9

        def empty(records):
            records[0]["finished_ns"] = records[0]["started_ns"]

        for mutate in (overlap, early, late, empty):
            self.assert_metrics_rejected(mutate, "per-input intervals")

        def phases(records):
            interval = records[0]["finished_ns"] - records[0]["started_ns"]
            records[0]["phase_ns"] = {"backend": interval, "frontend": 1}
        self.assert_metrics_rejected(phases, "phase timings exceed")

    def test_control_status_and_diagnostics_match_the_frozen_oracle(self):
        for field, value in (("status", "compiled"), ("exit_contribution", 0),
                             ("diagnostic_sha256", "e" * 64)):
            def change(records, field=field, value=value):
                records[1][field] = value
            self.assert_metrics_rejected(change, "status, exit contribution or diagnostics")

        def member_diagnostic(records):
            records[0]["diagnostic_sha256"] = "e" * 64
        self.assert_metrics_rejected(member_diagnostic, "status, exit contribution or diagnostics")

        def reorder(records):
            records.reverse()
        self.assert_metrics_rejected(reorder, "frozen batch input order")

        def inconsistent_control(plan):
            plan["groups"][1]["controls"][0]["status"] = "compiled"
        self.rewrite_plan(inconsistent_control)
        with self.assertRaisesRegex(ValueError, "status, exit contribution and object disagree"):
            self.check()
        self.attach()

        def clean_exit(plan):
            for side in ("baseline", "candidate"):
                plan["groups"][1][side]["exit_status"] = 0
        self.rewrite_plan(clean_exit)
        with self.assertRaisesRegex(ValueError, "exit status contradicts"):
            self.check()

    def test_metrics_artifacts_are_bounded_and_exactly_the_frozen_inputs(self):
        _plan, groups, rows = binding._check_execution_plan(
            self.root, self.plan, self.record, self.rows, self.rules["sampling"], 3,
            binding.NATIVE_TIMED_TARGET)
        event = self.events[self.batch_index()]
        oversized = dict(event["metrics_artifact"], bytes=binding.METRICS_ARTIFACT_BYTE_CAP + 1)
        with mock.patch.object(binding, "_check_evidence") as read:
            with self.assertRaisesRegex(ValueError, "exceeds its bounded size"):
                binding._check_batch_metrics(
                    self.root, oversized, groups[1], rows, {row["row"]: row for row in self.rows},
                    event["variant"], event["started_ns"], event["finished_ns"], "metrics")
            read.assert_not_called()

        def huge_record(records):
            records[0]["code_functions"] = [{"bytes": 1, "name": "f" * binding.METRICS_RECORD_BYTE_CAP}]
        self.assert_metrics_rejected(huge_record, "missing, truncated, or oversized")

        def extra(records):
            records.append(dict(records[-1], input=len(records)))
        self.assert_metrics_rejected(extra, "undeclared extra records")

        def missing(records):
            records.pop()
        self.assert_metrics_rejected(missing, "missing, truncated, or oversized")

    def test_batch_process_metrics_and_artifacts_are_required(self):
        for index in (self.batch_index("warmup"), self.batch_index()):
            for field, value, message in (
                    ("metrics_artifact", None, "lacks its per-input metrics artifact"),
                    ("peak_rss_bytes", None, "peak_rss_bytes"),
                    ("peak_rss_bytes", 0, "peak_rss_bytes")):
                events = copy.deepcopy(self.events)
                events[index][field] = value
                with self.subTest(index=index, field=field, value=value), \
                        self.assertRaisesRegex(ValueError, message):
                    self.check(self.write_transcript(self.root, self.receipt, events))
        events = copy.deepcopy(self.events)
        batch = events[self.batch_index()]
        batch["metrics_artifact"] = events[self.batch_index("warmup")]["metrics_artifact"]
        with self.assertRaisesRegex(ValueError, "reuses another batch"):
            self.check(self.write_transcript(self.root, self.receipt, events))
        events = copy.deepcopy(self.events)
        events[0]["metrics_artifact"] = batch["metrics_artifact"]
        with self.assertRaisesRegex(ValueError, "singleton stage invocation"):
            self.check(self.write_transcript(self.root, self.receipt, events))
        self.descriptor = self.write_transcript(self.root, self.receipt, self.events)
        self.check()
        self.db.execute("DELETE FROM batch_samples WHERE metric='compiler_batch_peak_rss'")
        with self.assertRaisesRegex(ValueError, "batch process"):
            self.check()

    def test_batch_group_contracts_are_the_derived_partition(self):
        changes = (
            (lambda plan: plan["groups"].pop(), "derived A1 partition"),
            (lambda plan: plan["groups"][1].update(members=[]), "members differ"),
            (lambda plan: plan["groups"][1]["recipe"].update(cpu="haswell"),
             "configuration or recipe"),
            (lambda plan: plan["groups"][1].update(kind=binding.SINGLETON_STAGE_GROUP),
             "derived A1 partition"),
            (lambda plan: plan["groups"][1]["controls"].append(
                dict(self.rejection_control("other/invocation-1.c"))),
             "collide on their object basename"),
            (lambda plan: plan["groups"][1]["controls"][0].update(
                row=0, fixture="tests/invocation-0.c"), "control row is timed"),
            (lambda plan: plan["groups"][0]["controls"].append(self.rejection_control()),
             "singleton stage group cannot carry"),
            (lambda plan: plan["rows"][1].update(group=0), "derived batch group"),
            (lambda plan: plan["rows"][1]["candidate"].update(
                compiler_command_sha256="e" * 64), "batch group command"),
        )
        for change, message in changes:
            self.attach()
            self.rewrite_plan(change)
            with self.subTest(message=message), self.assertRaisesRegex(ValueError, message):
                self.check()

    def test_untimed_code_rows_bind_a_reproduction_digest(self):
        self.add_untimed_rows()
        changes = (
            (lambda row: row["baseline"].update(reproduction_sha256=None),
             3, "lacks its reproduction digest"),
            (lambda row: row["candidate"].update(reproduction_sha256="e" * 64),
             3, "nondeterminism"),
            (lambda row: row["baseline"].update(reproduction_sha256="e" * 64),
             1, "only bound for untimed"),
        )
        for mutate, target, message in changes:
            self.attach()

            def change(plan, mutate=mutate, target=target):
                mutate(next(item for item in plan["rows"] if item["row"] == target))
            self.rewrite_plan(change)
            with self.subTest(message=message), self.assertRaisesRegex(ValueError, message):
                self.check()

    def test_code_records_cover_every_code_row_with_its_reproduction(self):
        self.add_untimed_rows()
        self.attach()
        _plan, _groups, rows = binding._check_execution_plan(
            self.root, self.plan, self.record, self.rows, self.rules["sampling"], 3,
            binding.NATIVE_TIMED_TARGET)
        good = self.code_records(self.root, self.plan, self.rows)
        facts = binding._check_code_records(self.root, good, self.rows, rows)
        self.assertEqual(facts, {0: (100, 100), 1: (100, 100), 3: (100, 100)})
        for mutate, message in (
                (lambda value: value["baseline"].update(reproduction_sha256=None),
                 "lacks its reproduction digest"),
                (lambda value: value["candidate"].update(reproduction_sha256="e" * 64),
                 "nondeterminism"),
                (lambda value: value["candidate"].update(code_section_bytes=99),
                 "frozen execution-plan code facts")):
            descriptor = self.code_records(self.root, self.plan, self.rows,
                                           path="execution/bad-code-records.jsonl",
                                           mutate=lambda value, mutate=mutate: (
                                               mutate(value) if value["row"] == 3 else None))
            with self.subTest(message=message), self.assertRaisesRegex(ValueError, message):
                binding._check_code_records(self.root, descriptor, self.rows, rows)
        lines = (self.root / good["path"]).read_bytes().splitlines(keepends=True)
        omitted = self.put(self.root, "execution/omitted-code-records.jsonl", b"".join(lines[:2]))
        with self.assertRaisesRegex(ValueError, "missing, truncated, or oversized"):
            binding._check_code_records(self.root, {**omitted, "records": 3}, self.rows, rows)
        with self.assertRaisesRegex(ValueError, "every code-eligible row"):
            binding._check_code_records(self.root, {**omitted, "records": 2}, self.rows, rows)

    def test_rehashed_wrong_context_or_plan_rejects(self):
        for field in ("context_sha256", "execution_plan_sha256"):
            receipt = copy.deepcopy(self.receipt)
            receipt[field] = "0" * 64
            descriptor = self.write_transcript(self.root, receipt, self.events)
            with self.subTest(field=field), self.assertRaisesRegex(ValueError, "not joined"):
                self.check(descriptor)

    def test_receipt_and_shard_metadata_are_bounded_before_reading(self):
        oversized = dict(self.descriptor, bytes=binding.EXECUTION_RECEIPT_BYTE_CAP + 1)
        with mock.patch.object(binding, "_read_trusted_json_evidence") as read:
            with self.assertRaisesRegex(ValueError, "bounded metadata"):
                self.check(oversized)
            read.assert_not_called()
        shards = self.receipt["shards"] * (binding.EXECUTION_SHARD_CAP + 1)
        with mock.patch.object(binding, "_check_evidence") as read:
            with self.assertRaisesRegex(ValueError, "shard population"):
                list(binding._execution_trace_records(self.root, shards, len(shards)))
            read.assert_not_called()

    def test_truncated_or_oversized_transcript_rejects(self):
        for data in (b"{}", b" " * (binding.EXECUTION_LINE_CAP + 1) + b"\n"):
            shard = self.put(self.root, "execution/invocations.jsonl", data)
            receipt = copy.deepcopy(self.receipt)
            receipt["shards"] = [{**shard, "records": len(self.events)}]
            descriptor = self.put(self.root, "execution/invocation-receipt.json", receipt)
            with self.subTest(size=len(data)), self.assertRaises(ValueError):
                self.check(descriptor)

    @unittest.skipIf(os.name == "nt", "POSIX native schedule oracle")
    def test_schedule_matches_native_retirement_order(self):
        compiler = shutil.which("clang") or shutil.which("cc")
        if compiler is None:
            self.skipTest("a C compiler is required for the native schedule oracle")
        # Standalone extraction of #619's schedule functions at
        # 978622bf. This oracle does not call the Python implementation.
        source = r'''#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
static uint64_t mix(uint64_t value) {
    value = (value ^ (value >> 30)) * UINT64_C(0xbf58476d1ce4e5b9);
    value = (value ^ (value >> 27)) * UINT64_C(0x94d049bb133111eb);
    return value ^ (value >> 31);
}
static uint64_t seed_for(uint64_t seed, unsigned round, unsigned block, unsigned count) {
    uint64_t value = seed ^ UINT64_C(0x6e61746976652d31);
    value ^= mix(UINT64_C(1) + UINT64_C(0x100000001b3));
    value ^= mix((uint64_t)round + UINT64_C(0x9e3779b97f4a7c15));
    value ^= mix((uint64_t)block + UINT64_C(0xd1b54a32d192ed03));
    value ^= mix((uint64_t)count + UINT64_C(0x94d049bb133111eb));
    return mix(value);
}
static uint64_t bounded(uint64_t *state, uint64_t bound) {
    uint64_t threshold = (uint64_t)(0 - bound) % bound;
    uint64_t result;
    do { *state += UINT64_C(0x9e3779b97f4a7c15); result = mix(*state); }
    while (result < threshold);
    return result % bound;
}
int main(int argc, char **argv) {
    if (argc != 2) return 1;
    uint64_t seed = (uint64_t)strtoull(argv[1], NULL, 10);
    for (unsigned round = 0; round < 2; ++round) {
        for (unsigned block = 0; block < 30; ++block) {
            uint64_t random = seed_for(seed, round, block, 2);
            unsigned first[2], order[2][2];
            for (unsigned j = 0; j < 2; ++j) {
                first[j] = (unsigned)bounded(&random, 2);
                order[0][j] = order[1][j] = j;
            }
            for (unsigned p = 0; p < 2; ++p) {
                for (unsigned remaining = 2; remaining > 1; --remaining) {
                    unsigned swap = (unsigned)bounded(&random, remaining);
                    unsigned temp = order[p][remaining - 1];
                    order[p][remaining - 1] = order[p][swap]; order[p][swap] = temp;
                }
            }
            for (unsigned p = 0; p < 2; ++p) {
                for (unsigned slot = 0; slot < 2; ++slot) {
                    unsigned job = order[p][slot];
                    for (unsigned position = 0; position < 2; ++position) {
                        unsigned variant = first[job] ^ p ^ position;
                        printf("%u %u %u %u %u\n", round, block * 2 + p, job, position, variant);
                    }
                }
            }
        }
    }
    return 0;
}
'''
        source_path = self.root / "schedule.c"
        executable = self.root / "schedule-oracle"
        source_path.write_text(source)
        subprocess.run([compiler, "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
                        str(source_path), "-o", str(executable)],
                       check=True, capture_output=True, timeout=30)
        for seed in (1, 20260913, (1 << 32) + 1, (1 << 64) - 1):
            with self.subTest(seed=seed):
                native = subprocess.run([str(executable), str(seed)], check=True,
                                        capture_output=True, text=True, timeout=10)
                expected = [tuple(map(int, line.split())) for line in native.stdout.splitlines()]
                sampling = dict(self.rules["sampling"], seed=seed)
                # (A1) The compiler campaign's cells are the two batch groups.
                actual = [(e["round"], e["pair"], e["group"], e["position"],
                           int(e["variant"] == "candidate"))
                          for e in binding._execution_schedule(self.rows, sampling)
                          if e["kind"] == "compiler" and e["phase"] == "sample"]
                self.assertEqual(actual, expected)

    def test_schedule_balances_every_two_pair_block(self):
        by_pair = {}
        for event in binding._execution_schedule(self.rows, self.rules["sampling"]):
            if event["phase"] == "sample":
                unit = event["group"] if event["kind"] == "compiler" else event["row"]
                key = event["kind"], unit, event["round"], event["pair"]
                by_pair.setdefault(key, []).append(event["variant"])
        for (kind, unit, round_id, pair), variants in by_pair.items():
            self.assertEqual(set(variants), {"baseline", "candidate"})
            if not pair & 1:
                self.assertEqual(variants[::-1], by_pair[(kind, unit, round_id, pair + 1)])
        self.assertEqual({key[:2] for key in by_pair},
                         {("compiler", 0), ("compiler", 1), ("runtime", 0)})
        for seed in (0, 1 << 64, True):
            rules = copy.deepcopy(self.rules["sampling"])
            rules["seed"] = seed
            with self.subTest(seed=seed), self.assertRaises(ValueError):
                list(binding._execution_schedule(self.rows, rules))


if __name__ == "__main__":
    unittest.main()
