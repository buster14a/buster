#!/usr/bin/env python3
"""End-to-end check of the #881-E C result composer against the #511 validator.

Usage: retirement_compose_test.py COMPOSE_BINARY [THROUGHPUT_TEST_DIRECTORY]

COMPOSE_BINARY is build/bench-service-tools/retirement-compose-tests, whose
`compose SPEC` mode plans a fresh result store (binding the retained-file
declaration), imports lane D's stream files and the retained A/A copies, runs
the composer (including the reviewed `bench_throughput retirement-replay`
adapter, executed from its hashed descriptor) and issues the producer
authority; `canonical FILE` and `context BINDING RAW` print the C canonical
JSON writer's bytes.

test_canonical_writer_matches_python compares the C writer with json.dumps
(sort_keys, compact, ensure_ascii=False) over generated documents, and the C
`_execution_context` derivation with the validator's own function.

test_composed_sealed_result_validates builds the bounded A1 binding fixture of
native_retirement_performance_binding_test (one native object batch group with
a frozen rejection control, one native link row with runtime, one cross-target
untimed code row) and its D-format streams: the invocation transcript, the
per-batch metrics shards and untimed batch records of
native_retirement_performance_identity_test, and #615 row/batch shards in D's
canonical record form. Everything after the post-A/A phase -- manifests, code
records, statistics input and output, execution receipt, result bundle and the
`workflow.phases.sealed_result` seal -- is produced by the C composer, whose
execution context is derived in C from the post-A/A binding document. The
test then adds the independent-replay phase and runs the unchanged validator
end to end; the out-of-band trust root is the receipt digest read from the
producer's authority file (never from the composer's own report), and the
authority's retained-manifest digest is checked against every retained file.
Mutations (a halved sample, a dropped retained A/A file, a changed binding,
tampered outputs) are refused by the composer or the validator.

test_throughput_fixture_streams (only with THROUGHPUT_TEST_DIRECTORY, the
output of `bench_throughput self-test`; CI runs it after that self-test)
composes lane D's own C-encoded full-invocation fixture -- whose samples D's
encoders derived from the same transcript and metrics -- and checks the
composed receipt, manifests and statistics input with the validator's
transcript, #615 and adapter-series readers; a halved sample is refused.
That fixture binds placeholder executable digests, so it cannot back a
complete binding; the first test covers the full validate() path.

WorkerUnitResultTests (only when the preparation runner of `bench_service
self-test` has exported its composed job-82 result beside COMPOSE_BINARY)
reads the worker-unit producer's own output (#881 PR 3): the #511 binding the
C writer derived (structurally valid, bound to the result root's documents,
mutations refused), the execution context over the bundle's raw digest, the
producer authority and context chain, the result manifest and bundle index.

Spec directives written here (one per line): source, store, scratch,
adapter PATH SHA256, authority, binding PATH SHA256, sealed, timeout NS,
identity JOB ATTEMPT BOOT BOUND COMPLETED, digests PLAN ROWS RESULT_PLAN
FAMILY POST_AA, statistics SEED PAIRS RESAMPLES BOOTSTRAP CELLS, population
ROWS, metrics-budget HEADER PER_INPUT, group KIND INPUTS, untimed-group KIND
INPUTS, row ID GROUP RUNTIME DIMS*6, code ROW (ARTIFACT BYTES CODE
REPRODUCTION)*2, partition rows|batches ID PATH START RECORDS,
transcript|metrics|untimed|untimed-metrics PATH [SOURCE], samples
rows|batches PATH [SOURCE], retain-file KIND PATH reserved|BYTES [SOURCE],
retain-declared KIND PATH reserved|BYTES (declared but never imported),
retain-group KIND PREFIX =SUFFIX FILES reserved|BYTES, retain-member PATH
[SOURCE], aa-transcript PATH, aa-metrics-tag TAG, prior NAME PATH BYTES
SHA256. The store root is the evidence root:
the prior closure and the binding live beside the store files.
No fixture here is service admission or performance evidence.
"""

from contextlib import closing
import copy
import hashlib
import io
import json
import os
from pathlib import Path
import random
import shutil
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
AUTHORITY_HEADER = "BQ-RETIREMENT-AUTHORITY-V3"
RETAINED_MANIFEST = "retirement-retained-manifest.txt"
RETAINED_HEADER = "BQ-RETIREMENT-RETAINED-V1"
BINDING_PATH = "binding-post-aa.json"


def _canonical(value):
    return (json.dumps(value, sort_keys=True, separators=(",", ":")) + "\n").encode("utf-8")


def _descriptor(data, path):
    return {"path": path, "bytes": len(data), "sha256": hashlib.sha256(data).hexdigest()}


def _records(prefix, values):
    return b"".join(_canonical({
        "record_id": f"{prefix}-{unit}/round-{round_number}/pair-{pair}",
        prefix: unit, "round": round_number, "pair": pair, "measurements": measurements,
    }) for (unit, round_number, pair), measurements in sorted(values.items()))


def _python_canonical(value):
    return json.dumps(value, sort_keys=True, separators=(",", ":"), ensure_ascii=False).encode("utf-8")


def _write_binding(evidence, record):
    """The post-A/A binding document, deliberately not in canonical form:
    the composer derives the canonical execution context from it in C."""
    data = json.dumps(record, indent=1, ensure_ascii=True).encode("ascii")
    (evidence / BINDING_PATH).write_bytes(data)
    return f"binding {BINDING_PATH} {hashlib.sha256(data).hexdigest()}"


def _adapter_line(executable):
    return f"adapter {executable} {hashlib.sha256(Path(executable).read_bytes()).hexdigest()}"


def _compose_process(spec_lines, directory, name="compose.spec"):
    spec = Path(directory) / name
    spec.write_text("\n".join(spec_lines) + "\n", encoding="utf-8")
    return subprocess.run([str(COMPOSE_BINARY), "compose", str(spec)], check=False,
                          capture_output=True, text=True)


def _run_compose(spec_lines, directory):
    process = _compose_process(spec_lines, directory)
    lines = [line for line in process.stdout.splitlines() if line.startswith("COMPOSE_RESULT ")]
    if process.returncode != 0 or len(lines) != 1:
        raise AssertionError(f"composer refused: rc={process.returncode} stderr={process.stderr}")
    return json.loads(lines[0][len("COMPOSE_RESULT "):])


def _refused_stage(spec_lines, directory, name):
    """The stage at which the composer refused (None if it composed)."""
    process = _compose_process(spec_lines, directory, name)
    marker = "COMPOSE_RESULT refused stage="
    stages = [line[len(marker):] for line in process.stderr.splitlines() if line.startswith(marker)]
    return stages[0] if process.returncode != 0 and len(stages) == 1 else None


def _with_store(spec_lines, store, scratch):
    """The same spec against a fresh copy of the pristine store and scratch."""
    return [f"store {store}" if line.startswith("store ") else
            f"scratch {scratch}" if line.startswith("scratch ") else line for line in spec_lines]


def _fresh(root, pristine, name):
    store, scratch = root / f"{name}-store", root / f"{name}-scratch"
    shutil.copytree(pristine, store)
    scratch.mkdir(mode=0o700)
    return store, scratch


def _aa_transcript(source, name, stage_tag, output):
    """An A/A transcript standing in for lane D's: the A/B transcript with its
    metrics artifacts renamed to the `aa` shards, which are byte copies of the
    A/B shards, so the composer's A/A tiling checks them like A/B's."""
    data = (source / name).read_bytes()
    renamed = data.replace(f"retirement-metrics-{stage_tag}-".encode(), b"retirement-metrics-aa-")
    if renamed == data:
        raise AssertionError("the transcript references no metrics shard")
    Path(output).write_bytes(renamed)
    return str(output)


def _retained_lines(transcript, rows, batches, metrics):
    """The A/A stage's retained evidence (its transcript tiles the `aa`
    metrics shards; copies of the A/B numeric shards stand in for its
    samples, which the composer never reads) and an empty failure-log
    group."""
    lines = ["aa-transcript retirement-execution-aa-0000.jsonl", "aa-metrics-tag aa",
             f"retain-file transcript retirement-execution-aa-0000.jsonl reserved {transcript}",
             f"retain-file samples retirement-samples-aa-0000.jsonl reserved {rows}",
             f"retain-file samples retirement-batches-aa-0000.jsonl reserved {batches}",
             "retain-group metrics retirement-metrics-aa- =.txt 8 reserved",
             "retain-group log retirement-failure- =.log 8 65536"]
    lines += [f"retain-member retirement-metrics-aa-{index:04d}.txt {source}"
              for index, source in enumerate(metrics)]
    return lines


def _halved_sample(source, name, output):
    """A copy of a D row shard (at `output`, relative to `source`) whose first
    candidate wall time is halved."""
    original = (source / name).read_bytes()
    lines = original.split(b"\n")
    record = json.loads(lines[0])
    wall = record["measurements"]["compiler_wall_time"]
    marker = b'"compiler_wall_time":{"baseline":' + json.dumps(wall["baseline"]).encode() + b',"candidate":'
    old = marker + json.dumps(wall["candidate"]).encode()
    new = marker + repr(wall["candidate"] / 2).encode()
    if old not in lines[0]:
        raise AssertionError("sample mutation did not apply")
    lines[0] = lines[0].replace(old, new, 1)
    (source / output).write_bytes(b"\n".join(lines))


def _trusted_authority(test, authority, evidence, composed):
    """The receipt trust root read from the producer's authority file, and
    the retained manifest it binds checked against every retained file."""
    record = composed["authority"]
    text = (authority / f"authority-{record['job']}-{record['attempt']}.txt").read_text(encoding="ascii")
    lines = text.split("\n")
    test.assertEqual(lines[0], AUTHORITY_HEADER)
    test.assertEqual(lines[1], record["job"])
    test.assertEqual(hashlib.sha256(text.encode("ascii")).hexdigest(), record["authority_sha256"])
    receipt, retained = lines[5], lines[7]
    manifest = (evidence / RETAINED_MANIFEST).read_bytes()
    test.assertEqual(hashlib.sha256(manifest).hexdigest(), retained)
    listed = manifest.decode("ascii").split("\n")
    test.assertEqual(listed[0], RETAINED_HEADER)
    paths = []
    for line in listed[1:-1]:
        _kind, digest, size, path = line.split(" ")
        data = (evidence / path).read_bytes()
        test.assertEqual((len(data), hashlib.sha256(data).hexdigest()), (int(size), digest))
        paths.append(path)
    test.assertEqual(paths, sorted(paths))
    test.assertIn("retirement-execution-aa-0000.jsonl", paths)
    return receipt


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
        binding_line = _write_binding(evidence, record)

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
            "sealed retirement-sealed-result.json", binding_line,
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
            *_retained_lines(_aa_transcript(source, "execution/invocations.jsonl", "ab",
                                            source / "retirement-execution-aa.jsonl"),
                             "retirement-samples-0000.jsonl",
                             "retirement-batches-0000.jsonl",
                             [path.name for path in sorted(source.glob("retirement-metrics-ab-*.txt"))]),
        ]
        (source / "scratch").mkdir(mode=0o700)
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
        # The production 64 MiB shard size gives this small family one series
        # shard; a 4 KiB fixture size (#1880) spans several, with the
        # validator's approved size patched to match.
        for shard_bytes in (None, 4096):
            with self.subTest(shard_bytes=shard_bytes):
                self._compose_and_validate(shard_bytes)

    def _compose_and_validate(self, shard_bytes):
        tests = binding_tests.BindingTests("test_complete_record_structural_contract_is_accepted")
        size = mock.patch.object(binding, "ADAPTER_SERIES_SHARD_BYTES",
                                 shard_bytes or binding.ADAPTER_SERIES_SHARD_BYTES)
        with tests._adapter_checkout() as repository, size, \
                mock.patch.object(binding, "__file__",
                                  str(repository / "tools" / "native_retirement_performance_binding.py")), \
                tempfile.TemporaryDirectory(prefix="retirement-compose-e2e-") as directory:
            root = Path(directory)
            evidence, source, authority = root / "evidence", root / "source", root / "authority"
            for path in (evidence, source, authority):
                path.mkdir(mode=0o700)
            record, spec, support_output, family = self._fixture(evidence, source)
            if shard_bytes:
                spec.append(f"series-shard-bytes {shard_bytes}")
            pristine = root / "pristine"
            shutil.copytree(evidence, pristine)
            with tempfile.TemporaryDirectory(prefix="retirement-compose-trusted-adapter-") as adapter:
                executable, *_ = binding._compile_trusted_retirement_adapter(Path(adapter))
                spec += [_adapter_line(executable), f"authority {authority}"]
                composed = _run_compose(spec, root)
                if not shard_bytes:
                    self._mutations(root, pristine, source, spec)
            self.assertEqual(composed["invocations"], 732)
            self.assertEqual(composed["members"], len(family["members"]))
            self.assertEqual(composed["retained_files"], 3 + len(list(source.glob("retirement-metrics-ab-*.txt"))))
            self.assertEqual((composed["untimed_records"], composed["untimed_production"]), (4, 2))
            # (#1880) The adapter input is the series manifest over canonical
            # shards; their concatenation is the one series stream.
            manifest = binding._adapter_series_manifest(
                evidence, {key: composed["series"][key] for key in ("path", "bytes", "sha256")})
            self.assertEqual(len(manifest["shards"]), composed["series_shards"])
            self.assertEqual(composed["series_shards"] > 1, bool(shard_bytes))
            parts = [(evidence / shard["path"]).read_bytes() for shard in manifest["shards"]]
            self.assertEqual(binding.adapter_series_shards(b"".join(parts), shard_bytes), parts)
            self.assertTrue(parts[0].startswith(b"version=1 seed="))
            # The composed context is the validator's own _execution_context.
            receipt_value = json.loads((evidence / composed["receipt"]["path"]).read_bytes())
            self.assertEqual(receipt_value["context_sha256"], binding._canonical_json_digest(
                binding._execution_context(record, composed["raw_measurements_sha256"])))
            trusted = _trusted_authority(self, authority, evidence, composed)
            self.assertEqual(trusted, composed["receipt"]["sha256"])
            self._independent_replay(record, evidence, composed, family)
            path = tests.write_record(root, record)

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
            # Composed bytes are content-bound: tamper with the receipt (the
            # authority's trust root no longer matches), the series manifest
            # or its last shard (the seal no longer matches) and the
            # validator refuses.
            targets = {"receipt": evidence / composed["receipt"]["path"],
                       "series": evidence / composed["series"]["path"],
                       "shard": evidence / manifest["shards"][-1]["path"]}
            for name, target in targets.items():
                original = target.read_bytes()
                os.chmod(target, 0o600)
                target.write_bytes(original.replace(b"}", b"} ", 1) if name == "receipt" else original + b"end\n")
                with self.subTest(tampered=name), self.assertRaises(ValueError):
                    validate()
                target.write_bytes(original)
            validate()

    def _mutations(self, root, pristine, source, spec):
        """The composer refuses before sealing anything: a halved sample (the
        transcript join), a dropped retained A/A file (the declaration bound
        before timing) and a changed binding digest (the derived context)."""
        _halved_sample(source, "retirement-samples-0000.jsonl", "retirement-samples-halved.jsonl")
        store, scratch = _fresh(root, pristine, "halved")
        halved = [line.replace("samples rows retirement-samples-0000.jsonl",
                               "samples rows retirement-samples-0000.jsonl retirement-samples-halved.jsonl")
                  for line in _with_store(spec, store, scratch)]
        self.assertEqual(_refused_stage(halved, root, "halved.spec"), "samples")
        self.assertFalse((store / "retirement-sealed-result.json").exists())
        store, scratch = _fresh(root, pristine, "dropped")
        # The declaration still names the A/A transcript copy at plan time,
        # but it is never published: composing without it is refused.
        dropped = ["retain-declared transcript retirement-execution-aa-0000.jsonl reserved"
                   if line.startswith("retain-file transcript ") else line
                   for line in _with_store(spec, store, scratch)]
        self.assertEqual(_refused_stage(dropped, root, "dropped.spec"), "inventory")
        store, scratch = _fresh(root, pristine, "binding")
        changed = [f"binding {BINDING_PATH} {'0' * 64}" if line.startswith("binding ") else line
                   for line in _with_store(spec, store, scratch)]
        self.assertEqual(_refused_stage(changed, root, "binding.spec"), "context")

    def test_canonical_writer_matches_python(self):
        """The C writer is json.dumps(sort_keys, compact, ensure_ascii=False)."""
        generator = random.Random(881)
        alphabet = ["a", "Z", "0", " ", "\"", "\\", "/", "\n", "\t", "\x01", "\x1f", "\x7f", "\u00e9",
                    "\u2028", "\U0001f600", "\u4e2d"]

        def text():
            return "".join(generator.choice(alphabet) for _ in range(generator.randrange(0, 8)))

        def number():
            choice = generator.randrange(6)
            if choice == 0:
                return generator.randrange(-10 ** 30, 10 ** 30)
            if choice == 1:
                return generator.random() * 10 ** generator.randrange(-30, 30)
            if choice == 2:
                return -generator.random() * 10.0 ** generator.randrange(-300, 300)
            if choice == 3:
                return float(generator.randrange(0, 10 ** 17))
            if choice == 4:
                return generator.choice([0.0, -0.0, 1e16, 1e-5, 0.0001, 5e-324, 1.7976931348623157e308])
            return generator.randrange(-5, 5)

        def value(depth):
            choice = generator.randrange(8 if depth < 4 else 5)
            if choice == 0:
                return text()
            if choice in (1, 3, 4):
                return number()
            if choice == 2:
                return generator.choice([True, False, None])
            if choice in (5, 6):
                return {text(): value(depth + 1) for _ in range(generator.randrange(0, 5))}
            return [value(depth + 1) for _ in range(generator.randrange(0, 5))]

        with tempfile.TemporaryDirectory(prefix="retirement-compose-canonical-") as directory:
            path = Path(directory) / "document.json"
            for index in range(200):
                document = {text(): value(0) for _ in range(generator.randrange(1, 6))}
                encoded = json.dumps(document, indent=generator.choice([None, 1]),
                                     ensure_ascii=bool(index % 2)).encode("utf-8")
                path.write_bytes(encoded)
                process = subprocess.run([str(COMPOSE_BINARY), "canonical", str(path)], check=False,
                                         capture_output=True)
                self.assertEqual(process.returncode, 0, encoded)
                self.assertEqual(process.stdout, _python_canonical(json.loads(encoded)), encoded)
            # The execution context of a binding with every kind of value.
            record = {"workflow": {"phases": {"pre_sample_plan": {"sha256": "1" * 64},
                                              "post_aa_binding": {"sha256": "2" * 64}},
                                   "records": {"admission": {"sha256": "3" * 64}, "oracle": {"sha256": "4" * 64}}},
                      "support": {"root_sha256": "5" * 64, "files": [1, 2.5]},
                      "subjects": {"baseline": {"binary": value(1)}, "candidate": {"b\u00e9": [value(1), -0.0]}},
                      "measurement": {"x": 1e21, "y": [0.1, 1e-7, "tab\t"]}, "execution": {"z": {"w": None}}}
            path.write_bytes(json.dumps(record, indent=2).encode("ascii"))
            raw = "6" * 64
            process = subprocess.run([str(COMPOSE_BINARY), "context", str(path), raw], check=False,
                                     capture_output=True)
            self.assertEqual(process.returncode, 0)
            self.assertEqual(hashlib.sha256(process.stdout).hexdigest(),
                             binding._canonical_json_digest(binding._execution_context(record, raw)))
            # Duplicate keys (which the validator refuses) are refused.
            path.write_bytes(b'{"a":1,"a":2}')
            process = subprocess.run([str(COMPOSE_BINARY), "canonical", str(path)], check=False,
                                     capture_output=True)
            self.assertNotEqual(process.returncode, 0)


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
            binding_line = _write_binding(evidence, record)
            events = binding._execution_trace_records(
                fixture, [{"path": "retirement-execution.jsonl",
                           "bytes": (fixture / "retirement-execution.jsonl").stat().st_size,
                           "sha256": hashlib.sha256((fixture / "retirement-execution.jsonl").read_bytes()).hexdigest(),
                           "records": 1220}], 1220)
            completed = max(event["finished_ns"] for event in events) + 1
            authority = root / "authority"
            authority.mkdir(mode=0o700)
            pristine = root / "pristine"
            shutil.copytree(evidence, pristine)
            with tempfile.TemporaryDirectory(prefix="retirement-compose-trusted-adapter-") as adapter:
                executable, *_ = binding._compile_trusted_retirement_adapter(Path(adapter))
                spec = [
                    f"source {fixture}", f"store {evidence}", f"scratch {scratch}", _adapter_line(executable),
                    f"authority {authority}", binding_line, "sealed retirement-sealed-result.json",
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
                    *_retained_lines(_aa_transcript(fixture, "retirement-execution.jsonl", "rec",
                                                    root / "retirement-execution-aa.jsonl"),
                                     "retirement-samples-0000.jsonl",
                                     "retirement-batches-0000.jsonl", ["retirement-metrics-rec-0000.txt"]),
                ]
                composed = _run_compose(spec, root)
                # D's own samples, with one candidate wall time halved, no
                # longer equal their transcript observation. The copy lives
                # beside the fixture's source directory under a private name.
                halved_source = root / "halved-source"
                halved_source.mkdir(mode=0o700)
                for name in ("retirement-execution.jsonl", "retirement-batches-0000.jsonl",
                             "retirement-metrics-rec-0000.txt"):
                    shutil.copyfile(fixture / name, halved_source / name)
                shutil.copyfile(fixture / "retirement-samples-0000.jsonl",
                                halved_source / "retirement-samples-original.jsonl")
                _halved_sample(halved_source, "retirement-samples-original.jsonl", "retirement-samples-0000.jsonl")
                store, fresh_scratch = _fresh(root, pristine, "halved")
                halved = [f"source {halved_source}" if line.startswith("source ") else line
                          for line in _with_store(spec, store, fresh_scratch)]
                self.assertEqual(_refused_stage(halved, root, "halved.spec"), "samples")
            self.assertEqual(composed["invocations"], 1220)
            trusted = _trusted_authority(self, authority, evidence, composed)
            # The composed files are checked by the validator's own readers.
            self._check_composed(fixture, evidence, composed, record, rows, rules, plan, family, trusted)

    def _check_composed(self, fixture, evidence, composed, record, rows, rules, plan, family, trusted):
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
                composed["raw_measurements_sha256"], trusted, 2, binding.NATIVE_TIMED_TARGET)
            self.assertEqual(checked["invocations"], 1220)


# The composed job-82 result the preparation runner exports beside itself
# (retirement_worker_unit_tests.h, bq_prep_worker_unit_export): `result/`
# (the result root), `authority/` (the producer authority and context chain)
# and `census/` (the installed census the binding's support files name).
WORKER_UNIT_RESULT = "retirement-worker-unit-result"
WORKER_UNIT_BINDING = "retirement-binding.json"
WORKER_UNIT_MANIFEST = "native-retirement-performance-v1.manifest"
WORKER_UNIT_BUNDLE = "native-retirement-performance-v1.bundle"
WORKER_UNIT_CHAIN_HEADER = "BQ-RETIREMENT-CONTEXT-CHAIN-V2"
WORKER_UNIT_PENDING = {"bytes": 1, "sha256": "0" * 64}
# The preparation fixture's installed census files under the binding's
# `census/` support paths (retirement_binding_context_fixture.py).
WORKER_UNIT_CENSUS_ROLES = ("support_declaration", "manifest", "inputs", "rows", "performance_rows",
                            "validator_report")


def _sha256_file(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def _keyed(text):
    return dict(line.split("=", 1) for line in text.split("\n") if "=" in line)


class WorkerUnitResultTests(unittest.TestCase):
    """The worker-unit producer's composed result (#881 PR 3).

    The preparation runner (`bench_service self-test`) drives job 82 through
    the real coordinator path to MEASURED and exports its result root beside
    itself; `build/bench-service-tools/` is also COMPOSE_BINARY's directory.
    These checks read that output with the validator's own readers: the #511
    binding the C writer derived from the pinned context (structurally valid,
    its pre-campaign documents and admission receipt bound, its two late
    phases pending), the execution context over the result bundle's raw
    digest, the producer authority and its context chain, the result manifest
    and BQ-BUNDLE-V1 index. Mutated bindings are refused by the validator.
    Absent that output (CI runs this file without the preparation runner),
    the class is skipped.
    """

    @classmethod
    def setUpClass(cls):
        cls.export = Path(COMPOSE_BINARY).parent / WORKER_UNIT_RESULT
        if not (cls.export / "result" / WORKER_UNIT_BINDING).is_file():
            raise unittest.SkipTest(f"run `bench_service self-test` first: {cls.export} is missing")
        cls.result = cls.export / "result"
        cls.authorities = sorted((cls.export / "authority").glob("authority-*.txt"))
        cls.chains = sorted((cls.export / "authority").glob("context-chain-*.txt"))

    def record(self):
        return json.loads((self.result / WORKER_UNIT_BINDING).read_bytes())

    def file_descriptor(self, name):
        return _descriptor((self.result / name).read_bytes(), name)

    def validate(self, record, name="binding.json"):
        with tempfile.TemporaryDirectory(prefix="retirement-worker-unit-binding-") as directory:
            path = Path(directory) / name
            path.write_bytes(_python_canonical(record))
            return binding.validate(path)

    def test_binding_is_structurally_valid_and_bound(self):
        data = (self.result / WORKER_UNIT_BINDING).read_bytes()
        record = json.loads(data)
        # The C writer's bytes are the validator's canonical JSON.
        self.assertEqual(data, _python_canonical(record))
        checked = binding.validate(self.result / WORKER_UNIT_BINDING)
        self.assertEqual(checked["proof"], "structural-only")
        self.assertGreater(checked["required_rows"], 0)
        workflow = record["workflow"]
        # Lane D's pre-sample plan, post-A/A binding, oracle and result-input
        # plan, and the admission receipt, all as the result root holds them.
        for descriptor, name in ((workflow["phases"]["pre_sample_plan"], "retirement-pre-sample-plan.json"),
                                 (workflow["phases"]["post_aa_binding"], "retirement-post-aa-binding.json"),
                                 (workflow["records"]["oracle"], "retirement-oracle.json"),
                                 (workflow["records"]["result_input_plan"], "retirement-result-input-plan.json"),
                                 (record["execution"]["host"]["aa_admission_receipt"],
                                  "retirement-aa-admission.json")):
            self.assertEqual(descriptor, self.file_descriptor(name))
        # The sealed result binds this record's digest, so the late phases
        # are lane F's to fill.
        for phase in ("sealed_result", "independent_replay"):
            self.assertEqual({key: workflow["phases"][phase][key] for key in ("bytes", "sha256")},
                             WORKER_UNIT_PENDING)
        # The support files are the installed census's bytes.
        files = record["support"]["files"]
        for role in WORKER_UNIT_CENSUS_ROLES:
            artifact = files[binding.SUPPORT_FILE_ROLES.index(role)]
            self.assertTrue(artifact["path"].startswith("census/"))
            binding._check_evidence(self.export, artifact, f"support.files.{role}")
        # The subjects are the sealed result's pinned binaries.
        sealed = json.loads((self.result / "retirement-sealed-result.json").read_bytes())
        self.assertEqual(sealed["post_aa_binding_sha256"], workflow["phases"]["post_aa_binding"]["sha256"])

    def test_mutated_binding_is_refused(self):
        record = self.record()
        self.validate(record)
        mutations = {
            "decision": lambda value: value.__setitem__("decision_id", "other-decision"),
            "support-root": lambda value: value["support"].__setitem__("root_sha256", "f" * 64),
            "closure-manifest": lambda value: value["requested_work"]["closure"].__setitem__(
                "manifest_sha256", "e" * 64),
            "same-subjects": lambda value: value["subjects"]["candidate"].__setitem__(
                "source_commit", value["subjects"]["baseline"]["source_commit"]),
            "sampling-family": lambda value: value["rules"]["sampling"].__setitem__(
                "cell_members_per_scope", value["rules"]["sampling"]["cell_members_per_scope"] + 1),
        }
        for name, mutate in mutations.items():
            mutated = copy.deepcopy(record)
            mutate(mutated)
            with self.subTest(mutation=name), self.assertRaises(ValueError):
                self.validate(mutated)

    def test_context_is_the_validators_over_the_result_raw_digest(self):
        record = self.record()
        bundle = json.loads((self.result / "retirement-result-bundle.json").read_bytes())
        raw = bundle["raw_measurements_sha256"]
        context = binding._canonical_json_digest(binding._execution_context(record, raw))
        receipt = json.loads((self.result / "retirement-execution-receipt.json").read_bytes())
        self.assertEqual(receipt["context_sha256"], context)
        chain = _keyed(self.chains[0].read_text(encoding="ascii"))
        self.assertEqual(chain["context"], context)
        # Lane D's A/B stage raw digest is the composer's.
        self.assertEqual(chain["stage-1"].split(" ")[3], raw)
        # A different binding derives a different context.
        record["measurement"]["harness_source_commit"] = "9" * 40
        self.assertNotEqual(binding._canonical_json_digest(binding._execution_context(record, raw)), context)

    def test_authority_and_context_chain(self):
        self.assertEqual((len(self.authorities), len(self.chains)), (1, 1))
        text = self.authorities[0].read_text(encoding="ascii")
        lines = text.split("\n")
        receipt = json.loads((self.result / "retirement-execution-receipt.json").read_bytes())
        self.assertEqual(lines[0], AUTHORITY_HEADER)
        self.assertEqual((lines[1], lines[2]), (receipt["job_id"], str(receipt["attempt"])))
        # The plan is lane D's execution-plan document; the receipt is the
        # composer's, byte for byte.
        self.assertEqual(lines[3], receipt["execution_plan_sha256"])
        self.assertEqual(lines[3], _sha256_file(self.result / "retirement-execution-plan.json"))
        self.assertEqual(lines[4], receipt["context_sha256"])
        self.assertEqual(lines[5], _sha256_file(self.result / "retirement-execution-receipt.json"))
        # Line 6 is the store identity; line 7 the retained manifest.
        manifest = (self.result / RETAINED_MANIFEST).read_bytes()
        self.assertEqual(lines[7], hashlib.sha256(manifest).hexdigest())
        listed = manifest.decode("ascii").split("\n")
        self.assertEqual(listed[0], RETAINED_HEADER)
        for line in listed[1:-1]:
            _kind, digest, size, path = line.split(" ")
            self.assertEqual(self.file_descriptor(path), {"path": path, "bytes": int(size), "sha256": digest})
        self.assertIn("unit-campaign-post-sample.txt", manifest.decode("ascii"))
        chain_text = self.chains[0].read_text(encoding="ascii")
        self.assertEqual(chain_text.split("\n")[0], WORKER_UNIT_CHAIN_HEADER)
        chain = _keyed(chain_text)
        self.assertEqual((chain["job"], chain["attempt"]), (lines[1], lines[2]))
        self.assertEqual(chain["plan"], lines[3])
        self.assertEqual(chain["binding"], _sha256_file(self.result / WORKER_UNIT_BINDING))
        self.assertEqual(chain["bound-at"], str(receipt["bound_at_ns"]))
        # The READY digest the coordinator kept is the one the chain carries.
        ready = _keyed((self.result / "worker-phase-5").read_text(encoding="ascii"))
        self.assertEqual(chain["ready"], ready["digest-sha256"])
        # The unit record retains the pre- and post-sample contexts.
        unit = _keyed((self.result / "unit-campaign-post-sample.txt").read_text(encoding="ascii"))
        self.assertEqual((unit["pre-sample"], unit["post-sample"]), (chain["pre-sample"], chain["post-sample"]))
        self.assertEqual(unit["execution-plan"], chain["plan"])

    def test_result_manifest_and_bundle(self):
        manifest = _keyed((self.result / WORKER_UNIT_MANIFEST).read_text(encoding="ascii"))
        self.assertEqual((manifest["recipe"], manifest["status"], manifest["stage"]),
                         ("native-retirement-performance-v1", "succeeded", "retirement"))
        self.assertEqual(manifest["authority-sha256"], _sha256_file(self.authorities[0]))
        self.assertEqual(manifest["receipt-sha256"], _sha256_file(self.result / "retirement-execution-receipt.json"))
        self.assertEqual(manifest["sealed-result"], "retirement-sealed-result.json")
        self.assertEqual(manifest["sealed-result-sha256"],
                         _sha256_file(self.result / "retirement-sealed-result.json"))
        self.assertEqual(manifest["binding"], WORKER_UNIT_BINDING)
        self.assertEqual(manifest["binding-sha256"], _sha256_file(self.result / WORKER_UNIT_BINDING))
        self.assertEqual(manifest["bundle-sha256"], _sha256_file(self.result / WORKER_UNIT_BUNDLE))
        chain = _keyed(self.chains[0].read_text(encoding="ascii"))
        self.assertEqual(manifest["ready-sha256"], chain["ready"])
        lines = (self.result / WORKER_UNIT_BUNDLE).read_text(encoding="ascii").split("\n")
        self.assertEqual(lines[0], "BQ-BUNDLE-V1")
        entries = [line.split(" ", 2) for line in lines[3:] if line]
        names = sorted(path.name for path in self.result.iterdir()
                       if path.name not in (WORKER_UNIT_MANIFEST, WORKER_UNIT_BUNDLE))
        self.assertEqual([entry[2] for entry in entries], names)
        self.assertEqual(lines[1], f"entries={len(entries)}")
        total = 0
        for digest, size, name in entries:
            self.assertEqual(self.file_descriptor(name), {"path": name, "bytes": int(size), "sha256": digest})
            total += int(size)
        self.assertEqual(lines[2], f"bytes={total}")

    def test_result_streams_through_the_validator_readers(self):
        bundle = json.loads((self.result / "retirement-result-bundle.json").read_bytes())
        series = binding._adapter_series_manifest(self.result, bundle["adapter_input"])
        self.assertTrue(series["shards"])
        receipt = json.loads((self.result / "retirement-execution-receipt.json").read_bytes())
        records = binding._execution_trace_records(self.result, receipt["shards"], receipt["invocations"])
        try:
            self.assertEqual(sum(1 for _ in records), receipt["invocations"])
        finally:
            records.close()


if __name__ == "__main__":
    if len(sys.argv) not in (2, 3):
        raise SystemExit("usage: retirement_compose_test.py COMPOSE_BINARY [THROUGHPUT_TEST_DIRECTORY]")
    COMPOSE_BINARY = Path(sys.argv[1]).resolve()
    THROUGHPUT_DIRECTORY = Path(sys.argv[2]).resolve() if len(sys.argv) == 3 else None
    unittest.main(argv=[sys.argv[0]], verbosity=2)
