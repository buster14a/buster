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
        self.assertEqual(hashlib.sha256(self.declaration).hexdigest(),
                         binding.SUPPORT_DECLARATION_SHA256)

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

    def check_plan(self, root, pairs):
        # This is the mandatory stage lower bound. Real population validation
        # separately joins every admitted link/self-host/workload row.
        sample_rows = self.object_rows + len(binding.STAGES) - 1
        required = sample_rows * 2 * pairs
        cap = binding.RESULT_INPUT_MAX_RECORDS
        manifests = [{
            "identity": f"manifest-{index}", "path": f"results/manifest-{index}.json",
            "start_record": start, "records": min(cap, required - start),
        } for index, start in enumerate(range(0, required, cap))]
        support = {"manifest_sha256": "a" * 64, "rows_sha256": "b" * 64,
                   "object_row_count": self.object_rows}
        plan = {
            "schema": binding.RESULT_INPUT_PLAN_SCHEMA, "version": 1,
            "source_manifest_sha256": support["manifest_sha256"],
            "source_rows_sha256": support["rows_sha256"],
            "identity_field": "record_id", "coordinate_schema": "row-round-pair-v1",
            "sample_population": "trusted-census-eligible-performance-rows-with-required-metrics",
            "eligible_population": "authenticated-applicability-minus-nonexecuted-rows",
            "object_row_count": self.object_rows, "sample_row_count": sample_rows,
            "rounds": 2, "pairs_per_round": pairs, "records_per_row": 2 * pairs,
            "required_records": required, "max_records_per_manifest": cap,
            "manifest_count": len(manifests), "manifests": manifests, "predeclared": True,
        }
        data = (json.dumps(plan, sort_keys=True, separators=(",", ":")) + "\n").encode()
        path = root / "plan.json"
        path.write_bytes(data)
        rules = {"sampling": {"rounds": 2, "pairs_per_round": pairs}}
        return binding._result_input_plan(root, self.descriptor(path.name, data),
                                          support, {}, rules)

    def test_current_population_and_stage_floor_fit_predeclared_partition(self):
        self.assertEqual(binding.RESULT_INPUT_MAX_TOTAL_RECORDS, 39_518_208)
        sample_rows = self.object_rows + len(binding.STAGES) - 1
        maximum_even = min(256, binding.RESULT_INPUT_MAX_TOTAL_RECORDS // (2 * sample_rows))
        maximum_even -= maximum_even % 2
        self.assertGreaterEqual(maximum_even, 60, "current population cannot fit minimum sampling")
        with tempfile.TemporaryDirectory() as directory:
            plan = self.check_plan(Path(directory), maximum_even)
            self.assertEqual(plan["sample_row_count"], sample_rows)
            self.assertEqual(sum(part["records"] for part in plan["manifests"]),
                             sample_rows * 2 * maximum_even)

    def test_current_population_over_cap_cannot_be_rescued_by_more_shards(self):
        sample_rows = self.object_rows + len(binding.STAGES) - 1
        required = sample_rows * 2 * 256
        self.assertGreater(required, binding.RESULT_INPUT_MAX_TOTAL_RECORDS)
        with tempfile.TemporaryDirectory() as directory:
            with self.assertRaisesRegex(ValueError, "immutable total-record ceiling"):
                self.check_plan(Path(directory), 256)


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

    @classmethod
    def attach_execution(cls, root, record, parsed, samples):
        """Attach a complete test-only transcript to an existing small fixture.

        ``samples`` maps (row, round, pair) to metric -> {baseline, candidate}.
        Frozen oracle outputs are test values, never deployment evidence.
        """
        sampling = record["rules"]["sampling"]
        oracle_path = record["workflow"]["records"]["oracle"]["path"]
        oracle_by_row = {item["row"]: item for item in json.loads((root / oracle_path).read_text())["records"]}
        contracts = []
        for row in parsed:
            contract = {"row": row["row"], "identity_sha256": binding._canonical_json_digest(row["identity"]),
                        "oracle_sha256": binding._canonical_json_digest(oracle_by_row[row["row"]])}
            for side in ("baseline", "candidate"):
                metrics = samples.get((row["row"], 0, 0), {})
                observed = oracle_by_row[row["row"]]["code_section_status"] == "parsed-deterministic"
                code_bytes = (metrics["generated_code_bytes"][side]
                              if row["metrics"]["generated_code_bytes"] else 0 if observed else None)
                contract[side] = {
                    "compiler_command_sha256": (hashlib.sha256(f"compile/{row['row']}/{side}".encode()).hexdigest()
                                                if row["metrics"]["compiler_wall_time"] else None),
                    "artifact_sha256": (hashlib.sha256(f"artifact/{row['row']}/{side}".encode()).hexdigest()
                                        if row["metrics"]["compiler_wall_time"] else None),
                    "code_section_sha256": (hashlib.sha256(b"").hexdigest() if code_bytes == 0
                                             else "b" * 64 if code_bytes is not None else None),
                    "code_section_bytes": code_bytes,
                    "runtime_command_sha256": ("c" * 64 if row["metrics"]["generated_runtime"] else None),
                    "runtime_output_sha256": ("d" * 64 if row["metrics"]["generated_runtime"] else None),
                }
            contracts.append(contract)
        plan = {"schema": binding.EXECUTION_PLAN_SCHEMA, "version": 1,
                "schedule": binding.EXECUTION_SCHEDULE, "cpu": 3,
                "performance_rows_sha256": binding._support_file(record["support"], "performance_rows")["sha256"],
                "rows": contracts}
        for key in ("seed", "rounds", "pairs_per_round", "warmups_per_variant"):
            plan[key] = sampling[key]
        plan_descriptor = cls.put(root, "execution/invocation-plan.json", plan)
        by_row = {item["row"]: item for item in contracts}
        events = []
        last_end = 100
        job_id = "synthetic-job"
        attempt = 1
        boot_id = "synthetic-boot"
        for identity in binding._execution_schedule(parsed, sampling):
            event = dict(identity)
            compiler = event["kind"] == "compiler"
            side = by_row[event["row"]][event["variant"]]
            key = ((event["row"], event["round"], event["pair"]) if event["phase"] == "sample"
                   else (event["row"], 0, 0))
            metrics = samples[key]
            seconds = metrics["compiler_wall_time" if compiler else "generated_runtime"][event["variant"]]
            duration_ns = int(round(seconds * 1_000_000_000))
            pid = 2000 + event["sequence"]
            process_start_token = f"synthetic-process-{event['sequence']}"
            event.update({
                "pid": pid, "process_start_token": process_start_token,
                "process_instance_sha256": binding._process_instance_digest(
                    job_id, attempt, boot_id, pid, process_start_token),
                "cpu": 3,
                "started_ns": last_end + 1, "finished_ns": last_end + 1 + duration_ns,
                "exit_code": 0, "signal": 0, "timed_out": False, "cancelled": False,
                "executable_sha256": (record["subjects"][event["variant"]]["binary"]["sha256"]
                                      if compiler else side["artifact_sha256"]),
                "command_sha256": side["compiler_command_sha256" if compiler else "runtime_command_sha256"],
                "output_sha256": side["artifact_sha256" if compiler else "runtime_output_sha256"],
                "code_section_sha256": side["code_section_sha256"] if compiler else None,
                "code_section_bytes": side["code_section_bytes"] if compiler else None,
                "wall_seconds": seconds,
                "peak_rss_bytes": metrics["compiler_peak_rss"][event["variant"]] if compiler else None,
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

    def setUp(self):
        import sqlite3
        from native_retirement_performance_binding_test import BindingTests
        self.directory = tempfile.TemporaryDirectory(prefix="invocation-regression-")
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)
        self.db = sqlite3.connect(":memory:")
        self.addCleanup(self.db.close)
        self.db.execute("CREATE TABLE samples(row_id INTEGER, round_id INTEGER, pair_id INTEGER, "
                        "metric TEXT, baseline TEXT, candidate TEXT, PRIMARY KEY(row_id, round_id, pair_id, metric))")
        self.rules = BindingTests._rules()
        template = BindingTests._series_join_fixture()[0][0]
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
        oracle = self.put(self.root, "execution/oracle.json", {"records": [
            {"row": i, "code_section_status": "parsed-deterministic",
             "code_section_bytes": 100, "code_section_sha256": "b" * 64,
             "runtime_oracle_status": "passed-native" if i == 0 else "not-applicable",
             "runtime_exit_code": 0 if i == 0 else -1, "native_runtime": i == 0}
            for i in range(2)]})
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
        self.samples = {}
        for row in self.rows:
            for round_id in range(2):
                for pair in range(60):
                    metrics = {}
                    for metric in binding.METRICS:
                        if not row["metrics"][metric]:
                            continue
                        values = {}
                        for side_index, side in enumerate(("baseline", "candidate")):
                            if metric in ("compiler_wall_time", "generated_runtime"):
                                values[side] = (1_000_000 + 1000 * row["row"] + 100 * side_index + 60 * round_id + pair) / 1e9
                            else:
                                values[side] = 100 + side_index
                        metrics[metric] = values
                        self.db.execute("INSERT INTO samples VALUES (?, ?, ?, ?, ?, ?)",
                                        (row["row"], round_id, pair, metric,
                                         str(values["baseline"]), str(values["candidate"])))
                    self.samples[(row["row"], round_id, pair)] = metrics
        (self.plan, self.receipt, self.descriptor, self.events, self.raw_digest) = self.attach_execution(
            self.root, self.record, self.rows, self.samples)

    def check(self, descriptor=None, trusted=None):
        descriptor = descriptor or self.descriptor
        return binding._check_execution_transcript(
            self.root, descriptor, self.plan, self.record, self.rows,
            self.rules["sampling"], self.db, self.raw_digest,
            trusted if trusted is not None else descriptor["sha256"],
            3, "x86_64-unknown-linux-gnu")

    def test_complete_warmups_and_native_runtime_join(self):
        result = self.check()
        self.assertEqual(result["invocations"], 3 * (4 + 2 * 60 * 2))

    def test_mixed_untimed_and_empty_code_rows_replay_complete_transcript(self):
        # The zero-code row still runs its compiler; the retained control does not.
        empty = hashlib.sha256(b"").hexdigest()
        zero = self.rows[1]
        zero["metrics"]["generated_code_bytes"] = False
        zero["eligibility"]["generated_code_bytes"] = False
        zero["eligibility"]["code_section"] = "deterministic-zero-baseline-code-section"
        for coordinate, metrics in self.samples.items():
            if coordinate[0] == 1:
                del metrics["generated_code_bytes"]
        self.db.execute("DELETE FROM samples WHERE row_id=1 AND metric='generated_code_bytes'")
        control = copy.deepcopy(zero)
        control["row"] = 2
        control["identity"]["fixture"] = "tests/untimed-control.c"
        control["identity"]["compile_obligation"] = "registered-non-object-control"
        for metric in binding.METRICS:
            control["metrics"][metric] = False
            control["eligibility"][metric] = False
        control["eligibility"]["code_section"] = "not-applicable"
        self.rows.append(control)
        oracle_path = self.record["workflow"]["records"]["oracle"]["path"]
        oracle = json.loads((self.root / oracle_path).read_text())
        oracle["records"][1].update(code_section_bytes=0, code_section_sha256=empty)
        oracle["records"].append({"row": 2, "code_section_status": "not-applicable",
                                  "code_section_bytes": None, "code_section_sha256": None,
                                  "runtime_oracle_status": "not-applicable",
                                  "runtime_exit_code": None, "native_runtime": False})
        self.record["workflow"]["records"]["oracle"] = self.put(self.root, oracle_path, oracle)
        self.plan, self.receipt, self.descriptor, self.events, self.raw_digest = self.attach_execution(
            self.root, self.record, self.rows, self.samples)
        self.assertEqual(self.check()["invocations"], 3 * (4 + 2 * 60 * 2))
        self.assertNotIn(2, {event["row"] for event in self.events})
        self.assertEqual({event["code_section_bytes"] for event in self.events
                          if event["row"] == 1}, {0})

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

    def test_mismatched_binary_command_oracle_and_sections_reject(self):
        for field, bad in (("executable_sha256", "0" * 64), ("command_sha256", "0" * 64),
                           ("output_sha256", "0" * 64), ("code_section_sha256", "0" * 64),
                           ("code_section_bytes", 2), ("cpu", 4)):
            events = copy.deepcopy(self.events)
            events[0][field] = bad
            with self.subTest(field=field), self.assertRaises(ValueError):
                self.check(self.write_transcript(self.root, self.receipt, events))

    def test_workflow_checks_actual_invocations_before_calling_statistics(self):
        # Exercise the production workflow, real #615 streaming, and the new
        # receipt join. The sole sentinel is the NEXT stage: this deliberately
        # does not stand in for the separately required full C/replay test.
        support = {"support_declaration_sha256": "a" * 64,
                   "manifest_sha256": "b" * 64, "rows_sha256": "c" * 64,
                   "object_row_count": len(self.rows)}
        data = b"".join((json.dumps({
            "record_id": f"row-{row}/round-{round_id}/pair-{pair}",
            "row": row, "round": round_id, "pair": pair, "measurements": measurements,
        }, sort_keys=True, separators=(",", ":")) + "\n").encode()
            for (row, round_id, pair), measurements in sorted(self.samples.items()))
        shard = self.put(self.root, "results/numeric.jsonl", data)
        manifest = self.put(self.root, "results/input.json", {
            "schema": binding.RESULT_INPUT.MANIFEST_SCHEMA, "version": 1,
            "identity_field": "record_id", "shards": [{"identity": "numeric", **shard}]})
        plan = self.put(self.root, "results/input-plan.json", {
            "schema": binding.RESULT_INPUT_PLAN_SCHEMA, "version": 1,
            "source_manifest_sha256": support["manifest_sha256"],
            "source_rows_sha256": support["rows_sha256"],
            "identity_field": "record_id", "coordinate_schema": "row-round-pair-v1",
            "sample_population": "trusted-census-eligible-performance-rows-with-required-metrics",
            "eligible_population": "authenticated-applicability-minus-nonexecuted-rows", "object_row_count": 2,
            "sample_row_count": 2, "rounds": 2, "pairs_per_round": 60, "records_per_row": 120,
            "required_records": 240, "max_records_per_manifest": binding.RESULT_INPUT_MAX_RECORDS,
            "manifest_count": 1, "manifests": [{"identity": "numeric", "path": manifest["path"],
                "start_record": 0, "records": 240}], "predeclared": True})
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
        self.raw_digest = hashlib.sha256(data).hexdigest()
        self.receipt["context_sha256"] = binding._canonical_json_digest(
            binding._execution_context(self.record, self.raw_digest))
        self.descriptor = self.write_transcript(self.root, self.receipt, self.events)
        adapter = self.put(self.root, "results/series.txt", b"not reached by this boundary test\n")
        result = {"schema": binding.RESULT_BUNDLE_SCHEMA, "version": 1,
                  "source_rows_sha256": "c" * 64, "result_input_plan_sha256": plan["sha256"],
                  "family_sha256": self.family["sha256"], "result_manifests": [{
                      "identity": "numeric", **manifest, "start_record": 0, "records": 240,
                      "input_bytes": manifest["bytes"] + shard["bytes"]}],
                  "raw_measurements_sha256": self.raw_digest,
                  "member_invocations_sha256": binding._family_invocation_digest(self.family),
                  "member_count": len(self.family["members"]), "scopes_per_member": 3,
                  "adapter_input": adapter, "execution_receipt": self.descriptor,
                  "code_bytes_summary": binding._code_bytes_summary(self.rows, self.db, 2, 60)}
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
        oracle_path = self.record["workflow"]["records"]["oracle"]["path"]
        original = json.loads((self.root / oracle_path).read_text())
        for field, bad in (("runtime_oracle_status", "not-applicable"),
                           ("runtime_exit_code", 1), ("runtime_exit_code", False),
                           ("native_runtime", False), ("code_section_status", "unknown")):
            oracle = copy.deepcopy(original)
            oracle["records"][0][field] = bad
            self.record["workflow"]["records"]["oracle"] = self.put(self.root, oracle_path, oracle)
            (self.plan, self.receipt, self.descriptor, self.events, self.raw_digest) = self.attach_execution(
                self.root, self.record, self.rows, self.samples)
            with self.subTest(field=field, bad=bad), self.assertRaisesRegex(ValueError, "oracle"):
                self.check()

    def test_runtime_applicability_is_derived_from_frozen_row_obligation(self):
        rows = copy.deepcopy(self.rows)
        rows[0]["identity"]["execution_obligation"] = "unavailable-platform-control"
        plan, receipt, descriptor, _events, raw_digest = self.attach_execution(
            self.root, self.record, rows, self.samples)
        with self.assertRaisesRegex(ValueError, "frozen row obligation"):
            binding._check_execution_transcript(
                self.root, descriptor, plan, self.record, rows,
                self.rules["sampling"], self.db, raw_digest,
                descriptor["sha256"], 3, "x86_64-unknown-linux-gnu")

    def test_positive_but_wrong_result_measurement_rejects(self):
        self.db.execute("UPDATE samples SET baseline='1000' WHERE metric='compiler_peak_rss'")
        with self.assertRaisesRegex(ValueError, "not the authenticated invocation"):
            self.check()

    def test_overlapping_processes_and_false_clock_measurement_reject(self):
        for field, bad in (("started_ns", 99), ("finished_ns", 10), ("wall_seconds", 50),
                           ("row", False), ("sequence", False), ("peak_rss_bytes", True)):
            events = copy.deepcopy(self.events)
            events[0][field] = bad
            with self.subTest(field=field), self.assertRaises(ValueError):
                self.check(self.write_transcript(self.root, self.receipt, events))

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
                actual = [(e["round"], e["pair"], e["row"], e["position"],
                           int(e["variant"] == "candidate"))
                          for e in binding._execution_schedule(self.rows, sampling)
                          if e["kind"] == "compiler" and e["phase"] == "sample"]
                self.assertEqual(actual, expected)

    def test_schedule_balances_every_two_pair_block(self):
        by_pair = {}
        for event in binding._execution_schedule(self.rows, self.rules["sampling"]):
            if event["phase"] == "sample":
                key = event["kind"], event["row"], event["round"], event["pair"]
                by_pair.setdefault(key, []).append(event["variant"])
        for (kind, row, round_id, pair), variants in by_pair.items():
            self.assertEqual(set(variants), {"baseline", "candidate"})
            if not pair & 1:
                self.assertEqual(variants[::-1], by_pair[(kind, row, round_id, pair + 1)])
        for seed in (0, 1 << 64, True):
            rules = copy.deepcopy(self.rules["sampling"])
            rules["seed"] = seed
            with self.subTest(seed=seed), self.assertRaises(ValueError):
                list(binding._execution_schedule(self.rows, rules))


if __name__ == "__main__":
    unittest.main()
