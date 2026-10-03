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
                       binding.NEXT_SUPPORT_DECLARATION_SHA256,
                       binding.APPLE_CI_SUPPORT_DECLARATION_SHA256,
                       binding.PROPOSED_SUPPORT_DECLARATION_SHA256,
                       binding.MAIN_CI_REUSE_SUPPORT_DECLARATION_SHA256,
                       binding.BOOTSTRAP_WORKFLOW_SUPPORT_DECLARATION_SHA256,
                       binding.RETIRED_BRIDGE_SUPPORT_DECLARATION_SHA256,
                       binding.ALIGNED_TYPEDEF_SUPPORT_DECLARATION_SHA256))

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

    def test_native_population_rejects_pairs_above_the_collection_maximum(self):
        # (A1) The smaller native population fits the record ceiling even at
        # 256 pairs, so the 254-pair maximum must be enforced by name.
        sample_rows, groups = self.native_population_bounds()
        self.assertEqual(binding.SAMPLING_MAX_PAIRS, 254)
        self.assertLessEqual((sample_rows + groups) * 2 * 256, binding.RESULT_INPUT_MAX_TOTAL_RECORDS)
        for pairs in (256, 258):
            with tempfile.TemporaryDirectory() as directory, self.subTest(pairs=pairs), \
                    self.assertRaisesRegex(ValueError, "254-pair collection maximum"):
                self.check_plan(Path(directory), pairs, sample_rows, groups)

    def test_current_population_over_cap_cannot_be_rescued_by_more_shards(self):
        # The immutable ceiling is kept: the full census at the 254-pair
        # collection maximum, even ignoring the native projection, is rejected
        # before any sample (256 pairs is now refused by the pair maximum).
        sample_rows = self.object_rows + len(binding.STAGES) - 1
        required = sample_rows * 2 * binding.SAMPLING_MAX_PAIRS
        self.assertGreater(required, binding.RESULT_INPUT_MAX_TOTAL_RECORDS)
        with tempfile.TemporaryDirectory() as directory:
            with self.assertRaisesRegex(ValueError, "immutable total-record ceiling"):
                self.check_plan(Path(directory), binding.SAMPLING_MAX_PAIRS, sample_rows, 1)


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
    # (L7) The reviewed budget these fixtures bind: every object group's
    # metrics bound is header + inputs * per-input from its record.
    METRICS_HEADER_BYTES = 4096
    METRICS_INPUT_BYTES = 16384

    @classmethod
    def campaign_budget(cls, header=None, per_input=None, edit=None):
        """A canonical campaign budget record (retirement_budget.h layout)."""
        values = dict.fromkeys(binding.CAMPAIGN_BUDGET_SCALARS, 1000000000)
        values["reviewed-ns"] = 36000000000000
        values["metrics-header-bytes"] = cls.METRICS_HEADER_BYTES if header is None else header
        values["metrics-input-bytes"] = cls.METRICS_INPUT_BYTES if per_input is None else per_input
        values["aa-attestation-ns-per-mib"] = 8000000
        lines = [f"schema={binding.CAMPAIGN_BUDGET_SCHEMA}",
                 f"derivation={binding.CAMPAIGN_BUDGET_DERIVATION}"]
        lines += [f"{key}={values[key]}" for key in binding.CAMPAIGN_BUDGET_SCALARS]
        for prefix in ("", "untimed-"):
            lines += [f"{prefix}batch=4:100000000", f"{prefix}batch=1024:2000000000"]
            lines += [f"{prefix}singleton={stage}:900000000" for stage in binding.CAMPAIGN_BUDGET_STAGES]
        record = "\n".join(lines) + "\n"
        if edit is not None:
            record = edit(record)
        return {"record": record, "sha256": hashlib.sha256(record.encode("ascii")).hexdigest()}

    class MetricsShards:
        """Pack per-batch metrics artifacts into ``retirement-metrics-<tag>-NNNN.txt``
        shards, ``per_shard`` artifacts each, as the producer's shard writer does."""

        def __init__(self, root, tag, per_shard=100):
            self.root, self.tag, self.per_shard = root, tag, per_shard
            self.index, self.count, self.size = -1, per_shard, 0
            for stale in root.glob(f"retirement-metrics-{tag}-*.txt"):
                stale.unlink()

        def append(self, data):
            if self.count == self.per_shard:
                self.index, self.count, self.size = self.index + 1, 0, 0
            path = f"retirement-metrics-{self.tag}-{self.index:04d}.txt"
            with (self.root / path).open("ab") as stream:
                stream.write(data)
            artifact = {"bytes": len(data), "offset": self.size, "path": path,
                        "sha256": hashlib.sha256(data).hexdigest()}
            self.size += len(data)
            self.count += 1
            return artifact

    @staticmethod
    def metrics_slice(root, artifact):
        with (root / artifact["path"]).open("rb") as stream:
            stream.seek(artifact["offset"])
            return stream.read(artifact["bytes"])

    @staticmethod
    def put_metrics(root, path, data):
        """A one-artifact metrics shard at ``path``."""
        (root / path).write_bytes(data)
        return {"bytes": len(data), "offset": 0, "path": path,
                "sha256": hashlib.sha256(data).hexdigest()}

    @staticmethod
    def digest(text):
        return hashlib.sha256(text.encode()).hexdigest()

    @classmethod
    def rejection_control(cls, fixture="tests/rejection-control.c"):
        """A frozen, status-checked (never timed) rejection control input."""
        return {"fixture": fixture, "row": None, "status": "rejected",
                "error": "driver.analysis",
                "diagnostic_sha256": cls.digest(f"diagnostic/{fixture}"),
                "object_sha256": None}

    @classmethod
    def group_contract(cls, group, controls, command_prefix, fixtures=None):
        object_group = group["kind"] == binding.OBJECT_BATCH_GROUP
        listing = None
        if object_group:
            names = list(fixtures or []) + [item["fixture"] for item in controls]
            listing = hashlib.sha256(binding._input_list_bytes(names, "fixture")).hexdigest()
        exit_status = 1 if any(item["status"] != "ok" for item in controls) else 0
        sides = {}
        for side in ("baseline", "candidate"):
            command = (cls.digest(f"{command_prefix}/{group['group']}/{side}") if object_group
                       else cls.digest(f"compile/{group['rows'][0]}/{side}"))
            sides[side] = {"command_sha256": command, "exit_status": exit_status}
        return {
            "group": group["group"], "kind": group["kind"],
            "target": group["identity"]["target"],
            "configuration": {field: group["identity"][field]
                              for field in ("allocator", "frontend_lowering", "PIC")},
            "recipe": {field: group["identity"][field]
                       for field in ("fixture_recipe", "cpu", "cpu_features")},
            "members": [{"row": row,
                         "diagnostic_sha256": cls.EMPTY_SHA256 if object_group else None}
                        for row in group["rows"]],
            "controls": controls, "input_list_sha256": listing,
            "metrics_bytes_max": (cls.METRICS_HEADER_BYTES + cls.METRICS_INPUT_BYTES * len(names)
                                  if object_group else None), **sides}

    @classmethod
    def execution_plan(cls, root, record, parsed, controls=None):
        """Build a v3 plan: row contracts plus derived timed and untimed groups."""
        sampling = record["rules"]["sampling"]
        oracle_path = record["workflow"]["records"]["oracle"]["path"]
        oracle_by_row = {item["row"]: item for item in
                         json.loads((root / oracle_path).read_text())["records"]}
        controls = controls or {}
        groups = binding._batch_groups(parsed)
        group_of_row = {row: group["group"] for group in groups for row in group["rows"]}
        fixture_of = {row["row"]: row["identity"]["fixture"] for row in parsed}
        group_contracts = [
            cls.group_contract(group, copy.deepcopy(controls.get(group["group"], []))
                               if group["kind"] == binding.OBJECT_BATCH_GROUP else [], "batch",
                               [fixture_of[row] for row in group["rows"]])
            for group in groups]
        untimed_contracts = [cls.group_contract(group, [], "untimed",
                                                [fixture_of[row] for row in group["rows"]])
                             for group in binding._untimed_groups(parsed)]
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
                "campaign_budget": cls.campaign_budget(),
                "rows": contracts, "groups": group_contracts,
                "untimed_groups": untimed_contracts}
        for key in ("seed", "rounds", "pairs_per_round", "warmups_per_variant"):
            plan[key] = sampling[key]
        return plan

    @staticmethod
    def cc_metrics(contract, fixtures, wall_ns, intervals, arenas, rss, exit_status):
        """Compiler ``-fmetrics-out`` records for one batch (members, then controls).

        Returned as editable ``{"header", "inputs", "functions"}`` so a test
        can corrupt one field before ``cc_metrics_bytes`` serializes it.
        """
        inputs = [(member["row"], member["diagnostic_sha256"], "ok", "driver.none", True)
                  for member in contract["members"]]
        inputs.extend((control["row"], control["diagnostic_sha256"], control["status"],
                       control["error"], False) for control in contract["controls"])
        records, functions = [], {}
        cursor = 1
        for index, (row, diagnostic, status, error, member) in enumerate(inputs):
            interval = intervals[row] if member else 10
            ok = status == "ok"
            record = {field: 0 for field in binding.CC_METRICS_INPUT_FIELDS}
            record.update({
                "version": 1, "index": index, "status": status, "error": error,
                "errors": 0 if ok else 1, "measured": 1, "start_ns": cursor,
                "end_ns": cursor + interval, "total_ns": interval,
                "parse_ns": interval // 4, "codegen_ns": interval // 2,
                "arena_peak_bytes": arenas[row] if member else 4096,
                "source_bytes": 100, "object_file_bytes": 1000 if ok else 0,
                "text_bytes": 100 if ok else 0, "code_bytes": 100 if ok else 0,
                "function_records": 1 if ok else 0,
                "diagnostic_records": 0 if ok else 1, "diagnostic_digest": diagnostic,
                "path_hex": fixtures[index].encode(), "diagnostic_code_hex": b"" if ok else error.encode(),
                "diagnostic_path_hex": b"" if ok else fixtures[index].encode(),
                "message_bytes": 0 if ok else 5, "message_hex": b"" if ok else b"error",
            })
            records.append(record)
            if ok:
                functions[index] = [{"version": 1, "input": index, "ordinal": 0,
                                     "code_bytes": 100, "name_bytes": 4, "name_truncated": 0,
                                     "name_hex": b"main"}]
            cursor += interval
        statuses = [record["status"] for record in records]
        header = {
            "version": 1, "schema": "buster-cc-metrics", "inputs": len(records),
            "records": len(records),
            **{status: statuses.count(status) for status in binding.CC_INPUT_STATUSES},
            "error": next((record["error"] for record in records if record["status"] != "ok"),
                          "driver.none"),
            "exit_status": exit_status, "action": "object",
            "target": binding.TARGET_METRICS_NAMES[contract["target"]],
            "allocator": contract["configuration"]["allocator"], "compile_jobs": 1,
            "compilation_workers": 1, "intervals": "serial", "keep_going": 1,
            "function_sizes": 1, "wall_ns": wall_ns, "peak_rss_bytes": rss,
        }
        assert cursor <= wall_ns, "fixture intervals must fit inside the compiler window"
        return {"header": header, "inputs": records, "functions": functions}

    @staticmethod
    def cc_metrics_bytes(metrics):
        def value(item):
            if isinstance(item, bytes):
                return item.hex() if item else "-"
            return str(item)

        def line(tag, fields, record):
            return tag + "".join(f" {field}={value(record[field])}" for field in fields) + "\n"

        text = [line("CC_METRICS", binding.CC_METRICS_HEADER_FIELDS, metrics["header"])]
        for index, record in enumerate(metrics["inputs"]):
            text.append(line("CC_METRICS_INPUT", binding.CC_METRICS_INPUT_FIELDS, record))
            for function in metrics["functions"].get(index, []):
                text.append(line("CC_METRICS_FUNCTION", binding.CC_METRICS_FUNCTION_FIELDS,
                                 function))
        return "".join(text).encode()

    @classmethod
    def attach_execution(cls, root, record, parsed, samples, batch_samples=None,
                         controls=None, mutate_metrics=None, mutate_objects=None):
        """Attach a complete test-only v3 plan and batch transcript to a fixture.

        ``samples`` maps (row, round, pair) to metric -> {baseline, candidate};
        ``batch_samples`` maps (group, round, pair) the same way for the batch
        process pair.  Warmups reuse coordinate (unit, 0, 0).  Frozen oracle
        outputs are test values, never deployment evidence.
        ``mutate_metrics(sequence, metrics)`` may corrupt one metrics artifact
        and ``mutate_objects(sequence, digests)`` one batch's written objects.
        """
        sampling = record["rules"]["sampling"]
        plan = cls.execution_plan(root, record, parsed, controls)
        plan_descriptor = cls.put(root, "execution/invocation-plan.json", plan)
        plan_rows = {item["row"]: item for item in plan["rows"]}
        group_contracts = {item["group"]: item for item in plan["groups"]}
        row_by_id = {row["row"]: row for row in parsed}
        groups = {group["group"]: group for group in binding._batch_groups(parsed)}
        events = []
        shards = cls.MetricsShards(root, "ab")
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
                    # The header RSS is diagnostic; varying it per batch keeps
                    # every metrics artifact's content distinct.
                    metrics = cls.cc_metrics(contract, fixtures,
                                             int(round(seconds * 1_000_000_000)) - 1,
                                             intervals, arenas, rss + event["sequence"],
                                             contract[variant]["exit_status"])
                    objects = ([plan_rows[row][variant]["artifact_sha256"]
                                for row in group["rows"]]
                               + [item["object_sha256"] for item in contract["controls"]])
                    if mutate_metrics is not None:
                        mutate_metrics(event["sequence"], metrics)
                    if mutate_objects is not None:
                        mutate_objects(event["sequence"], objects)
                    output = binding._batch_output_digest(objects)
                    metrics_artifact = shards.append(cls.cc_metrics_bytes(metrics))
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
    def untimed_batches(cls, root, plan_descriptor, parsed, purposes=("reproduction",),
                        mutate=None, path="execution/untimed-batches.jsonl", *, record, receipt):
        """Write the untimed code-artifact batch records and their metrics.

        Each batch is its own supervisor-bound process of the variant's
        subject binary, serially after the timed collection window.
        """
        plan = json.loads((root / plan_descriptor["path"]).read_text())
        rows = {item["row"]: item for item in plan["rows"]}
        row_by_id = {row["row"]: row for row in parsed}
        lines = []
        shards = cls.MetricsShards(root, "untimed", per_shard=3)
        for contract in plan["untimed_groups"]:
            object_group = contract["kind"] == binding.OBJECT_BATCH_GROUP
            members = [member["row"] for member in contract["members"]]
            for variant in ("baseline", "candidate"):
                for purpose in purposes:
                    field = "artifact_sha256" if purpose == "production" else "reproduction_sha256"
                    objects = [rows[row][variant][field] for row in members]
                    metrics_artifact = None
                    index = len(lines)
                    if object_group:
                        # The header RSS differs per batch, so no two metrics
                        # artifacts share content.
                        metrics = cls.cc_metrics(
                            contract, [row_by_id[row]["identity"]["fixture"] for row in members],
                            5_000_000, {row: 1000 for row in members},
                            {row: 8192 for row in members}, 65536 + index,
                            contract[variant]["exit_status"])
                        value = {"metrics": metrics, "objects": objects}
                        if mutate is not None:
                            mutate(contract["group"], variant, purpose, value)
                        metrics_artifact = shards.append(cls.cc_metrics_bytes(value["metrics"]))
                        objects = value["objects"]
                    pid = 900000 + index
                    token = f"untimed-process-{index}"
                    started = receipt["completed_at_ns"] + 10_000_000 * (index + 1)
                    lines.append({"command_sha256": contract[variant]["command_sha256"],
                                  "executable_sha256": record["subjects"][variant]["binary"]["sha256"],
                                  "exit_status": contract[variant]["exit_status"],
                                  "finished_ns": started + 5_000_001,
                                  "group": contract["group"],
                                  "metrics_artifact": metrics_artifact,
                                  "output_sha256": binding._batch_output_digest(objects),
                                  "pid": pid,
                                  "process_instance_sha256": binding._process_instance_digest(
                                      receipt["job_id"], receipt["attempt"], receipt["boot_id"],
                                      pid, token),
                                  "process_start_token": token,
                                  "purpose": purpose, "started_ns": started, "variant": variant})
        if not lines:
            return None
        data = b"".join((json.dumps(item, sort_keys=True, separators=(",", ":")) + "\n").encode()
                        for item in lines)
        return {**cls.put(root, path, data), "records": len(lines)}

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

    def test_execution_plan_rejects_pairs_above_the_collection_maximum(self):
        for pairs in (256, 255):
            def widen(plan, pairs=pairs):
                plan["pairs_per_round"] = pairs
            self.rewrite_plan(widen)
            with self.subTest(pairs=pairs), \
                    self.assertRaisesRegex(ValueError, "254-pair collection maximum|even"):
                self.check()

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
                  "code_records": code_records, "untimed_batches": None,
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

    def assert_metrics_rejected(self, mutate, message, sequences=None, hook="mutate_metrics"):
        for target in sequences or (self.batch_sequences()[0], self.batch_sequences()[-1]):
            def corrupt(sequence, value, target=target):
                if sequence == target:
                    mutate(value)
            self.attach(**{hook: corrupt})
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
        # Determinism: every warmup and sample batch must write each fixture's
        # object byte-identical to its frozen artifact; the producer hashes the
        # written objects in input order into the invocation's output digest.
        def mismatch(objects):
            objects[0] = "f" * 64
        self.assert_metrics_rejected(mismatch, "nondeterminism", hook="mutate_objects")

        def extra_object(objects):
            objects[1] = "f" * 64
        self.assert_metrics_rejected(extra_object, "nondeterminism", hook="mutate_objects")

    def test_per_input_intervals_are_ordered_and_inside_the_batch(self):
        def overlap(metrics):
            first, second = metrics["inputs"][:2]
            second["start_ns"] = first["end_ns"] - 1
            second["end_ns"] = second["start_ns"] + 10
            second["total_ns"] = 10

        def late(metrics):
            record = metrics["inputs"][-1]
            record["end_ns"] = metrics["header"]["wall_ns"] + 1
            record["total_ns"] = record["end_ns"] - record["start_ns"]

        def empty(metrics):
            record = metrics["inputs"][0]
            record["end_ns"] = record["start_ns"]
            record["total_ns"] = 0

        def outside_process(metrics):
            metrics["header"]["wall_ns"] += 10 ** 9
        for mutate, message in ((overlap, "per-input intervals"), (late, "per-input intervals"),
                                (empty, "per-input intervals"),
                                (outside_process, "header is not one serial")):
            self.assert_metrics_rejected(mutate, message)

        def phases(metrics):
            record = metrics["inputs"][0]
            record["codegen_ns"] = record["total_ns"]
        self.assert_metrics_rejected(phases, "phase timings exceed")

        def total(metrics):
            metrics["inputs"][0]["total_ns"] += 1
        self.assert_metrics_rejected(total, "total_ns is not its per-input interval")

    def test_control_status_and_diagnostics_match_the_frozen_oracle(self):
        for field, value in (("status", "failed"), ("error", "driver.parse"),
                             ("diagnostic_digest", "e" * 64)):
            def change(metrics, field=field, value=value):
                metrics["inputs"][1][field] = value
            self.assert_metrics_rejected(change, "status, error or diagnostics|header is not")

        def member_diagnostic(metrics):
            metrics["inputs"][0]["diagnostic_digest"] = "e" * 64
        self.assert_metrics_rejected(member_diagnostic, "status, error or diagnostics")

        def unmeasured(metrics):
            metrics["inputs"][0]["measured"] = 0
        self.assert_metrics_rejected(unmeasured, "status, error or diagnostics")

        def reorder(metrics):
            metrics["inputs"][0]["path_hex"], metrics["inputs"][1]["path_hex"] = \
                metrics["inputs"][1]["path_hex"], metrics["inputs"][0]["path_hex"]
        self.assert_metrics_rejected(reorder, "frozen batch input order")

        def object_written(metrics):
            metrics["inputs"][1]["object_file_bytes"] = 10
        self.assert_metrics_rejected(object_written, "object output contradicts")

        def inconsistent_control(plan):
            plan["groups"][1]["controls"][0]["status"] = "ok"
        self.rewrite_plan(inconsistent_control)
        with self.assertRaisesRegex(ValueError, "status, error and object disagree"):
            self.check()
        self.attach()

        def clean_exit(plan):
            for side in ("baseline", "candidate"):
                plan["groups"][1][side]["exit_status"] = 0
        self.rewrite_plan(clean_exit)
        with self.assertRaisesRegex(ValueError, "exit status contradicts"):
            self.check()

    def test_metrics_header_is_one_serial_continue_on_failure_object_batch(self):
        for field, value in (("compile_jobs", 2), ("compilation_workers", 2),
                             ("intervals", "concurrent"), ("keep_going", 0),
                             ("action", "link"), ("target", "aarch64-linux"),
                             ("allocator", "quality"), ("exit_status", 0),
                             ("error", "driver.none"), ("inputs", 3), ("records", 3),
                             ("rejected", 0), ("not_run", 1), ("schema", "other-metrics"),
                             ("function_sizes", 2)):
            def change(metrics, field=field, value=value):
                metrics["header"][field] = value
            self.assert_metrics_rejected(change, "header is not one serial",
                                         sequences=(self.batch_sequences()[-1],))

    def test_metrics_artifacts_are_strict_bounded_and_exactly_the_frozen_inputs(self):
        _plan, groups, rows, _untimed = binding._check_execution_plan(
            self.root, self.plan, self.record, self.rows, self.rules["sampling"], 3,
            binding.NATIVE_TIMED_TARGET)
        event = self.events[self.batch_index()]
        frozen = binding._frozen_batch_inputs(groups[1], rows, {row["row"]: row for row in self.rows},
                                              event["variant"], "artifact_sha256")
        oversized = dict(event["metrics_artifact"], bytes=binding.METRICS_ARTIFACT_BYTE_CAP + 1)
        outside = dict(event["metrics_artifact"], offset=binding.METRICS_SHARD_BYTE_CAP)
        for descriptor in (oversized, outside):
            with mock.patch.object(binding.Path, "open") as read:
                with self.assertRaisesRegex(ValueError, "exceeds its bounded size"):
                    binding._check_batch_metrics(self.root, descriptor, groups[1], frozen, 1,
                                                 binding.NATIVE_TIMED_TARGET, None, "metrics")
                read.assert_not_called()
        # The group's reviewed bound is checked against every artifact.
        bounded = dict(groups[1], metrics_bytes_max=event["metrics_artifact"]["bytes"] - 1)
        with self.assertRaisesRegex(ValueError, "reviewed metrics bound"):
            binding._check_batch_metrics(self.root, event["metrics_artifact"], bounded, frozen, 1,
                                         binding.NATIVE_TIMED_TARGET, None, "metrics")
        # A range past the shard's end, or with bytes beyond it, is not the artifact.
        past = dict(event["metrics_artifact"], offset=event["metrics_artifact"]["offset"] + 10 ** 6)
        with self.assertRaisesRegex(ValueError, "outside its metrics shard"):
            binding._check_batch_metrics(self.root, past, groups[1], frozen, 1,
                                         binding.NATIVE_TIMED_TARGET, None, "metrics")
        for field, value in (("path", "execution/metrics/batch.txt"),
                             ("path", "retirement-metrics-AB-0000.txt"), ("offset", -1)):
            with self.subTest(field=field, value=value), self.assertRaises(ValueError):
                binding._metrics_artifact(dict(event["metrics_artifact"], **{field: value}),
                                          "metrics")

        def huge_record(metrics):
            metrics["inputs"][1]["message_hex"] = b"e" * binding.METRICS_RECORD_BYTE_CAP
        self.assert_metrics_rejected(huge_record, "missing, truncated, or oversized")

        def extra(metrics):
            metrics["inputs"].append(dict(metrics["inputs"][-1], index=len(metrics["inputs"])))
        self.assert_metrics_rejected(extra, "undeclared extra records")

        def missing(metrics):
            metrics["inputs"].pop()
        self.assert_metrics_rejected(missing, "missing, truncated, or oversized")

        def version(metrics):
            metrics["header"]["version"] = 2
        self.assert_metrics_rejected(version, "not metrics version 1")

        def leading_zero(metrics):
            metrics["inputs"][0]["errors"] = "00"
        self.assert_metrics_rejected(leading_zero, "canonical unsigned decimal")

        def upper_hex(metrics):
            metrics["inputs"][0]["path_hex"] = "ABCD"
        self.assert_metrics_rejected(upper_hex, "lowercase hex")

        # The input path is exactly the frozen fixture path: no prefix or
        # suffix match, no `..` component, printable bytes only.
        for change in (lambda path: b"/checkout/" + path, lambda path: b"src/../" + path,
                       lambda path: path + b"\n", lambda path: path.replace(b"/", b"\x01", 1),
                       lambda path: path.split(b"/", 1)[1], lambda path: b""):
            def rewrite(metrics, change=change):
                metrics["inputs"][0]["path_hex"] = change(metrics["inputs"][0]["path_hex"])
            self.assert_metrics_rejected(rewrite, "frozen batch input order")

        def stray_function(metrics):
            metrics["functions"][0][0]["input"] = 1
        self.assert_metrics_rejected(stray_function, "function records do not follow")

        def unrequested_functions(metrics):
            metrics["header"]["function_sizes"] = 0
        self.assert_metrics_rejected(unrequested_functions, "did not request")

        # Messages and names are cut at 1,024 bytes; the full length and the
        # flag must agree with the recorded text.
        limit = binding.CC_METRICS_TEXT_LIMIT
        for field, value in (("message_truncated", 1), ("message_truncated", 2),
                             ("message_bytes", 6)):
            def message(metrics, field=field, value=value):
                metrics["inputs"][1][field] = value
            self.assert_metrics_rejected(message, "truncation contradicts")
        for update in ({"name_bytes": limit + 1, "name_hex": b"n" * limit},
                       {"name_bytes": limit, "name_truncated": 1, "name_hex": b"n" * limit},
                       {"name_bytes": limit + 1, "name_truncated": 1, "name_hex": b"n" * (limit + 1)}):
            def name(metrics, update=update):
                metrics["functions"][0][0].update(update)
            self.assert_metrics_rejected(name, "truncation contradicts")

        def cut_name(sequence, metrics):
            metrics["functions"][0][0].update(name_bytes=limit + 5, name_truncated=1,
                                               name_hex=b"n" * limit)
        self.attach(mutate_metrics=cut_name)
        self.check()

    def test_metrics_artifact_parser_rejects_reordered_or_unknown_keys(self):
        event = self.events[self.batch_index()]
        data = self.metrics_slice(self.root, event["metrics_artifact"])
        lines = data.splitlines(keepends=True)
        swapped = lines[1].replace(b" errors=", b" warnings_tmp=").replace(
            b" warnings=", b" errors=").replace(b" warnings_tmp=", b" warnings=")
        for index, replacement in ((1, swapped),
                                   (1, lines[1].replace(b"\n", b" extra=1\n")),
                                   (0, lines[0].replace(b"CC_METRICS ", b"CC_METRICS  ", 1)),
                                   (1, lines[1].replace(b"CC_METRICS_INPUT", b"CC_METRICS_ROW"))):
            changed = b"".join(lines[:index] + [replacement] + lines[index + 1:])
            descriptor = self.put_metrics(self.root, "retirement-metrics-tamper-0000.txt", changed)
            with self.subTest(index=index), self.assertRaisesRegex(
                    ValueError, "pinned field order|missing, reordered, or unknown"):
                binding._read_cc_metrics(self.root, descriptor, 2, "tampered metrics")

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
        with self.assertRaisesRegex(ValueError, "packed contiguously"):
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

    def test_batch_response_file_and_metrics_bound_are_frozen(self):
        # (A1 Q10, M4) Each object group binds the digest of its canonical
        # `@file` input list over the frozen member order plus controls, and
        # the reviewed metrics bound; a singleton group has neither.
        listing = binding._input_list_bytes(["tests/invocation-1.c", "tests/rejection-control.c"],
                                            "list")
        self.assertEqual(listing, b'"tests/invocation-1.c"\n"tests/rejection-control.c"\n')
        plan = json.loads((self.root / self.plan["path"]).read_text())
        self.assertEqual(plan["groups"][1]["input_list_sha256"], hashlib.sha256(listing).hexdigest())
        self.assertIsNone(plan["groups"][0]["input_list_sha256"])
        # Quotes and backslashes round-trip through the driver's grammar.
        quoted = binding._input_list_bytes(['tests/a "b" c.c'], "list")
        self.assertEqual(binding._response_file_arguments(quoted, "list"), [b'tests/a "b" c.c'])
        self.assertEqual(binding._response_file_arguments(b"a 'b c'\td\\ e \"\"", "list"),
                         [b"a", b"b c", b"d e", b""])
        for data, message in ((b'"open', "quote open"), (b"a\\", "backslash"), (b"a\0b", "NUL"),
                              (b"@nested", "nests"), (b'"@quoted"', "nests")):
            with self.subTest(data=data), self.assertRaisesRegex(ValueError, message):
                binding._response_file_arguments(data, "list")
        for fixture in ("@tests/a.c", "-tests/a.c", "tests/../a.c", "tests/a\tb.c"):
            with self.subTest(fixture=fixture), self.assertRaises(ValueError):
                binding._input_list_bytes([fixture], "list")
        changes = (
            (lambda plan: plan["groups"][1].update(input_list_sha256="e" * 64),
             "canonical response file"),
            (lambda plan: plan["groups"][1]["controls"][0].update(
                fixture="tests/other-control.c"), "canonical response file"),
            (lambda plan: plan["groups"][1].update(metrics_bytes_max=0), "reviewed per-artifact bound"),
            (lambda plan: plan["groups"][1].update(
                metrics_bytes_max=binding.METRICS_ARTIFACT_BYTE_CAP + 1), "reviewed per-artifact bound"),
            (lambda plan: plan["groups"][0].update(metrics_bytes_max=1), "no response file"),
            # (L7) The bound is exactly the plan's budget record's, and that
            # record is canonical and digest-bound.
            (lambda plan: plan["groups"][1].update(
                metrics_bytes_max=plan["groups"][1]["metrics_bytes_max"] - 1), "reviewed per-artifact bound"),
            (lambda plan: plan.update(campaign_budget=self.campaign_budget(per_input=8192)),
             "reviewed per-artifact bound"),
            (lambda plan: plan.update(campaign_budget=dict(plan["campaign_budget"], sha256="e" * 64)),
             "digest of its record"),
            (lambda plan: plan.update(campaign_budget=self.campaign_budget(
                edit=lambda record: record.replace("singleton=link:900000000\n", "", 1))),
             "singleton bound"),
            (lambda plan: plan.update(campaign_budget=self.campaign_budget(
                edit=lambda record: record.replace("batch=4:", "batch=04:", 1))), "canonical uint64"),
            (lambda plan: plan.update(campaign_budget=self.campaign_budget(
                edit=lambda record: record.replace("-v3", "-v2", 1))), "schema and derivation"),
            (lambda plan: plan.pop("campaign_budget"), "missing fields"),
            (lambda plan: plan["groups"][0].update(input_list_sha256="e" * 64), "no response file"),
            (lambda plan: plan["groups"][1].pop("input_list_sha256"), "missing fields"),
        )
        for change, message in changes:
            self.attach()
            self.rewrite_plan(change)
            with self.subTest(message=message), self.assertRaisesRegex(ValueError, message):
                self.check()

    def test_metrics_artifacts_tile_their_shards_in_record_order(self):
        # (M4) Per-batch artifacts are individually addressed shard ranges:
        # each shard starts at offset 0, each artifact follows the previous
        # one, and a shard left behind is never revisited.
        self.attach()
        batches = [index for index, event in enumerate(self.events)
                   if event["metrics_artifact"] is not None]
        paths = {self.events[index]["metrics_artifact"]["path"] for index in batches}
        self.assertEqual(len(paths), 3)  # 244 batches at 100 artifacts per shard.
        self.assertEqual(self.check()["metrics_shards"].order, sorted(paths))
        first, second = self.events[batches[0]], self.events[batches[1]]
        for change, message in (
                (lambda events: events[batches[1]]["metrics_artifact"].update(
                    offset=second["metrics_artifact"]["offset"] + 1), "packed contiguously"),
                (lambda events: events[batches[0]]["metrics_artifact"].update(offset=1),
                 "fresh metrics shard"),
                (lambda events: events[batches[150]].update(
                    metrics_artifact=dict(first["metrics_artifact"])), "packed contiguously"),
                (lambda events: events[batches[1]]["metrics_artifact"].update(
                    sha256="e" * 64), "authenticated digest"),
                (lambda events: events[batches[1]]["metrics_artifact"].update(
                    bytes=second["metrics_artifact"]["bytes"] - 1),
                 "missing, truncated, or oversized|extra records")):
            events = copy.deepcopy(self.events)
            change(events)
            with self.subTest(message=message), self.assertRaisesRegex(ValueError, message):
                self.check(self.write_transcript(self.root, self.receipt, events))
        # A sealed shard must hold exactly its artifacts: trailing bytes reject.
        tracker = binding._MetricsShards(self.root)
        for index in batches:
            tracker.stream(self.events[index]["metrics_artifact"], "artifact")
        shards = tracker.finish()
        self.assertEqual([item["path"] for item in shards], sorted(paths))
        self.assertEqual(sum(item["bytes"] for item in shards),
                         sum(self.events[index]["metrics_artifact"]["bytes"] for index in batches))
        with (self.root / shards[-1]["path"]).open("ab") as stream:
            stream.write(b"hidden\n")
        with self.assertRaisesRegex(ValueError, "outside its authenticated artifacts"):
            tracker.finish()
        # The untimed batches may not continue or reuse a timed shard, nor
        # reuse the timed writer's tag.
        forked = tracker.fork()
        with self.assertRaisesRegex(ValueError, "fresh metrics shard"):
            forked.add(first["metrics_artifact"], "artifact")
        (self.root / "retirement-metrics-ab-0099.txt").write_bytes(b"x")
        with self.assertRaisesRegex(ValueError, "another writer's metrics shard tag"):
            tracker.fork().add({"path": "retirement-metrics-ab-0099.txt", "offset": 0, "bytes": 1,
                                "sha256": hashlib.sha256(b"x").hexdigest()}, "artifact")
        # (L6) A registered range must be streamed in full before finish.
        unfed = binding._MetricsShards(self.root)
        unfed.add(first["metrics_artifact"], "artifact")
        with self.assertRaisesRegex(ValueError, "not all streamed"):
            unfed.finish()
        # (L5) One tag per writer, shard index equal to its position, and at
        # most 2048 shards per writer.
        def artifact(path):
            return {"path": path, "offset": 0, "bytes": 1, "sha256": "e" * 64}
        for paths, message in (
                (["retirement-metrics-ab-0000.txt", "retirement-metrics-aa-0001.txt"], "shard sequence"),
                (["retirement-metrics-ab-0001.txt"], "shard sequence"),
                (["retirement-metrics-ab-0000.txt", "retirement-metrics-ab-0002.txt"], "shard sequence")):
            sequence = binding._MetricsShards(self.root)
            with self.subTest(paths=paths), self.assertRaisesRegex(ValueError, message):
                for path in paths:
                    sequence.add(artifact(path), "artifact")
        capped = binding._MetricsShards(self.root)
        for index in range(binding.METRICS_SHARD_CAP):
            capped.add(artifact(f"retirement-metrics-ab-{index:04d}.txt"), "artifact")
        with self.assertRaisesRegex(ValueError, "one writer's shard bound"):
            capped.add(artifact(f"retirement-metrics-ab-{binding.METRICS_SHARD_CAP:04d}.txt"), "artifact")
        self.assertEqual(binding.METRICS_SHARD_CAP, 2048)

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
        _plan, _groups, rows, _untimed = binding._check_execution_plan(
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

    def untimed_contracts(self):
        _plan, _groups, rows, untimed = binding._check_execution_plan(
            self.root, self.plan, self.record, self.rows, self.rules["sampling"], 3,
            binding.NATIVE_TIMED_TARGET)
        return rows, untimed

    def untimed(self, purposes, mutate=None):
        return self.untimed_batches(self.root, self.plan, self.rows, purposes, mutate,
                                    record=self.record, receipt=self.receipt)

    def check_untimed(self, descriptor, rows, untimed, execution=None):
        return binding._check_untimed_batches(self.root, descriptor, self.rows, rows, untimed,
                                              self.record, execution or self.check())

    def test_untimed_code_artifact_batches_are_sealed_and_checked(self):
        # (A1) Untimed code rows need their frozen oracle status/diagnostics and
        # a byte-identical reproduction, from batches outside the timed count.
        self.add_untimed_rows()
        self.attach()
        self.assertEqual([group["rows"] for group in binding._untimed_groups(self.rows)], [[3]])
        rows, untimed = self.untimed_contracts()
        self.assertEqual(self.check()["invocations"], (2 + 1) * 2 * (2 + 2 * 60))
        for purposes in (("reproduction",), ("production", "reproduction")):
            descriptor = self.untimed(purposes)
            records = self.check_untimed(descriptor, rows, untimed)
            self.assertEqual([(record["variant"], record["purpose"]) for record in records],
                             [(variant, purpose) for variant in ("baseline", "candidate")
                              for purpose in purposes])
        with self.assertRaisesRegex(ValueError, "lacks its untimed code-artifact batch records"):
            self.check_untimed(None, rows, untimed)
        with self.assertRaisesRegex(ValueError, "lacks its reproduction batch"):
            self.check_untimed(self.untimed(("production",)), rows, untimed)

        def status(group, variant, purpose, value):
            value["metrics"]["inputs"][0]["diagnostic_digest"] = "e" * 64

        def rejected(group, variant, purpose, value):
            value["metrics"]["inputs"][0].update(status="rejected", error="driver.parse")

        def object_mismatch(group, variant, purpose, value):
            value["objects"][0] = "f" * 64

        def interval(group, variant, purpose, value):
            record = value["metrics"]["inputs"][0]
            record["end_ns"] = value["metrics"]["header"]["wall_ns"] + 1
            record["total_ns"] = record["end_ns"] - record["start_ns"]
        for mutate, message in ((status, "status, error or diagnostics"),
                                (rejected, "status, error or diagnostics|header is not"),
                                (object_mismatch, "nondeterminism"),
                                (interval, "per-input intervals")):
            for purpose in ("production", "reproduction"):
                def selected(group, variant, which, value, mutate=mutate, purpose=purpose):
                    if variant == "candidate" and which == purpose:
                        mutate(group, variant, which, value)
                descriptor = self.untimed(("production", "reproduction"), selected)
                with self.subTest(message=message, purpose=purpose), \
                        self.assertRaisesRegex(ValueError, message):
                    self.check_untimed(descriptor, rows, untimed)
        good = self.untimed(("reproduction",))
        lines = [json.loads(line) for line in (self.root / good["path"]).read_bytes().splitlines()]
        for change, message in (
                (lambda value: value[0].update(command_sha256="e" * 64), "frozen contract"),
                (lambda value: value[0].update(exit_status=1), "frozen contract"),
                (lambda value: value[0].update(metrics_artifact=None), "lacks its per-input"),
                (lambda value: value.reverse(), "unique and ordered"),
                (lambda value: value[1].update(variant="baseline"), "unique and ordered"),
                (lambda value: value[0].update(group=1), "unknown group"),
                (lambda value: value[0].update(purpose="timed"), "unknown group, variant or purpose")):
            candidate = copy.deepcopy(lines)
            change(candidate)
            data = b"".join((json.dumps(item, sort_keys=True, separators=(",", ":")) + "\n").encode()
                            for item in candidate)
            descriptor = {**self.put(self.root, "execution/untimed-bad.jsonl", data),
                          "records": len(candidate)}
            with self.subTest(message=message), self.assertRaisesRegex(ValueError, message):
                self.check_untimed(descriptor, rows, untimed)

    def test_untimed_batches_prove_binary_and_process_independence(self):
        # (A1 review M2/L6) Each untimed batch runs the variant's subject binary
        # as its own supervisor-bound process, outside the timed window, and
        # shares no metrics path or content with any other batch.
        self.add_untimed_rows()
        self.attach()
        rows, untimed = self.untimed_contracts()
        execution = self.check()
        good = self.untimed(("production", "reproduction"))
        self.assertEqual(len(self.check_untimed(good, rows, untimed, execution)), 4)
        lines = [json.loads(line) for line in (self.root / good["path"]).read_bytes().splitlines()]
        timed = next(event for event in self.events if event["metrics_artifact"] is not None)
        copied = self.put_metrics(self.root, "retirement-metrics-copy-0000.txt",
                                  self.metrics_slice(self.root, lines[0]["metrics_artifact"]))

        def rebind(record, pid, token):
            record.update(pid=pid, process_start_token=token,
                          process_instance_sha256=binding._process_instance_digest(
                              self.receipt["job_id"], self.receipt["attempt"],
                              self.receipt["boot_id"], pid, token))

        cases = (
            (lambda value: value[0].update(executable_sha256=self.record["subjects"]["candidate"]
                                           ["binary"]["sha256"]), "bound subject binary"),
            (lambda value: value[0].update(pid=value[0]["pid"] + 1), "not supervisor-bound"),
            (lambda value: rebind(value[0], timed["pid"], timed["process_start_token"]),
             "reuses a timed or untimed invocation's process instance"),
            # A reproduction that is its production batch's process.
            (lambda value: rebind(value[1], value[0]["pid"], value[0]["process_start_token"]),
             "reuses a timed or untimed invocation's process instance"),
            (lambda value: value[0].update(started_ns=self.receipt["bound_at_ns"] + 1,
                                           finished_ns=self.receipt["bound_at_ns"] + 5_000_002),
             "timed collection window"),
            (lambda value: value[0].update(finished_ns=value[0]["started_ns"]), "empty or reversed"),
            (lambda value: value[1].update(started_ns=value[0]["started_ns"],
                                           finished_ns=value[0]["finished_ns"]),
             "overlap one another"),
            (lambda value: value[0].update(finished_ns=value[0]["started_ns"] + 100),
             "header is not one serial"),
            (lambda value: value[1].update(metrics_artifact=copied), "per-input metrics content"),
            (lambda value: value[0].update(metrics_artifact=dict(timed["metrics_artifact"])),
             "per-input metrics content"),
            (lambda value: value[0].pop("process_start_token"), "missing fields"),
        )
        for change, message in cases:
            candidate = copy.deepcopy(lines)
            change(candidate)
            data = b"".join((json.dumps(item, sort_keys=True, separators=(",", ":")) + "\n").encode()
                            for item in candidate)
            descriptor = {**self.put(self.root, "execution/untimed-bad.jsonl", data),
                          "records": len(candidate)}
            with self.subTest(message=message), self.assertRaisesRegex(ValueError, message):
                self.check_untimed(descriptor, rows, untimed, execution)

    def test_timed_batches_never_share_metrics_content(self):
        batches = [index for index, event in enumerate(self.events)
                   if event["metrics_artifact"] is not None]
        events = copy.deepcopy(self.events)
        first, later = events[batches[0]], events[batches[-1]]
        # The copy starts the writer's next shard, so only its content repeats.
        index = len({events[item]["metrics_artifact"]["path"] for item in batches[:-1]})
        duplicate = self.put_metrics(self.root, f"retirement-metrics-ab-{index:04d}.txt",
                                     self.metrics_slice(self.root, first["metrics_artifact"]))
        later["metrics_artifact"] = duplicate
        with self.assertRaisesRegex(ValueError, "per-input metrics content"):
            self.check(self.write_transcript(self.root, self.receipt, events))

    def test_untimed_group_contracts_are_the_derived_partition(self):
        self.add_untimed_rows()
        for change, message in (
                (lambda plan: plan["untimed_groups"].pop(), "derived A1 partition"),
                (lambda plan: plan["untimed_groups"][0].update(target=binding.NATIVE_TIMED_TARGET),
                 "configuration or recipe"),
                (lambda plan: plan["untimed_groups"][0]["controls"].append(self.rejection_control()),
                 "no controls"),
                (lambda plan: plan["untimed_groups"][0]["members"][0].update(
                    diagnostic_sha256=None), "diagnostic_sha256")):
            self.attach()
            self.rewrite_plan(change)
            with self.subTest(message=message), self.assertRaisesRegex(ValueError, message):
                self.check()

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
