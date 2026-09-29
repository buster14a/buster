#!/usr/bin/env python3
"""End-to-end check of the #881-E C result composer against the #511 validator.

Usage: retirement_compose_test.py COMPOSE_BINARY [THROUGHPUT_TEST_DIRECTORY]

COMPOSE_BINARY is build/bench-service-tools/retirement-compose-tests, whose
`compose SPEC` mode plans a fresh result store, imports lane D's stream files,
runs the composer (including the reviewed `bench_throughput retirement-replay`
adapter) and issues the producer authority.

test_composed_sealed_result_validates builds the bounded A1 binding fixture of
native_retirement_performance_binding_test (one native object batch group with
a frozen rejection control, one native link row with runtime, one cross-target
untimed code row) and its D-format streams: the invocation transcript, the
per-batch metrics shards and untimed batch records of
native_retirement_performance_identity_test, and #615 row/batch shards in D's
canonical record form. Everything after the post-A/A phase -- manifests, code
records, statistics input and output, execution receipt, result bundle and the
`workflow.phases.sealed_result` seal -- is produced by the C composer. The test
then adds the independent-replay phase and runs the unchanged validator
end to end with the composer's receipt digest as the out-of-band trust root.

test_throughput_fixture_streams (only with THROUGHPUT_TEST_DIRECTORY, the
output of `bench_throughput self-test`) composes lane D's own C-encoded
full-invocation fixture and checks the composed receipt, manifests and
statistics input with the validator's transcript, #615 and adapter-series
readers. That fixture binds placeholder executable digests, so it cannot back
a complete binding; the first test covers the full validate() path.

Spec directives written here (one per line): source, store, evidence, scratch,
adapter, authority, context, sealed, identity JOB ATTEMPT BOOT BOUND COMPLETED,
digests PLAN ROWS RESULT_PLAN FAMILY POST_AA, statistics SEED PAIRS RESAMPLES
BOOTSTRAP CELLS, population ROWS, metrics-budget HEADER PER_INPUT,
group KIND INPUTS, untimed-group KIND INPUTS, row ID GROUP RUNTIME DIMS*6,
code ROW (ARTIFACT BYTES CODE REPRODUCTION)*2, partition rows|batches ID PATH
START RECORDS, transcript|metrics|untimed|untimed-metrics|retained PATH [SOURCE],
samples rows|batches PATH [SOURCE], prior NAME PATH BYTES SHA256.
No fixture here is service admission or performance evidence.
"""

from contextlib import closing
import copy
import hashlib
import io
import json
import os
from pathlib import Path
import sqlite3
import subprocess
import sys
import tarfile
import tempfile
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
import native_retirement_performance_binding_test as binding_tests  # noqa: E402
import native_retirement_performance_identity_test as identity_tests  # noqa: E402

binding = binding_tests.binding
COMPOSE_BINARY = None
THROUGHPUT_DIRECTORY = None
PLACEHOLDER = "0" * 64


def _canonical(value):
    return (json.dumps(value, sort_keys=True, separators=(",", ":")) + "\n").encode("utf-8")


def _descriptor(data, path):
    return {"path": path, "bytes": len(data), "sha256": hashlib.sha256(data).hexdigest()}


def _records(prefix, values):
    return b"".join(_canonical({
        "record_id": f"{prefix}-{unit}/round-{round_number}/pair-{pair}",
        prefix: unit, "round": round_number, "pair": pair, "measurements": measurements,
    }) for (unit, round_number, pair), measurements in sorted(values.items()))


def _context_template(record):
    value = binding._execution_context(record, PLACEHOLDER)
    return json.dumps(value, sort_keys=True, separators=(",", ":"), ensure_ascii=False).encode("utf-8")


def _run_compose(spec_lines, directory):
    spec = Path(directory) / "compose.spec"
    spec.write_text("\n".join(spec_lines) + "\n", encoding="utf-8")
    process = subprocess.run([str(COMPOSE_BINARY), "compose", str(spec)], check=False,
                             capture_output=True, text=True)
    lines = [line for line in process.stdout.splitlines() if line.startswith("COMPOSE_RESULT ")]
    if process.returncode != 0 or len(lines) != 1:
        raise AssertionError(f"composer refused: rc={process.returncode} stderr={process.stderr}")
    return json.loads(lines[0][len("COMPOSE_RESULT "):])


def _row_line(row, group, runtime):
    identity = row["identity"]
    dims = [identity[field] for field in binding.STATISTICAL_DIMENSIONS]
    return f"row {row['row']} {group} {int(runtime)} " + " ".join(dims)


class ComposeEndToEndTests(unittest.TestCase):
    """The composer's sealed result passes the unchanged binding validator."""

    def _fixture(self, evidence, source):
        """The bounded binding of _build_small_evidence_fixture up to post-A/A,
        plus lane D's streams in `source`; returns what composition needs."""
        tests = binding_tests.BindingTests("test_complete_record_structural_contract_is_accepted")
        record, contents = tests.make_record()

        def put(path, data):
            if isinstance(data, str):
                data = data.encode("utf-8")
            contents[path] = data
            return _descriptor(data, path)

        harness_commit = subprocess.run(["git", "rev-parse", "HEAD"], cwd=ROOT, check=True,
                                        capture_output=True, text=True).stdout.strip()
        harness_tree = subprocess.run(["git", "rev-parse", "HEAD^{tree}"], cwd=ROOT, check=True,
                                      capture_output=True, text=True).stdout.strip()
        record["measurement"]["harness_source_commit"] = harness_commit
        record["measurement"]["harness_source_tree"] = harness_tree
        relation_descriptor = record["provenance"]["relation_receipt"]
        relation = json.loads(contents[relation_descriptor["path"]].decode("utf-8"))
        relation["harness"].update({"source_commit": harness_commit, "source_tree": harness_tree})
        relation_descriptor.update(put(relation_descriptor["path"], _canonical(relation)))

        support_files = record["support"]["files"]
        performance_rows_descriptor = support_files[binding.SUPPORT_FILE_ROLES.index("performance_rows")]
        rows_record = json.loads(contents[performance_rows_descriptor["path"]].decode())
        cross_census_row = next(index for index, item in enumerate(rows_record["rows"])
                                if item["identity"]["target"] != binding.NATIVE_TIMED_TARGET)
        object_row = copy.deepcopy(rows_record["rows"][0])
        object_row["row"] = 0
        object_row["eligibility"] = {
            "compiler_wall_time": True, "compiler_peak_rss": True,
            "generated_code_bytes": True, "generated_runtime": False,
            "runtime_oracle": "not-applicable", "code_section": "deterministic-code-section",
        }
        runtime_row = copy.deepcopy(object_row)
        runtime_row["row"] = 1
        runtime_row["identity"]["artifact_stage"] = "link"
        runtime_row["eligibility"].update({
            "generated_runtime": True, "runtime_oracle": "independent-native-executable-oracle"})
        cross_row = copy.deepcopy(rows_record["rows"][cross_census_row])
        cross_row["row"] = 2
        cross_row["eligibility"] = copy.deepcopy(object_row["eligibility"])
        rows_record["rows"] = [object_row, runtime_row, cross_row]
        rows_data = _canonical(rows_record)
        performance_rows_descriptor.update(put(performance_rows_descriptor["path"], rows_data))
        parsed, axes, family = binding._performance_rows(rows_data)
        timed = binding._timed_rows(parsed)
        groups = binding._batch_groups(parsed)
        self.assertEqual([(group["kind"], group["rows"]) for group in groups],
                         [(binding.OBJECT_BATCH_GROUP, [0]), (binding.SINGLETON_STAGE_GROUP, [1])])

        performance_descriptor = support_files[binding.SUPPORT_FILE_ROLES.index("performance_declaration")]
        performance = json.loads(contents[performance_descriptor["path"]].decode())
        performance.update({"performance_rows_sha256": performance_rows_descriptor["sha256"],
                            "required_row_count": 3, "axes": axes, "statistical_family": family})
        performance_descriptor.update(put(performance_descriptor["path"], _canonical(performance)))
        record["support"]["root_sha256"] = binding._support_root_digest(
            support_files, record["support"]["validator"], record["support"]["closure"])
        record["population"].update({
            "required_row_count": 3, "required_rows_sha256": performance_rows_descriptor["sha256"],
            "axes": axes, "statistical_family": family,
            "source_digests": binding._artifact_digest_map(record["support"]),
        })
        aa_descriptor = record["execution"]["host"]["aa_admission_receipt"]
        aa_value = json.loads(contents[aa_descriptor["path"]].decode())
        aa_value["family_sha256"] = family["sha256"]
        aa_descriptor.update(put(aa_descriptor["path"], _canonical(aa_value)))
        counts = binding._family_member_counts(family)
        record["rules"]["sampling"].update({
            "bootstrap_members_per_scope": counts["bootstrap_members_per_scope"],
            "cell_members_per_scope": counts["cell_members_per_scope"],
        })

        admission_descriptor = record["workflow"]["records"]["admission"]
        admission = json.loads(contents[admission_descriptor["path"]].decode())
        template = copy.deepcopy(admission["records"][0])
        admission["records"] = []
        for row in parsed:
            item = copy.deepcopy(template)
            stage = row["identity"]["artifact_stage"]
            item.update({"row": row["row"], "census_row": cross_census_row if row["row"] == 2 else 0,
                         "identity": row["identity"], "artifact_stage": stage,
                         "artifact_kind": "object" if stage == "object" else "linked-executable"})
            admission["records"].append(item)
        admission_descriptor.update(put(admission_descriptor["path"], _canonical(admission)))
        oracle_descriptor = record["workflow"]["records"]["oracle"]
        oracle = json.loads(contents[oracle_descriptor["path"]].decode())
        template = copy.deepcopy(oracle["records"][0])
        oracle["records"] = []
        for row in parsed:
            runtime = row["metrics"]["generated_runtime"]
            item = copy.deepcopy(template)
            item.update({"row": row["row"],
                         "runtime_oracle_status": "passed-native" if runtime else "not-applicable",
                         "runtime_exit_code": 0 if runtime else -1, "native_runtime": runtime})
            oracle["records"].append(item)
        oracle_descriptor.update(put(oracle_descriptor["path"], _canonical(oracle)))

        samples, batch_samples = identity_tests.InvocationEvidenceTests.synthetic_samples(timed)
        row_manifest, batch_manifest = "retirement-rows-manifest-0.json", "retirement-batches-manifest-0.json"
        plan_value = identity_tests.InvocationEvidenceTests.result_input_plan(
            support_files[binding.SUPPORT_FILE_ROLES.index("manifest")]["sha256"],
            support_files[binding.SUPPORT_FILE_ROLES.index("rows")]["sha256"],
            2, 2, 1, row_path=row_manifest, batch_path=batch_manifest)
        plan_descriptor = record["workflow"]["records"]["result_input_plan"]
        plan_descriptor.update(put(plan_descriptor["path"], _canonical(plan_value)))

        pre_value = {
            "schema": binding.PHASE_SCHEMA["pre_sample_plan"], "version": 1,
            "status": "frozen-before-samples",
            "support_declaration_sha256": support_files[0]["sha256"],
            "manifest_sha256": support_files[2]["sha256"], "rows_sha256": support_files[4]["sha256"],
            "family_sha256": family["sha256"], "seed": record["rules"]["sampling"]["seed"],
            "rounds": 2, "pairs_per_round": 60, "resamples": record["rules"]["sampling"]["resamples"],
            "bootstrap_members_per_scope": counts["bootstrap_members_per_scope"],
            "cell_members_per_scope": counts["cell_members_per_scope"],
            "result_input_plan_sha256": plan_descriptor["sha256"],
        }
        for path, data in contents.items():
            target = source / path
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(data)
        execution_plan, execution_receipt, _receipt, _events, _digest = identity_tests.InvocationEvidenceTests.attach_execution(
            source, record, parsed, samples, batch_samples, {0: [identity_tests.InvocationEvidenceTests.rejection_control()]})
        untimed = identity_tests.InvocationEvidenceTests.untimed_batches(
            source, execution_plan, parsed, ("production", "reproduction"), record=record,
            receipt=execution_receipt)
        self.assertEqual(untimed["records"], 4)
        contents[execution_plan["path"]] = (source / execution_plan["path"]).read_bytes()
        pre_value["execution_plan"] = execution_plan
        pre_descriptor = put("workflow/pre-sample-plan.json", _canonical(pre_value))
        record["workflow"]["phases"]["pre_sample_plan"] = pre_descriptor
        post_value = dict(pre_value)
        post_value.update({
            "schema": binding.PHASE_SCHEMA["post_aa_binding"], "status": "bound-after-aa-before-samples",
            "pre_sample_plan_sha256": pre_descriptor["sha256"],
            "aa_admission_sha256": record["execution"]["host"]["aa_admission_receipt"]["sha256"],
        })
        post_descriptor = put("workflow/post-aa-binding.json", _canonical(post_value))
        record["workflow"]["phases"]["post_aa_binding"] = post_descriptor

        # Lane D's #615 row and batch shards, in its canonical record form.
        (source / "retirement-samples-0000.jsonl").write_bytes(_records("row", samples))
        (source / "retirement-batches-0000.jsonl").write_bytes(_records("group", batch_samples))
        tests.write_evidence(evidence, record, contents)

        prior = [("contract.source", record["contract"]["source"])]
        prior += [(name, artifact) for name, artifact in binding._all_artifacts(record)
                  if name not in ("workflow.phases.sealed_result", "workflow.phases.independent_replay")]
        prior.append(("workflow.execution_plan", execution_plan))
        plan = json.loads((source / execution_plan["path"]).read_text())
        code_lines = []
        for contract in plan["rows"]:
            if contract["baseline"]["code_section_bytes"] is None:
                continue
            sides = []
            for side in ("baseline", "candidate"):
                facts = contract[side]
                sides += [facts["artifact_sha256"], str(facts["code_section_bytes"]),
                          facts["code_section_sha256"], facts["artifact_sha256"]]
            code_lines.append(f"code {contract['row']} " + " ".join(sides))
        spec = [
            f"source {source}", f"store {evidence}", f"scratch {source / 'scratch'}",
            f"context {source / 'context.json'}", "sealed retirement-sealed-result.json",
            f"identity {execution_receipt['job_id']} {execution_receipt['attempt']} "
            f"{execution_receipt['boot_id']} {execution_receipt['bound_at_ns']} "
            f"{execution_receipt['completed_at_ns']}",
            f"digests {execution_plan['sha256']} {support_files[4]['sha256']} {plan_descriptor['sha256']} "
            f"{family['sha256']} {post_descriptor['sha256']}",
            f"statistics {record['rules']['sampling']['seed']} 60 {record['rules']['sampling']['resamples']} "
            f"{counts['bootstrap_members_per_scope']} {counts['cell_members_per_scope']}",
            "population 3", "metrics-budget 4096 16384",
            "group object 2", "group singleton 1", "untimed-group object 1",
            _row_line(parsed[0], 0, False), _row_line(parsed[1], 1, True),
            *code_lines,
            f"partition rows rows-0 {row_manifest} 0 240",
            f"partition batches batches-0 {batch_manifest} 0 120",
            "transcript retirement-execution-ab-0000.jsonl execution/invocations.jsonl",
            "samples rows retirement-samples-0000.jsonl", "samples batches retirement-batches-0000.jsonl",
            *[f"metrics {path.name}" for path in sorted(source.glob("retirement-metrics-ab-*.txt"))],
            f"untimed retirement-untimed-batches.jsonl {untimed['path']}",
            *[f"untimed-metrics {path.name}" for path in sorted(source.glob("retirement-metrics-untimed-*.txt"))],
            *[f"prior {name} {artifact['path']} {artifact['bytes']} {artifact['sha256']}" for name, artifact in prior],
        ]
        (source / "scratch").mkdir(mode=0o700)
        (source / "context.json").write_bytes(_context_template(record))
        census_rows_path = support_files[binding.SUPPORT_FILE_ROLES.index("rows")]["path"]
        support_output = {
            "manifest": {}, "inputs": [], "dependencies": [], "environment": [], "sources": {},
            "rows": binding._tsv(contents[census_rows_path], binding.ROW_FIELDS, "small census rows"),
            "axes": axes, "family": family,
            "support_declaration_sha256": support_files[0]["sha256"],
            "manifest_sha256": support_files[2]["sha256"], "rows_sha256": support_files[4]["sha256"],
            "performance_rows_sha256": performance_rows_descriptor["sha256"],
            "validator_report_sha256": support_files[8]["sha256"],
            "object_row_count": 2, "eligible_object_row_count": 2,
            "compiler_eligible_rows": {row["row"] for row in parsed}, "group_count": 1,
        }
        return record, spec, support_output, family

    def _independent_replay(self, record, evidence, composed, family):
        """The #510 publication and independent-replay phase over the sealed
        closure the composer produced (lane F's step, reproduced for the test)."""
        sealed_descriptor = composed["sealed"]
        sealed = json.loads((evidence / sealed_descriptor["path"]).read_bytes())
        bundle_descriptor = sealed["result_bundle"]
        bundle = json.loads((evidence / bundle_descriptor["path"]).read_bytes())
        record["workflow"]["phases"]["sealed_result"] = {key: sealed_descriptor[key]
                                                         for key in ("path", "bytes", "sha256")}

        def put(path, data):
            (evidence / path).write_bytes(data)
            return _descriptor(data, path)

        with tempfile.TemporaryDirectory(prefix="retirement-compose-adapter-") as adapter_directory:
            _executable, _binary, source_digest, toolchain_digest, build_command = \
                binding._compile_trusted_retirement_adapter(Path(adapter_directory))
        publication_id = "published-retirement-bundle-e"
        downloaded_files = sealed["seal"]["files"] + [{
            "name": "workflow.phases.sealed_result", "path": sealed_descriptor["path"],
            "bytes": sealed_descriptor["bytes"], "sha256": sealed_descriptor["sha256"]}]
        archive_files = [{key: item[key] for key in ("path", "bytes", "sha256")} for item in downloaded_files]
        archive_manifest = {
            "schema": "buster-native-retirement-independent-bundle-manifest-v1", "version": 1,
            "publication_id": publication_id, "files": archive_files,
            "root_sha256": binding._canonical_files_digest(
                [{"name": item["path"], **item} for item in archive_files]),
        }
        buffer = io.BytesIO()
        with tarfile.open(fileobj=buffer, mode="w") as archive:
            manifest_bytes = _canonical(archive_manifest)
            info = tarfile.TarInfo("bundle-manifest.json")
            info.size = len(manifest_bytes)
            archive.addfile(info, io.BytesIO(manifest_bytes))
            for item in downloaded_files:
                payload = (evidence / item["path"]).read_bytes()
                info = tarfile.TarInfo(item["path"])
                info.size = len(payload)
                archive.addfile(info, io.BytesIO(payload))
        downloaded = put("retirement-downloaded-independent.bundle.tar", buffer.getvalue())
        publication = put("retirement-performance-publication.json", _canonical({
            "schema": binding.PUBLICATION_SCHEMA, "version": 1,
            "publisher": "native-retirement-evidence-v1", "release": "retirement-v1",
            "run_id": "run-e-composer", "service_id": record["execution"]["service"]["id"],
            "publication_id": publication_id, "sealed_result_sha256": bundle_descriptor["sha256"],
            "published_bundle_sha256": downloaded["sha256"], "downloaded_bundle_sha256": downloaded["sha256"],
            "replay_result": "independently-replayed"}))
        replay = put("retirement-independent-replay.bundle", _canonical({
            "schema": binding.REPLAY_BUNDLE_SCHEMA, "version": 1,
            "sealed_result_sha256": bundle_descriptor["sha256"],
            "raw_measurements_sha256": composed["raw_measurements_sha256"],
            "family_sha256": family["sha256"],
            "member_invocations_sha256": binding._family_invocation_digest(family),
            "member_count": len(family["members"]),
            "adapter_command": "bench_throughput retirement-replay --input SERIES_FILE --output RESULT_JSON",
            "adapter_build_command": build_command, "adapter_toolchain_sha256": toolchain_digest,
            "adapter_source_sha256": source_digest,
            "code_bytes_summary_sha256": binding._canonical_json_digest(bundle["code_bytes_summary"]),
            "untimed_batches_sha256": bundle["untimed_batches"]["sha256"],
            "publication_id": publication_id, "published_bundle_sha256": downloaded["sha256"],
            "downloaded_bundle_sha256": downloaded["sha256"], "downloaded_bundle": downloaded,
            "adapter_result": {key: composed["replay"][key] for key in ("path", "bytes", "sha256")},
            "publication_receipt": publication}))
        record["workflow"]["phases"]["independent_replay"] = put(
            "retirement-independent-replay.json", _canonical({
                "schema": binding.PHASE_SCHEMA["independent_replay"], "version": 1,
                "status": "independently-replayed", "sealed_result_sha256": sealed_descriptor["sha256"],
                "replay_bundle": replay, "publication_receipt": publication}))

    def test_composed_sealed_result_validates(self):
        tests = binding_tests.BindingTests("test_complete_record_structural_contract_is_accepted")
        with tests._adapter_checkout() as repository, \
                mock.patch.object(binding, "__file__",
                                  str(repository / "tools" / "native_retirement_performance_binding.py")), \
                tempfile.TemporaryDirectory(prefix="retirement-compose-e2e-") as directory:
            root = Path(directory)
            evidence, source, authority = root / "evidence", root / "source", root / "authority"
            for path in (evidence, source, authority):
                path.mkdir(mode=0o700)
            record, spec, support_output, family = self._fixture(evidence, source)
            with tempfile.TemporaryDirectory(prefix="retirement-compose-trusted-adapter-") as adapter:
                executable, *_ = binding._compile_trusted_retirement_adapter(Path(adapter))
                spec += [f"adapter {executable}", f"authority {authority}"]
                composed = _run_compose(spec, root)
            self.assertEqual(composed["invocations"], 732)
            self.assertEqual(composed["members"], len(family["members"]))
            self.assertEqual(len(composed["authority"]["authority_sha256"]), 64)
            self._independent_replay(record, evidence, composed, family)
            path = tests.write_record(root, record)
            trusted = composed["receipt"]["sha256"]

            def validate():
                with mock.patch.object(binding, "_check_support_output", return_value=support_output), \
                        mock.patch.object(binding, "_population", return_value=record["population"]):
                    return binding.validate(path, evidence, trusted_execution_receipt_sha256=trusted)

            result = validate()
            self.assertEqual(result["proof"], "evidence-and-receipts-checked-without-independent-git")
            self.assertTrue(result["invocations_checked"])
            self.assertTrue(result["bundle_checked"])
            # The trust root is the composer's receipt, never a bundle field.
            with mock.patch.object(binding, "_check_support_output", return_value=support_output), \
                    mock.patch.object(binding, "_population", return_value=record["population"]), \
                    self.assertRaises(ValueError):
                binding.validate(path, evidence, trusted_execution_receipt_sha256="f" * 64)
            # A composed byte is content-bound by the seal: tamper and refuse.
            target = evidence / composed["series"]["path"]
            os.chmod(target, 0o600)
            target.write_bytes(target.read_bytes() + b"end\n")
            with self.assertRaises(ValueError):
                validate()


@unittest.skipIf(THROUGHPUT_DIRECTORY is None and len(sys.argv) < 3,
                 "needs the `bench_throughput self-test` output directory")
class ThroughputFixtureTests(unittest.TestCase):
    """Lane D's own C-encoded full-invocation fixture through the composer."""

    def test_throughput_fixture_streams(self):
        fixture = Path(THROUGHPUT_DIRECTORY)
        rows = []
        for index, row_id in enumerate((0, 6, 10)):
            row = copy.deepcopy(binding_tests.BindingTests._series_join_fixture()[0][0])
            row["row"] = row_id
            row["identity"]["fixture"] = f"tests/native-execution-{index}.c"
            row["identity"]["artifact_stage"] = "object" if row_id == 6 else "link"
            row["metrics"]["generated_runtime"] = row_id != 6
            rows.append(row)
        family = binding._derive_statistical_family(rows)
        counts = binding._family_member_counts(family)
        rules = binding_tests.BindingTests._rules()
        rules["sampling"].update(seed=1, rounds=2, pairs_per_round=60, warmups_per_variant=2,
                                 bootstrap_members_per_scope=counts["bootstrap_members_per_scope"],
                                 cell_members_per_scope=counts["cell_members_per_scope"])
        with tempfile.TemporaryDirectory(prefix="retirement-compose-throughput-") as directory:
            root = Path(directory)
            evidence, scratch = root / "evidence", root / "scratch"
            evidence.mkdir(mode=0o700)
            scratch.mkdir(mode=0o700)

            def put(name, value):
                data = value if isinstance(value, bytes) else _canonical(value)
                (evidence / name).write_bytes(data)
                return _descriptor(data, name)

            artifact = {"path": "placeholder", "bytes": 1, "sha256": "a" * 64}
            oracles = [{"row": row["row"], "code_section_status": "parsed-deterministic",
                        "code_section_bytes": 32, "code_section_sha256": "d" * 64,
                        "runtime_oracle_status": "passed-native" if row["metrics"]["generated_runtime"]
                        else "not-applicable",
                        "runtime_exit_code": 0 if row["metrics"]["generated_runtime"] else -1,
                        "native_runtime": row["metrics"]["generated_runtime"]} for row in rows]
            groups = binding._batch_groups(rows)
            contracts, group_contracts = [], []
            for row, oracle in zip(rows, oracles):
                runtime = row["metrics"]["generated_runtime"]
                side = {"compiler_command_sha256": "b" * 64, "artifact_sha256": "c" * 64,
                        "reproduction_sha256": None, "code_section_sha256": "d" * 64, "code_section_bytes": 32,
                        "runtime_command_sha256": "b" * 64 if runtime else None,
                        "runtime_output_sha256": "c" * 64 if runtime else None}
                contracts.append({"row": row["row"], "identity_sha256": binding._canonical_json_digest(row["identity"]),
                                  "oracle_sha256": binding._canonical_json_digest(oracle),
                                  "group": next(group["group"] for group in groups if row["row"] in group["rows"]),
                                  "baseline": side, "candidate": side})
            for group in groups:
                identity = group["identity"]
                object_group = group["kind"] == binding.OBJECT_BATCH_GROUP
                group_contracts.append({
                    "group": group["group"], "kind": group["kind"], "target": identity["target"],
                    "configuration": {field: identity[field] for field in ("allocator", "frontend_lowering", "PIC")},
                    "recipe": {field: identity[field] for field in ("fixture_recipe", "cpu", "cpu_features")},
                    "members": [{"row": row, "diagnostic_sha256": hashlib.sha256(b"").hexdigest()
                                 if object_group else None} for row in group["rows"]],
                    "controls": [],
                    "input_list_sha256": hashlib.sha256(binding._input_list_bytes(
                        ["tests/native-execution-1.c"], "list")).hexdigest() if object_group else None,
                    "metrics_bytes_max": 4096 + 16384 if object_group else None,
                    "baseline": {"command_sha256": "b" * 64, "exit_status": 0},
                    "candidate": {"command_sha256": "b" * 64, "exit_status": 0}})
            budget = (fixture / "retirement-campaign-budget.txt").read_text(encoding="ascii")
            plan = put("plan.json", {
                "schema": binding.EXECUTION_PLAN_SCHEMA, "version": 1, "schedule": binding.EXECUTION_SCHEDULE,
                "seed": 1, "rounds": 2, "pairs_per_round": 60, "warmups_per_variant": 2, "cpu": 2,
                "native_target": binding.NATIVE_TIMED_TARGET, "performance_rows_sha256": "a" * 64,
                "campaign_budget": {"record": budget,
                                    "sha256": hashlib.sha256(budget.encode("ascii")).hexdigest()},
                "rows": contracts, "groups": group_contracts, "untimed_groups": []})
            oracle_descriptor = put("oracle.json", {"records": oracles})
            post = put("post.json", {"post": 1})
            result_plan = put("result-plan.json", {"input": 1})
            record = {
                "subjects": {side: {"binary": artifact} for side in ("baseline", "candidate")},
                "support": {"root_sha256": "3" * 64,
                            "files": [{"name": role, **artifact} for role in binding.SUPPORT_FILE_ROLES]},
                "measurement": {"harness_binary": artifact}, "execution": {"service": "synthetic"},
                "rules": rules,
                "workflow": {"phases": {"pre_sample_plan": artifact, "post_aa_binding": post},
                             "records": {"admission": artifact, "oracle": oracle_descriptor}},
            }
            (root / "context.json").write_bytes(_context_template(record))
            events = binding._execution_trace_records(
                fixture, [{"path": "retirement-execution.jsonl",
                           "bytes": (fixture / "retirement-execution.jsonl").stat().st_size,
                           "sha256": hashlib.sha256((fixture / "retirement-execution.jsonl").read_bytes()).hexdigest(),
                           "records": 1220}], 1220)
            completed = max(event["finished_ns"] for event in events) + 1
            with tempfile.TemporaryDirectory(prefix="retirement-compose-trusted-adapter-") as adapter:
                executable, *_ = binding._compile_trusted_retirement_adapter(Path(adapter))
                composed = _run_compose([
                    f"source {fixture}", f"store {evidence}", f"scratch {scratch}", f"adapter {executable}",
                    f"context {root / 'context.json'}", "sealed retirement-sealed-result.json",
                    f"identity job-1 2 boot-123 1000 {completed}",
                    f"digests {plan['sha256']} {'a' * 64} {result_plan['sha256']} {family['sha256']} {post['sha256']}",
                    f"statistics 1 60 100000 {counts['bootstrap_members_per_scope']} "
                    f"{counts['cell_members_per_scope']}",
                    "population 11", "metrics-budget 4096 16384",
                    "group singleton 1", "group object 1", "group singleton 1",
                    _row_line(rows[0], 0, True), _row_line(rows[1], 1, False), _row_line(rows[2], 2, True),
                    *[f"code {row['row']} {'c' * 64} 32 {'d' * 64} {'c' * 64} {'c' * 64} 32 {'d' * 64} {'c' * 64}"
                      for row in rows],
                    "partition rows rows-0 retirement-rows-manifest-0.json 0 360",
                    "partition batches batches-0 retirement-batches-manifest-0.json 0 120",
                    "transcript retirement-execution.jsonl",
                    "samples rows retirement-samples-0000.jsonl",
                    "samples batches retirement-batches-0000.jsonl",
                    "metrics retirement-metrics-rec-0000.txt",
                    f"prior workflow.execution_plan {plan['path']} {plan['bytes']} {plan['sha256']}",
                    f"prior workflow.phases.post_aa_binding {post['path']} {post['bytes']} {post['sha256']}",
                    f"prior workflow.records.result_input_plan {result_plan['path']} {result_plan['bytes']} "
                    f"{result_plan['sha256']}",
                ], root)
            self.assertEqual(composed["invocations"], 1220)
            # The composed files are checked by the validator's own readers.
            self._check_composed(fixture, evidence, composed, record, rows, rules, plan, family)

    def _check_composed(self, fixture, evidence, composed, record, rows, rules, plan, family):
        receipt = json.loads((evidence / composed["receipt"]["path"]).read_bytes())
        self.assertEqual(receipt["context_sha256"], binding._canonical_json_digest(
            binding._execution_context(record, composed["raw_measurements_sha256"])))
        self.assertEqual(receipt["invocations"], 1220)
        with closing(sqlite3.connect(":memory:")) as db:
            identity_tests.InvocationEvidenceTests.create_sample_tables(db)
            digest = hashlib.sha256()
            row_ordinals = {row["row"]: index for index, row in enumerate(rows)}
            row_by_id = {row["row"]: row for row in rows}
            for kind, manifest in (("rows", "retirement-rows-manifest-0.json"),
                                   ("batches", "retirement-batches-manifest-0.json")):
                seen = [0]

                def consume(_identity, _ordinal, value, kind=kind, seen=seen):
                    if kind == "rows":
                        binding._consume_result_record(value, row_ordinals, row_by_id, 2, 60, 0, seen, digest, db)
                    else:
                        binding._consume_batch_record(value, {1: 0}, 2, 60, 0, seen, digest, db)
                verified = binding.RESULT_INPUT.verify(evidence.resolve(), manifest, record_consumer=consume)
                self.assertEqual(verified["records"], 360 if kind == "rows" else 120)
            self.assertEqual(digest.hexdigest(), composed["raw_measurements_sha256"])
            series = {key: composed["series"][key] for key in ("path", "bytes", "sha256")}
            binding._check_adapter_series(evidence, series, family, rows, rules, db)
            # The receipt's one shard is D's transcript byte for byte, and the
            # validator joins every invocation to the composed samples.
            shard = receipt["shards"][0]
            self.assertEqual(shard["sha256"], hashlib.sha256(
                (fixture / "retirement-execution.jsonl").read_bytes()).hexdigest())
            descriptor = {key: composed["receipt"][key] for key in ("path", "bytes", "sha256")}
            checked = binding._check_execution_transcript(
                evidence, descriptor, plan, record, rows, rules["sampling"], db,
                composed["raw_measurements_sha256"], descriptor["sha256"], 2, binding.NATIVE_TIMED_TARGET)
            self.assertEqual(checked["invocations"], 1220)


if __name__ == "__main__":
    if len(sys.argv) not in (2, 3):
        raise SystemExit("usage: retirement_compose_test.py COMPOSE_BINARY [THROUGHPUT_TEST_DIRECTORY]")
    COMPOSE_BINARY = Path(sys.argv[1]).resolve()
    THROUGHPUT_DIRECTORY = Path(sys.argv[2]).resolve() if len(sys.argv) == 3 else None
    unittest.main(argv=[sys.argv[0]], verbosity=2)
