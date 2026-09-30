#!/usr/bin/env python3
"""Emit a test binding context and its evidence for the #881 worker-unit fixture.

Usage: retirement_binding_context_fixture.py CENSUS OUTPUT EVIDENCE
       BASELINE_BINARY CANDIDATE_BINARY BASELINE_SHA256 CANDIDATE_SHA256
       CONTRACT_SHA256 SEED PAIRS RESAMPLES CPU

The worker-unit producer writes the #511 binding document the composer reads
(tools/bench_service/retirement_worker_compose.c) from a pinned, reviewed
binding context: the header ``BQ-RETIREMENT-BINDING-CONTEXT-V1`` and one
``<section>=<canonical JSON>`` line per pre-campaign section of the binding
record (contract, execution, measurement, population, producer, provenance,
requested_work, rules, subjects, support) plus the workflow's admission record
descriptor. It then publishes every file the context names into the result
root (bq_retirement_worker_evidence_publish): the subjects' binaries from its
held descriptors, everything else from the installed evidence directory
(``native-retirement-performance-v1.evidence`` under recipes/), each at the
flat result-root name the context gives it (``retirement-evidence-*``) and
only at the size and digest the context binds.

This emits such a context for the preparation fixture and writes the
evidence directory EVIDENCE (which must not exist yet): the structurally
complete record of native_retirement_performance_binding_test, with

* the fixture census's own support files (CENSUS is the installed census
  directory) and a performance declaration, statistical family, axes and
  admission records derived from its performance rows;
* the fixture's frozen subject binaries (BASELINE_BINARY and CANDIDATE_BINARY,
  whose digests must be BASELINE_SHA256 and CANDIDATE_SHA256), their source
  snapshots and build receipts;
* the service, host-profile, qualification and lease receipts under the
  identities of the fixture's A/A admission receipt stand-in
  (bq_retirement_worker_campaign_fixture_receipt: fixture-service,
  fixture-machine, fixture-profile, the baseline commit/tree 0...1/0...2 and
  the campaign CPU);
* the provenance receipts binding those subjects, the producer toolchain and
  the harness, whose source identity is this checkout's HEAD (the validator
  compiles the #619 adapter from that commit);
* the repository's contract source (CONTRACT_SHA256 must be its digest) and
  statistics implementation, and the frozen campaign values;
* the admission-receipt sentinel the producer replaces.

The subject commits, provenance receipts, host identities and other
descriptors are test data, not evidence of any service, host or build.
Nothing here is a reviewed production context.
"""

import copy
import hashlib
import json
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
import native_retirement_performance_binding_test as binding_tests  # noqa: E402

binding = binding_tests.binding
HEADER = "BQ-RETIREMENT-BINDING-CONTEXT-V1"
SECTIONS = ("contract", "execution", "measurement", "population", "producer", "provenance",
            "requested_work", "rules", "subjects", "support")
ADMISSION_PATH = "retirement-aa-admission.json"
PENDING = "0" * 64
# Every evidence file's flat result-root name starts with this
# (BQ_RETIREMENT_WORKER_EVIDENCE_PREFIX).
PREFIX = "retirement-evidence-"
CONTRACT_PATH = ROOT / "docs" / "native-retirement-performance-contract.md"
STATISTICS_PATH = ROOT / "tools" / "throughput" / "retirement_stats.h"
# The installed census file of each pinned #508 support role.
CENSUS_FILES = {
    "support_declaration": "support.tsv",
    "manifest": "manifest.txt",
    "inputs": "inputs.tsv",
    "rows": "rows.tsv",
    "performance_rows": "performance-rows.json",
    "validator_report": "validator-report.json",
}
# The identities of the fixture's A/A admission receipt stand-in.
SERVICE_ID = "fixture-service"
SERVICE_VERSION = "fixture-service-v1"
MACHINE_ID = "fixture-machine"
PROFILE_ID = "fixture-profile"
PROFILE_VERSION = "fixture-profile-v1"
BASELINE_COMMIT = "0" * 39 + "1"
BASELINE_TREE = "0" * 39 + "2"
CANDIDATE_COMMIT = "5" * 40
CANDIDATE_TREE = "6" * 40


def canonical(value):
    return json.dumps(value, sort_keys=True, separators=(",", ":"), ensure_ascii=False)


def descriptor(path, data):
    return {"path": path, "bytes": len(data), "sha256": hashlib.sha256(data).hexdigest()}


def git(*arguments):
    return subprocess.run(["git", *arguments], cwd=ROOT, check=True, capture_output=True,
                          text=True).stdout.strip()


class Evidence:
    """The installed evidence files, by flat result-root name."""

    def __init__(self):
        self.files = {}

    def put(self, name, data, installed=True):
        if isinstance(data, str):
            data = data.encode("utf-8")
        path = PREFIX + name
        if path in self.files:
            raise ValueError(f"evidence name {path} is repeated")
        if installed:
            self.files[path] = data
        return descriptor(path, data)

    def json(self, name, value):
        return self.put(name, canonical(value) + "\n")


def admission_records(census_rows, parsed, manifest_sha256, rows_sha256):
    """One admission record per performance row, derived from the census:
    compiler-eligible rows as completed native compilations (synthetic
    artifact facts), the others with explicit null observations."""
    fields = [field for field in binding.ROW_IDENTITY_FIELDS if field != "artifact_stage"]
    by_identity = {tuple(row[field] for field in fields): index for index, row in enumerate(census_rows)}
    records = []
    for row in parsed:
        identity = row["identity"]
        stage = identity["artifact_stage"]
        eligible = row["metrics"]["compiler_wall_time"]
        records.append({
            "row": row["row"], "census_row": by_identity[tuple(identity[field] for field in fields)],
            "identity": identity, "artifact_stage": stage,
            "requested_obligation": "compiler-wall-time-and-peak-rss" if eligible else None,
            "status": "completed" if eligible else "not-applicable",
            "exit_code": 0 if eligible else None, "timed_out": False, "native_compiler": eligible,
            "artifact_kind": ({"object": "object", "link": "linked-executable"}.get(stage, stage)
                              if eligible else None),
            "artifact_bytes": 1 if eligible else None,
            "artifact_sha256": "a" * 64 if eligible else None,
        })
    return {"schema": binding.ADMISSION_SCHEMA, "version": 1, "source_manifest_sha256": manifest_sha256,
            "source_rows_sha256": rows_sha256, "records": records}


def context(census, binaries, digests, contract, seed, pairs, resamples, cpu):
    """The context bytes and the installed evidence files."""
    record, contents = binding_tests.BindingTests._build_record()
    record = copy.deepcopy(record)
    census = Path(census)
    evidence = Evidence()

    # Support: the census files, the record's dependency and environment
    # files, and a performance declaration over the census rows.
    rows_data = (census / CENSUS_FILES["performance_rows"]).read_bytes()
    parsed, axes, family = binding._performance_rows(rows_data)
    support = record["support"]
    files = {}
    for item in support["files"]:
        role = item["name"]
        if role in CENSUS_FILES:
            data = (census / CENSUS_FILES[role]).read_bytes()
        elif role != "performance_declaration":
            data = contents[item["path"]]
        else:
            continue
        files[role] = evidence.put("census-" + role.replace("_", "-") + Path(item["path"]).suffix, data)
    declaration = {
        "schema": binding.PERFORMANCE_DECLARATION_SCHEMA,
        "version": binding.PERFORMANCE_DECLARATION_VERSION,
        **{f"{role}_sha256": files[role]["sha256"] for role in files},
        "object_row_count": sum(row["identity"]["artifact_stage"] == "object" for row in parsed),
        "required_row_count": len(parsed), "axes": axes,
        "row_identity_fields": list(binding.ROW_IDENTITY_FIELDS), "statistical_family": family,
        "runtime_eligibility": "independent-native-executable-oracle-only",
        "code_section_eligibility": "deterministic-code-section-payload-only",
    }
    files["performance_declaration"] = evidence.json("performance-declaration.json", declaration)
    for item in support["files"]:
        item.update(files[item["name"]])
    validator = support["validator"]
    validator["source"] = evidence.put("native_retirement_contract.py", contents[validator["source"]["path"]])
    items = record["requested_work"]["items"]
    for item in items:
        artifact = evidence.put(f"closure-{item['kind']}", contents[item["artifact"]["path"]])
        item["artifact"] = artifact
        support["closure"][item["kind"]]["artifact"] = artifact
        record["requested_work"]["closure"][item["kind"]].update(
            {"bytes": artifact["bytes"], "sha256": artifact["sha256"]})
    support["root_sha256"] = binding._support_root_digest(support["files"], validator, support["closure"])
    record["requested_work"]["root_sha256"] = binding._canonical_work_digest(items)
    record["requested_work"]["closure"]["manifest_sha256"] = files["manifest"]["sha256"]
    record["population"].update({
        "required_row_count": len(parsed),
        "required_rows_sha256": files["performance_rows"]["sha256"],
        "axes": axes, "statistical_family": family,
        "source_digests": binding._artifact_digest_map(support),
    })

    # Contract and measurement.
    contract_data = CONTRACT_PATH.read_bytes()
    if hashlib.sha256(contract_data).hexdigest() != contract:
        raise ValueError("the profile's contract pin is not the repository contract's digest")
    record["contract"]["source"] = evidence.put("contract.md", contract_data)
    measurement = record["measurement"]
    measurement.update({
        "harness_source_commit": git("rev-parse", "HEAD"),
        "harness_source_tree": git("rev-parse", "HEAD^{tree}"),
        "harness_binary": evidence.put("harness", contents[measurement["harness_binary"]["path"]]),
        "statistics_implementation": evidence.put("retirement_stats.h", STATISTICS_PATH.read_bytes()),
    })

    # Producer toolchain.
    producer = record["producer"]
    for group, name, leaf in (("toolchain", "compiler_binary", "producer-clang"),
                              ("toolchain", "resource_directory", "producer-resource.tar"),
                              ("build", "configuration", "producer-build-config.json"),
                              ("build", "flags", "producer-build-flags.txt")):
        producer[group][name] = evidence.put(leaf, contents[producer[group][name]["path"]])
    toolchain = {
        "compiler_binary_sha256": producer["toolchain"]["compiler_binary"]["sha256"],
        "resource_directory_sha256": producer["toolchain"]["resource_directory"]["sha256"],
        "configuration_sha256": producer["build"]["configuration"]["sha256"],
        "flags_sha256": producer["build"]["flags"]["sha256"],
    }

    # Subjects: the held binaries (published from the producer's descriptors,
    # not installed), their source snapshots and build receipts.
    for role, commit, tree, path, digest in (
            ("baseline", BASELINE_COMMIT, BASELINE_TREE, binaries[0], digests[0]),
            ("candidate", CANDIDATE_COMMIT, CANDIDATE_TREE, binaries[1], digests[1])):
        data = Path(path).read_bytes()
        if hashlib.sha256(data).hexdigest() != digest:
            raise ValueError(f"{role} binary {path} is not the held binary {digest}")
        subject = record["subjects"][role]
        subject["source_commit"], subject["source_tree"] = commit, tree
        subject["binary"] = evidence.put(f"{role}-ide", data, installed=False)
        subject["source_snapshot"] = evidence.json(f"{role}-source.json", {
            "schema": binding.SOURCE_SNAPSHOT_SCHEMA, "version": 1, "source_commit": commit,
            "source_tree": tree, "snapshot_kind": "git-tree-with-submodules-and-generated-inputs"})
        subject["build_receipt"] = evidence.json(f"{role}-build.json", {
            "schema": binding.BUILD_RECEIPT_SCHEMA, "version": 1, "source_commit": commit,
            "source_tree": tree, "source_snapshot_sha256": subject["source_snapshot"]["sha256"],
            "binary_sha256": digest, **toolchain,
            "relation": "git-source-snapshot-to-trusted-build-to-binary"})
    subjects = record["subjects"]

    # Execution: the service, host-profile, qualification and lease receipts.
    execution = record["execution"]
    execution["service"] = {"id": SERVICE_ID, "version": SERVICE_VERSION, "recipe": evidence.json(
        "service-receipt.json", {
            "schema": binding.SERVICE_RECEIPT_SCHEMA, "version": 1, "service_id": SERVICE_ID,
            "service_version": SERVICE_VERSION, "whole_job": True,
            "phases": ["preparation", "build", "tests", "measurement", "finalization", "cleanup"],
            "supervisor_authoritative": True, "lease_protocol": binding.LEASE_PROTOCOL,
            "cgroup_cleanup": True, "descendant_cleanup": True})}
    profile = evidence.json("host-profile.json", {
        "schema": binding.PROFILE_SCHEMA, "version": 1, "profile_id": PROFILE_ID,
        "profile_version": PROFILE_VERSION, "machine_id": MACHINE_ID, "logical_cpu": cpu,
        "native_target": binding.NATIVE_TIMED_TARGET, "native_only": True,
        "whole_host_isolation": True, "lease_protocol": binding.LEASE_PROTOCOL})
    execution["profile"] = {"id": PROFILE_ID, "version": PROFILE_VERSION, "machine_id": MACHINE_ID,
                            "descriptor": profile, "digest": profile["sha256"]}
    execution["host"] = {
        "machine_id": MACHINE_ID,
        "qualification_receipt": evidence.json("host-qualification.json", {
            "schema": binding.QUALIFICATION_SCHEMA, "version": 1, "machine_id": MACHINE_ID,
            "profile_id": PROFILE_ID, "profile_version": PROFILE_VERSION, "qualified": True,
            "logical_cpu": cpu, "native_target": binding.NATIVE_TIMED_TARGET,
            "whole_host_isolation": True, "lease_protocol": binding.LEASE_PROTOCOL}),
        "aa_admission_receipt": {"path": ADMISSION_PATH, "bytes": 1, "sha256": PENDING},
    }
    execution["lease"]["receipt"] = evidence.json("lease-receipt.json", {
        "schema": binding.LEASE_RECEIPT_SCHEMA, "version": 1, "authority": binding.LEASE_AUTHORITY,
        "access": binding.LEASE_ACCESS, "cleanup": binding.LEASE_CLEANUP, "owner": "server-supervisor",
        "candidate_can_access": False, "cloexec_before_candidate": True, "cgroup_cleanup": True,
        "descendant_cleanup": True})

    # Provenance: the #510 receipts over these subjects, toolchain and harness.
    provenance = record["provenance"]
    provenance["relation_receipt"] = evidence.json("provenance-relations.json", {
        "schema": binding.PROVENANCE_SCHEMA, "version": binding.PROVENANCE_VERSION,
        **{role: {"source_commit": subjects[role]["source_commit"],
                  "source_tree": subjects[role]["source_tree"],
                  "source_snapshot_sha256": subjects[role]["source_snapshot"]["sha256"],
                  "binary_sha256": subjects[role]["binary"]["sha256"],
                  "build_receipt_sha256": subjects[role]["build_receipt"]["sha256"]}
           for role in ("baseline", "candidate")},
        "toolchain": toolchain,
        "harness": {"source_commit": measurement["harness_source_commit"],
                    "source_tree": measurement["harness_source_tree"],
                    "binary_sha256": measurement["harness_binary"]["sha256"],
                    "statistics_sha256": measurement["statistics_implementation"]["sha256"]}})
    provenance["replay_bundle"] = evidence.put("provenance-replay-bundle.tar", "#510 bundle\n")
    provenance["census_receipt"] = evidence.json("provenance-census-replay.json", {
        "schema": "buster-native-retirement-census-replay-v1", "success": True,
        "release": "retirement-v1", "github_run_id": "123",
        "rows_validated": binding.SUPPORT_OBJECT_ROW_COUNT,
        "inputs_validated": binding.SUPPORT_INPUT_COUNT, "joined_files": 8,
        "joined_tree_sha256": "d" * 64, "candidate_binary_sha256": digests[1],
        "archived_direct_oracle_sha256": "e" * 64, "rebuilt_direct_oracle_sha256": "e" * 64,
        "rebuilt_matches_archived": True, "validator_report_sha256": files["validator_report"]["sha256"]})
    provenance["strict_receipt"] = evidence.json("provenance-strict-replay.json", {
        "schema": "buster-native-retirement-strict-replay-v1", "success": True,
        "release": "retirement-v1", "github_run_id": "123", "archive_sha256": "f" * 64,
        "archive_size": 1, "candidate_binary_sha256": digests[1], "recorded_summary": "pass",
        "replayed_summary": "pass", "cases": 1, "configurations": 432})
    provenance["replay_receipt"] = evidence.json("provenance-replay.json", {
        "schema": binding.REPLAY_SCHEMA, "version": binding.REPLAY_VERSION,
        "publisher": "native-retirement-evidence-v1",
        "bundle_sha256": provenance["replay_bundle"]["sha256"],
        "published_bundle_sha256": provenance["replay_bundle"]["sha256"],
        "downloaded_bundle_sha256": provenance["replay_bundle"]["sha256"],
        "census_receipt_sha256": provenance["census_receipt"]["sha256"],
        "strict_receipt_sha256": provenance["strict_receipt"]["sha256"],
        "contract_source_commit": record["contract"]["source_commit"],
        "contract_source_tree": record["contract"]["source_tree"],
        "candidate_source_commit": CANDIDATE_COMMIT, "candidate_source_tree": CANDIDATE_TREE,
        "replayed": True, "result": "identity-and-evidence-replayed"})

    # Rules: the frozen campaign values.
    counts = binding._family_member_counts(family)
    record["rules"]["sampling"].update({
        "seed": seed, "pairs_per_round": pairs, "resamples": resamples,
        "bootstrap_members_per_scope": counts["bootstrap_members_per_scope"],
        "cell_members_per_scope": counts["cell_members_per_scope"],
    })
    record["rules"]["uncertainty"]["resampling"]["resamples"] = resamples

    # The admission record over the census rows.
    census_rows = binding._tsv((census / CENSUS_FILES["rows"]).read_bytes(), binding.ROW_FIELDS, "census rows")
    admission = evidence.json("admission-records.json", admission_records(
        census_rows, parsed, files["manifest"]["sha256"], files["rows"]["sha256"]))

    lines = [HEADER]
    lines += [f"{name}={canonical(record[name])}" for name in SECTIONS]
    lines.append(f"admission={canonical(admission)}")
    return ("\n".join(lines) + "\n").encode("utf-8"), evidence.files


def install(directory, files):
    """The evidence directory: new, its files read-only, then itself."""
    directory = Path(directory)
    directory.mkdir(mode=0o700)
    for name, data in sorted(files.items()):
        target = directory / name
        target.write_bytes(data)
        target.chmod(0o444)
    directory.chmod(0o500)


def main():
    if len(sys.argv) != 13:
        print(__doc__, file=sys.stderr)
        return 2
    (census, output, evidence, baseline_binary, candidate_binary, baseline, candidate, contract,
     seed, pairs, resamples, cpu) = sys.argv[1:]
    data, files = context(census, (baseline_binary, candidate_binary), (baseline, candidate), contract,
                          int(seed), int(pairs), int(resamples), int(cpu))
    install(evidence, files)
    Path(output).write_bytes(data)
    return 0


if __name__ == "__main__":
    sys.exit(main())
