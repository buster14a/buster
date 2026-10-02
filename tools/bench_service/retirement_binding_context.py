#!/usr/bin/env python3
"""Assemble the #881 retirement binding context from verified inputs.

Usage: retirement_binding_context.py --inputs INPUTS --evidence-root ROOT
       --census CENSUS --binaries-record RECORD --profile PROFILE --output OUTPUT

This is the production generator of
``recipes/native-retirement-performance-v1.binding-context`` (the profile
pins it with ``binding-context-sha256=``). The worker-unit producer reads it
(``bq_retirement_worker_binding_import`` in retirement_worker_compose.c) and
checks it against the profile and the campaign
(``bq_retirement_worker_binding_check``) before it writes the #511 binding.
The bytes are the header ``BQ-RETIREMENT-BINDING-CONTEXT-V1`` and one
``<section>=<canonical JSON>`` line for each of contract, execution,
measurement, population, producer, provenance, requested_work, rules,
subjects and support, then ``admission=`` (the workflow's admission record
descriptor). ``execution`` opens with the admission-receipt sentinel the
producer replaces.

What the tool checks, and what it cannot:

* every artifact descriptor ``{path, bytes, sha256}`` is hashed from the named
  file under ROOT. ROOT and CENSUS are opened without following a symbolic
  link, every component with O_NOFOLLOW, and each file is fstat-checked,
  hashed and read once from that descriptor (``Evidence``); every later
  check reads those held bytes;
* the six pinned #508 support files are CENSUS's ``support.tsv``,
  ``manifest.txt``, ``inputs.tsv``, ``rows.tsv``, ``performance-rows.json`` and
  ``validator-report.json``, published as ``census/<name>``, and each must
  hash to its PROFILE pin, as must the contract source (``contract-sha256=``);
* the population (row count, rows digest, axes, statistical family, source
  digests) is derived from the census performance rows;
* both subject binaries must hash to the digests RECORD names. RECORD (the
  matched-build import, ``BQ-RETIREMENT-BINARIES-V2``) is a consistency
  input, not an authority: nothing here authenticates it. The authority is
  the unit, whose binding check requires the context's digests to equal the
  gate's held binaries;
* the #511 validator's own evidence checks run over the held bytes
  (``evidence_checks``): build receipts and source snapshots against the
  subjects and producer; the service, host profile, qualification and lease
  receipts (the #422 host facts: none is defaulted); and the relation, #510
  census, strict and replay receipts. The A/A admission receipt cannot exist
  before the campaign, so that part of the execution check sees a
  placeholder and checks nothing;
* the campaign policy (seed, pairs per round, resamples, bootstrap members)
  is range-checked, and the bootstrap member count must equal the derived
  family's.

It cannot check what only the campaign or the service knows: the A/A
admission, whether RECORD came from the unit, and the #422 facts beyond
their receipts' self-consistency. Each section passes the #511 validator's
structural check (tools/native_retirement_performance_binding.py), and the
emitted bytes are re-parsed with the layout the C importer enforces
(``check_context``: no NaN or Infinity, nesting at most 256). Any missing or
disagreeing input fails closed with a message and exit status 1; OUTPUT is
created exclusively and never replaced. Install it read-only and single-link
(``chmod 0400``), owned by root or the service user, as the importer's
installed-file read requires.

One admitted context pins exactly one baseline/candidate pair: a rebuilt
subject with other binary bytes needs a new context and a new profile pin.

Map: ``INPUTS_SCHEMA`` and ``inputs_template`` (the INPUTS shape);
``Evidence`` (descriptor reads and held bytes); ``profile_pins``;
``binaries_record``; ``host_facts``; ``campaign_policy``; ``approved_rules``;
``assemble`` (the record sections); ``evidence_checks`` (the #511 evidence
checks); ``json_depth``, ``encode``; ``check_context`` (the C-layout
re-parse); ``context`` (all of it); ``main``.
"""

import argparse
import copy
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import stat
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import native_retirement_performance_binding as binding  # noqa: E402

HEADER = "BQ-RETIREMENT-BINDING-CONTEXT-V1"
SECTIONS = ("contract", "execution", "measurement", "population", "producer", "provenance",
            "requested_work", "rules", "subjects", "support")
INPUTS_SCHEMA = "bq-retirement-binding-context-inputs-v1"
# The admission receipt sentinel (BQ_RETIREMENT_WORKER_ADMISSION_SENTINEL):
# the producer replaces it with the admitted A/A receipt's descriptor.
ADMISSION_PATH = "retirement-aa-admission.json"
PENDING = "0" * 64
EXECUTION_PREFIX = '{"host":{'
ADMISSION_SENTINEL = ('"aa_admission_receipt":{"bytes":1,"path":"' + ADMISSION_PATH +
                      '","sha256":"' + PENDING + '"}')
# The pinned #508 support roles: the installed census file each is read from
# and the profile key that pins it (bq_retirement_worker_binding_role_pins).
CENSUS_FILES = {
    "support_declaration": "support.tsv",
    "manifest": "manifest.txt",
    "inputs": "inputs.tsv",
    "rows": "rows.tsv",
    "performance_rows": "performance-rows.json",
    "validator_report": "validator-report.json",
}
CENSUS_PINS = {
    "support_declaration": "support-declaration-sha256",
    "manifest": "census-manifest-sha256",
    "inputs": "census-inputs-sha256",
    "rows": "census-rows-sha256",
    "performance_rows": "performance-rows-sha256",
    "validator_report": "validator-report-sha256",
}
# Support roles that are not census outputs, read from the evidence root.
EVIDENCE_SUPPORT_ROLES = tuple(role for role in binding.SUPPORT_FILE_ROLES if role not in CENSUS_FILES)
BINARIES_HEADER = "BQ-RETIREMENT-BINARIES-V2"
BINARIES_KEYS = ("job", "token", "request", "preparation", "directory", "base-source",
                 "candidate-source", "base-binary", "candidate-binary")
# BQ_RETIREMENT_WORKER_BINDING_CONTEXT_CAP (the context is one bundle file).
CONTEXT_CAP = 64 * 1024 * 1024 - 4096
# Evidence files are bounded like the #511 validator's receipts.
EVIDENCE_CAP = 512 * 1024 * 1024
DECIMAL_RE = re.compile(r"^(?:0|[1-9][0-9]{0,19})$")
UINT64_MAX = (1 << 64) - 1
# TP_COMPOSE_JSON_DEPTH: the C reader refuses deeper container nesting.
JSON_DEPTH = 256
SUBJECTS = {
    "baseline": ("direct-baseline", "direct-native", ("pre-cutover",)),
    "candidate": ("mir-candidate", "mir-only", ("mir-only-cutover", "post-deletion")),
}


class ContextError(Exception):
    """A missing, malformed or disagreeing input; nothing is written."""


def _fail(message):
    raise ContextError(message)


def canonical(value):
    return json.dumps(value, sort_keys=True, separators=(",", ":"), ensure_ascii=False)


def _checked(function, *arguments):
    """Run one #511 structural check, reporting its refusal as ours."""
    try:
        return function(*arguments)
    except ValueError as error:
        _fail(str(error))


def _load_json(data, name):
    try:
        return json.loads(data.decode("utf-8"), object_pairs_hook=binding._json_object,
                          parse_constant=_refuse_constant)
    except (UnicodeDecodeError, ValueError, RecursionError) as error:
        _fail(f"{name} is not strict JSON: {error}")


def _refuse_constant(name):
    """NaN and Infinity, which the C canonical reader refuses."""
    raise ValueError(f"non-finite number {name}")


class Evidence:
    """Files of one directory, each read once through a descriptor.

    The directory itself is opened without following a symbolic link, every
    path component below it with O_NOFOLLOW (directories with O_DIRECTORY),
    and each leaf is checked with fstat, hashed and read from that same
    descriptor. The held bytes are what every later check sees: with a
    snapshot directory (private, made here) they are also written there, and
    the #511 evidence checks, which read by path, run over the snapshot."""

    def __init__(self, root, name, snapshot=None):
        self.name = name
        self.held = {}
        self.snapshot = snapshot
        try:
            self.descriptor = os.open(root, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW | os.O_CLOEXEC)
        except OSError as error:
            _fail(f"{name} is not a directory reached without a symbolic link: {error}")

    def close(self):
        os.close(self.descriptor)

    def read(self, relative, name, cap=EVIDENCE_CAP):
        if relative not in self.held:
            self.held[relative] = self._read(relative, name, cap)
        return self.held[relative]

    def _read(self, relative, name, cap):
        _checked(binding._relative_path, relative, name)
        parts = PurePosixPath(relative).parts
        if not parts or any(part in (".", "..") for part in parts):
            _fail(f"{name} must name a file below the {self.name}")
        opened = []
        chunks = []
        try:
            directory = self.descriptor
            for part in parts[:-1]:
                directory = os.open(part, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW | os.O_CLOEXEC,
                                    dir_fd=directory)
                opened.append(directory)
            leaf = os.open(parts[-1], os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK | os.O_CLOEXEC, dir_fd=directory)
            opened.append(leaf)
            metadata = os.fstat(leaf)
            if not stat.S_ISREG(metadata.st_mode) or not 0 < metadata.st_size <= cap:
                _fail(f"{name} is not a non-empty regular file of at most {cap} bytes")
            remaining = metadata.st_size + 1
            chunk = os.read(leaf, min(remaining, 1 << 20))
            while chunk:
                chunks.append(chunk)
                remaining -= len(chunk)
                chunk = os.read(leaf, min(remaining, 1 << 20)) if remaining > 0 else b""
        except OSError as error:
            _fail(f"{name} cannot be read below the {self.name} without a symbolic link: {error}")
        finally:
            for descriptor in reversed(opened):
                os.close(descriptor)
        data = b"".join(chunks)
        if len(data) != metadata.st_size:
            _fail(f"{name} changed size while it was read")
        if self.snapshot is not None:
            target = self.snapshot.joinpath(*parts)
            try:
                target.parent.mkdir(parents=True, exist_ok=True)
                with open(target, "xb") as stream:
                    stream.write(data)
            except OSError as error:
                _fail(f"{name} cannot be held beside the other evidence: {error}")
        return data

    def descriptor_of(self, relative, name, published=None):
        """The artifact descriptor of one file, hashed from the held bytes."""
        data = self.read(relative, name)
        return {"path": published or relative, "bytes": len(data), "sha256": hashlib.sha256(data).hexdigest()}


def profile_pins(data):
    """The profile's `key=sha256` pins (duplicates and malformed lines refused)."""
    pins = {}
    text = data.decode("ascii", errors="strict") if data.isascii() else _fail("profile is not ASCII")
    if not text.endswith("\n"):
        _fail("profile must end with a newline")
    for line in text[:-1].split("\n"):
        key, separator, value = line.partition("=")
        if not separator or not key:
            _fail(f"profile line is not key=value: {line!r}")
        if key in pins:
            _fail(f"profile repeats {key}")
        pins[key] = value
    for key in ("contract-sha256", *CENSUS_PINS.values()):
        if not binding.SHA256_RE.fullmatch(pins.get(key, "")):
            _fail(f"profile has no {key}= pin")
    return pins


def binaries_record(data):
    """Both matched binaries' digests from the build import record, exactly
    as bq_retirement_binaries_format writes it."""
    text = data.decode("ascii") if data.isascii() else _fail("binaries record is not ASCII")
    lines = text.split("\n")
    if lines[-1] != "" or len(lines) != len(BINARIES_KEYS) + 2 or lines[0] != BINARIES_HEADER:
        _fail(f"binaries record is not a canonical {BINARIES_HEADER} record")
    values = {}
    for key, line in zip(BINARIES_KEYS, lines[1:-1]):
        if not line.startswith(key + "="):
            _fail(f"binaries record line {line!r} is not {key}=")
        values[key] = line[len(key) + 1:]
    for key in ("job", "token"):
        if not DECIMAL_RE.fullmatch(values[key]) or not 0 < int(values[key]) <= UINT64_MAX:
            _fail(f"binaries record {key}= is not a canonical decimal in 1..{UINT64_MAX}")
    for key in ("request", "preparation", "directory", "base-source", "candidate-source"):
        _checked(binding._sha, values[key], f"binaries record {key}")
    digests = []
    for key in ("base-binary", "candidate-binary"):
        fields = values[key].split(" ")
        if len(fields) != 2:
            _fail(f"binaries record {key}= must hold a digest and an identity")
        for field in fields:
            _checked(binding._sha, field, f"binaries record {key}")
        digests.append(fields[0])
    if digests[0] == digests[1]:
        _fail("binaries record names the same binary for both subjects")
    return {"baseline": digests[0], "candidate": digests[1]}


def host_facts(evidence, host):
    """execution.host and execution.profile from the #422 host facts. The
    profile's id and version are read from the host profile; every receipt
    field is then checked by the #511 execution-evidence check (``assemble``).
    Nothing is defaulted."""
    host = _checked(binding._keys, host, ("machine_id", "qualification_receipt", "profile"), "inputs.host")
    machine = _checked(binding._token, host["machine_id"], "inputs.host.machine_id")
    qualification = evidence.descriptor_of(host["qualification_receipt"], "inputs.host.qualification_receipt")
    profile = evidence.descriptor_of(host["profile"], "inputs.host.profile")
    described = _load_json(evidence.read(host["profile"], "inputs.host.profile"), "host profile")
    if type(described) is not dict:
        _fail("host profile is not a JSON object")
    profile_id = _checked(binding._token, described.get("profile_id"), "host profile.profile_id")
    version = _checked(binding._token, described.get("profile_version"), "host profile.profile_version")
    logical_cpu = described.get("logical_cpu")
    return ({"machine_id": machine, "qualification_receipt": qualification},
            {"id": profile_id, "version": version, "machine_id": machine, "descriptor": profile,
             "digest": profile["sha256"]}, logical_cpu)


def campaign_policy(campaign, family):
    """The frozen campaign values, range-checked as the #511 rules and #619 do."""
    campaign = _checked(binding._keys, campaign, ("seed", "pairs_per_round", "resamples",
                                                  "bootstrap_members_per_scope"), "inputs.campaign")
    seed = campaign["seed"]
    if type(seed) is not int or not 0 < seed <= (1 << 64) - 1:
        _fail("inputs.campaign.seed must be a positive uint64")
    pairs = campaign["pairs_per_round"]
    if type(pairs) is not int or not binding.SAMPLING_MIN_PAIRS <= pairs <= binding.SAMPLING_MAX_PAIRS:
        _fail(f"inputs.campaign.pairs_per_round must be in {binding.SAMPLING_MIN_PAIRS}.."
              f"{binding.SAMPLING_MAX_PAIRS}")
    if pairs % 2:
        _fail("inputs.campaign.pairs_per_round must be even for AB/BA blocks")
    resamples = campaign["resamples"]
    if type(resamples) is not int or not 100000 <= resamples <= 1000000:
        _fail("inputs.campaign.resamples must be in 100000..1000000")
    counts = _checked(binding._family_member_counts, family)
    if campaign["bootstrap_members_per_scope"] != counts["bootstrap_members_per_scope"]:
        _fail("inputs.campaign.bootstrap_members_per_scope differs from the derived statistical family")
    return {"seed": seed, "pairs_per_round": pairs, "resamples": resamples,
            "bootstrap_members_per_scope": counts["bootstrap_members_per_scope"],
            "cell_members_per_scope": counts["cell_members_per_scope"]}


def approved_rules(campaign):
    """The #511 decision rules: the approved constants and the campaign values."""
    rules = {
        "thresholds": {
            "aggregate": {metric: float(limit) for metric, limit in binding.AGGREGATE_THRESHOLDS.items()},
            "per_cell": {metric: float(limit) for metric, limit in binding.CELL_THRESHOLDS.items()},
        },
        "sampling": {"rounds": 2, "frozen_before_samples": True, "warmups_per_variant": 2,
                     "block_order": "one-AB-and-one-BA-pair",
                     "fixed_order": "seeded-cell-order-and-first-order",
                     "optional_stopping": False, "outlier_deletion": False,
                     "retain_all_samples": True, **campaign},
        "aggregation": {"ratio": "candidate-over-baseline",
                        "wall_time": "median-of-two-pair-block-geometric-means",
                        "peak_memory": "median-of-two-pair-block-geometric-means",
                        "batch_wall_time": "median-of-two-pair-block-geometric-means",
                        "batch_peak_rss": "median-of-two-pair-block-geometric-means",
                        "code_bytes": "exact-code-section-sum-ratio",
                        "runtime": "median-of-two-pair-block-geometric-means",
                        "cell_weight": "one-equal-weight-per-required-cell",
                        "denominator": "requested-work-from-manifest",
                        "scope": "both-rounds-and-pooled-analysis",
                        "runtime_eligibility": "independent-native-executable-oracle-only",
                        "code_bytes_scope": "deterministic-code-section-payload-only",
                        "timed_population": f"compiler-eligible-rows-on-{binding.NATIVE_TIMED_TARGET}",
                        "sampling_unit": "native-host-batch-group",
                        "batch_cells": "object-batch-groups",
                        "code_bytes_measurement": "once-per-variant-and-row-on-every-target-with-reproduction"},
        "uncertainty": {
            "confidence": "one-sided-95-percent-upper-bound", "simultaneous": True,
            "family_correction": "Bonferroni",
            "resampling": {"method": "paired-block-bootstrap", "resamples": campaign["resamples"],
                           "block_unit": "paired-round-block", "seeded": True},
            "invalid_data": "fail-closed",
            "family": {"scopes": list(binding.STATISTICAL_SCOPES),
                       "dimensions": list(binding.STATISTICAL_DIMENSIONS),
                       "metrics": list(binding.STATISTICAL_METRICS), "simultaneous": True,
                       "pic_slices": "approved-as-required-slices"},
        },
        "outcomes": {"allowed": list(binding.OUTCOMES),
                     "pass_requires": "all-identities-rows-oracles-rounds-bounds-and-code-bytes",
                     "only_pass_accepts": True},
    }
    return _checked(binding._rules, rules)


def _artifacts(evidence, value, keys, name):
    """Each named key of value, a path under ROOT, as its hashed descriptor."""
    return {key: evidence.descriptor_of(value[key], f"{name}.{key}") for key in keys}


def assemble(inputs, root, census, binaries, pins, snapshot):
    """The record's pre-campaign sections and the admission descriptor."""
    inputs = _checked(binding._keys, inputs, ("schema", "contract", "support", "requested_work", "subjects",
                                              "producer", "measurement", "execution", "host", "provenance",
                                              "admission", "campaign"), "inputs")
    if inputs["schema"] != INPUTS_SCHEMA:
        _fail(f"inputs.schema must be {INPUTS_SCHEMA!r}")
    contract = _checked(binding._keys, inputs["contract"], ("source_commit", "source_tree", "source"),
                        "inputs.contract")
    contract = {"source_commit": contract["source_commit"], "source_tree": contract["source_tree"],
                **_artifacts(root, contract, ("source",), "inputs.contract")}
    if contract["source"]["sha256"] != pins["contract-sha256"]:
        _fail("inputs.contract.source does not hash to the profile's contract-sha256= pin")

    support_in = _checked(binding._keys, inputs["support"], ("version", "files", "validator", "closure"),
                          "inputs.support")
    evidence_files = _checked(binding._keys, support_in["files"], EVIDENCE_SUPPORT_ROLES, "inputs.support.files")
    files, census_data = [], {}
    for role in binding.SUPPORT_FILE_ROLES:
        if role in CENSUS_FILES:
            census_data[role] = census.read(CENSUS_FILES[role], f"census {CENSUS_FILES[role]}")
            item = census.descriptor_of(CENSUS_FILES[role], f"census {CENSUS_FILES[role]}",
                                        f"census/{CENSUS_FILES[role]}")
            if item["sha256"] != pins[CENSUS_PINS[role]]:
                _fail(f"census {CENSUS_FILES[role]} does not hash to the profile's {CENSUS_PINS[role]}= pin")
        else:
            item = root.descriptor_of(evidence_files[role], f"inputs.support.files.{role}")
        files.append({"name": role, **item})
    validator = _checked(binding._keys, support_in["validator"], ("source_commit", "source_tree", "source"),
                         "inputs.support.validator")
    validator = {"name": "native_retirement_contract.py", "version": 1,
                 "source_commit": validator["source_commit"], "source_tree": validator["source_tree"],
                 **_artifacts(root, validator, ("source",), "inputs.support.validator")}
    closure_in = _checked(binding._keys, support_in["closure"], binding.REQUIRED_WORK_CLOSURE_KINDS,
                          "inputs.support.closure")
    closure = {}
    for kind in binding.REQUIRED_WORK_CLOSURE_KINDS:
        entry = _checked(binding._keys, closure_in[kind], ("name", "artifact"), f"inputs.support.closure.{kind}")
        closure[kind] = {"name": entry["name"],
                         **_artifacts(root, entry, ("artifact",), f"inputs.support.closure.{kind}")}
    support = {"schema": "native-retirement-support-v1", "version": support_in["version"],
               "root_sha256": binding._support_root_digest(files, validator, closure), "files": files,
               "validator": validator, "closure": closure}
    _checked(binding._support, support)

    work_in = _checked(binding._keys, inputs["requested_work"], ("items",), "inputs.requested_work")
    items = []
    for index, item in enumerate(_checked(binding._list, work_in["items"], "inputs.requested_work.items")):
        item = _checked(binding._keys, item, ("kind", "name", "artifact"), f"inputs.requested_work.items[{index}]")
        items.append({"kind": item["kind"], "name": item["name"],
                      **_artifacts(root, item, ("artifact",), f"inputs.requested_work.items[{index}]")})
    requested_closure = {"manifest_sha256": files[binding.SUPPORT_FILE_ROLES.index("manifest")]["sha256"]}
    for kind in binding.REQUIRED_WORK_CLOSURE_KINDS:
        requested_closure[kind] = {"name": closure[kind]["name"], "bytes": closure[kind]["artifact"]["bytes"],
                                   "sha256": closure[kind]["artifact"]["sha256"]}
    requested_work = {"schema": "native-retirement-requested-work-v1", "version": 1,
                      "root_sha256": binding._canonical_work_digest(items), "items": items,
                      "closure": requested_closure}
    _checked(binding._requested_work, requested_work, support)

    rows_data = census_data["performance_rows"]
    row_data = _checked(binding._performance_rows, rows_data)
    parsed, axes, family = row_data
    population = {"required_row_count": len(parsed),
                  "required_rows_sha256": hashlib.sha256(rows_data).hexdigest(), "axes": axes,
                  "row_identity_fields": list(binding.ROW_IDENTITY_FIELDS), "statistical_family": family,
                  "source_digests": binding._artifact_digest_map(support)}
    _checked(binding._population, population, support, row_data)

    subjects_in = _checked(binding._keys, inputs["subjects"], ("baseline", "candidate"), "inputs.subjects")
    subjects = {}
    for side, (role, dispatch, stages) in SUBJECTS.items():
        name = f"inputs.subjects.{side}"
        value = _checked(binding._keys, subjects_in[side], ("source_commit", "source_tree", "source_snapshot",
                                                            "binary", "build_receipt", "stage"), name)
        subject = {"role": role, "source_commit": value["source_commit"], "source_tree": value["source_tree"],
                   "dispatch": dispatch, "stage": value["stage"],
                   **_artifacts(root, value, ("source_snapshot", "binary", "build_receipt"), name)}
        if subject["binary"]["sha256"] != binaries[side]:
            _fail(f"{name}.binary does not hash to the matched-build import's {side} binary digest")
        subjects[side] = _checked(binding._subject, subject, f"subjects.{side}", role, dispatch, set(stages))
    for field in ("source_commit", "source_tree"):
        if subjects["baseline"][field] == subjects["candidate"][field]:
            _fail(f"baseline and candidate {field} must be distinct")

    producer_in = _checked(binding._keys, inputs["producer"], ("toolchain", "build"), "inputs.producer")
    toolchain = _checked(binding._keys, producer_in["toolchain"],
                         ("version", "compiler_binary", "resource_directory"), "inputs.producer.toolchain")
    build = _checked(binding._keys, producer_in["build"], ("configuration", "flags"), "inputs.producer.build")
    producer = {
        "toolchain": {"name": "clang", "version": toolchain["version"],
                      **_artifacts(root, toolchain, ("compiler_binary", "resource_directory"),
                                   "inputs.producer.toolchain")},
        "build": {"mode": "Release", "unity": True, "warnings_as_errors": True, "sanitizers": False,
                  "profiling": False, "allocation_hooks": False,
                  **_artifacts(root, build, ("configuration", "flags"), "inputs.producer.build")},
    }
    _checked(binding._producer, producer)

    measurement_in = _checked(binding._keys, inputs["measurement"], ("harness_source_commit",
                              "harness_source_tree", "harness_binary", "statistics_implementation"),
                              "inputs.measurement")
    measurement = {"harness_source_commit": measurement_in["harness_source_commit"],
                   "harness_source_tree": measurement_in["harness_source_tree"],
                   **_artifacts(root, measurement_in, ("harness_binary", "statistics_implementation"),
                                "inputs.measurement")}
    _checked(binding._measurement, measurement)

    execution_in = _checked(binding._keys, inputs["execution"], ("service", "lease_receipt"), "inputs.execution")
    service = _checked(binding._keys, execution_in["service"], ("id", "version", "recipe"),
                       "inputs.execution.service")
    host, profile, logical_cpu = host_facts(root, inputs["host"])
    host["aa_admission_receipt"] = {"path": ADMISSION_PATH, "bytes": 1, "sha256": PENDING}
    execution = {
        "service": {"id": service["id"], "version": service["version"],
                    **_artifacts(root, service, ("recipe",), "inputs.execution.service")},
        "host": host, "profile": profile, "job_ownership": binding.LEASE_PROTOCOL,
        "lease": {"authority": binding.LEASE_AUTHORITY, "access": binding.LEASE_ACCESS,
                  "cleanup": binding.LEASE_CLEANUP,
                  "receipt": root.descriptor_of(execution_in["lease_receipt"], "inputs.execution.lease_receipt")},
        "native_execution": "native-only",
    }
    _checked(binding._execution, execution)

    receipts = ("relation_receipt", "replay_receipt", "replay_bundle", "census_receipt", "strict_receipt")
    provenance_in = _checked(binding._keys, inputs["provenance"], receipts, "inputs.provenance")
    provenance = {"schema": binding.PROVENANCE_SCHEMA, "version": binding.PROVENANCE_VERSION,
                  **_artifacts(root, provenance_in, receipts, "inputs.provenance")}
    _checked(binding._provenance, provenance)

    rules = approved_rules(campaign_policy(inputs["campaign"], family))
    _checked(binding._check_sampling_family, population, rules)
    admission = root.descriptor_of(inputs["admission"], "inputs.admission")

    record = {"contract": contract, "execution": execution, "measurement": measurement,
              "population": population, "producer": producer, "provenance": provenance,
              "requested_work": requested_work, "rules": rules, "subjects": subjects, "support": support}
    _checked(binding._keys, record["contract"], ("source_commit", "source_tree", "source"), "contract")
    _checked(binding._commit, contract["source_commit"], "contract.source_commit")
    _checked(binding._commit, contract["source_tree"], "contract.source_tree")
    # Every artifact path is unique, as the #511 validator requires of the
    # whole binding; the workflow's phases and other records are written
    # after the campaign, so they are listed as absent here.
    workflow = {"phases": dict.fromkeys(binding.WORKFLOW_PHASES),
                "records": {"admission": admission, "oracle": None, "result_input_plan": None}}
    listed = binding._all_artifacts({**record, "workflow": workflow})
    paths = [artifact["path"] for _name, artifact in listed if artifact is not None]
    paths.append(contract["source"]["path"])
    if len(paths) != len(set(paths)):
        _fail("binding artifact paths must be unique")
    evidence_checks(snapshot, record, logical_cpu)
    return record, admission


def evidence_checks(snapshot, record, logical_cpu):
    """The #511 validator's own evidence checks over the held bytes (the
    snapshot): the subjects' snapshots and build receipts against the
    producer (``_check_subject_receipts``); the service, host profile,
    qualification and lease receipts (``_check_execution_evidence``); and the
    relation, #510 census, strict and replay receipts and the replay bundle
    (``_check_provenance_evidence``, which needs nothing from the campaign).

    The A/A admission receipt cannot exist before the campaign, so the
    execution check reads a placeholder at the sentinel's path, derived from
    the bound facts (its phase receipt digest is a placeholder too: the
    service's AA_MEASURED receipt is the coordinator's to check, and the band
    is the fixed in-job one the validator requires): that part checks nothing
    here. The producer's admission
    step checks the real receipt (the post-A/A document binds its digest) and
    lane F's #511 validation checks it inside the final binding."""
    execution = record["execution"]
    baseline = record["subjects"]["baseline"]
    placeholder = {"schema": binding.AA_SCHEMA, "version": binding.AA_VERSION,
                   "aa_decision": binding.AA_DECISION,
                   "equivalence_band": dict(binding.AA_EQUIVALENCE_BAND),
                   "phase_receipt_sha256": "0" * 64,
                   "machine_id": execution["host"]["machine_id"],
                   "profile_id": execution["profile"]["id"], "profile_version": execution["profile"]["version"],
                   "service_id": execution["service"]["id"], "logical_cpu": logical_cpu,
                   "native_target": binding.NATIVE_TIMED_TARGET, "admitted": True, "native_only": True,
                   "baseline_source_commit": baseline["source_commit"],
                   "baseline_source_tree": baseline["source_tree"], "lease_protocol": binding.LEASE_PROTOCOL,
                   "family_sha256": record["population"]["statistical_family"]["sha256"]}
    try:
        with open(snapshot / ADMISSION_PATH, "xb") as stream:
            stream.write(canonical(placeholder).encode())
    except OSError as error:
        _fail(f"the admission placeholder collides with the evidence: {error}")
    _checked(binding._check_subject_receipts, snapshot, record)
    _checked(binding._check_execution_evidence, snapshot, record)
    _checked(binding._check_provenance_evidence, snapshot, record, record["provenance"])


def json_depth(text):
    """The deepest container nesting of JSON text (strings skipped), counted
    without recursion."""
    depth = deepest = 0
    quoted = escaped = False
    for character in text:
        if quoted:
            escaped, quoted = (False, True) if escaped else (character == "\\", character != '"')
        elif character == '"':
            quoted = True
        elif character in "[{":
            depth += 1
            deepest = max(deepest, depth)
        elif character in "]}":
            depth -= 1
    return deepest


def encode(record, admission):
    lines = [HEADER]
    lines += [f"{name}={canonical(record[name])}" for name in SECTIONS]
    lines.append(f"admission={canonical(admission)}")
    return ("\n".join(lines) + "\n").encode("utf-8")


def check_context(data):
    """The layout bq_retirement_worker_binding_parse and the sentinel part of
    bq_retirement_worker_binding_check enforce: the header, exactly one
    canonical-JSON object line per section in order, an admission descriptor,
    and the sentinel exactly once as execution.host's first member. Returns
    the parsed sections."""
    if not data or len(data) > CONTEXT_CAP or b"\0" in data or not data.endswith(b"\n"):
        _fail("context is empty, oversized, holds a NUL or is not LF-terminated")
    lines = data.decode("utf-8").split("\n")[:-1]
    keys = (*SECTIONS, "admission")
    if len(lines) != len(keys) + 1 or lines[0] != HEADER:
        _fail(f"context must be {HEADER} and exactly {len(keys)} section lines")
    sections = {}
    for key, line in zip(keys, lines[1:]):
        name, separator, value = line.partition("=")
        if name != key or not separator:
            _fail(f"context line {len(sections) + 2} must be section {key!r}")
        if json_depth(value) > JSON_DEPTH:
            _fail(f"context section {key} nests deeper than {JSON_DEPTH}")
        parsed = _load_json(value.encode("utf-8"), f"context section {key}")
        if type(parsed) is not dict or canonical(parsed) != value:
            _fail(f"context section {key} is not a canonical JSON object")
        sections[key] = (parsed, value)
    admission = sections["admission"][0]
    if set(admission) != {"bytes", "path", "sha256"} or type(admission["sha256"]) is not str or \
            len(admission["sha256"]) != 64:
        _fail("context admission is not a {bytes, path, sha256} descriptor")
    execution = sections["execution"][1]
    if not execution.startswith(EXECUTION_PREFIX + ADMISSION_SENTINEL) or \
            execution.count(ADMISSION_SENTINEL) != 1:
        _fail("context execution must open with the admission-receipt sentinel, exactly once")
    return {key: parsed for key, (parsed, _value) in sections.items()}


def context(inputs, root, census, binaries_data, profile_data):
    """The verified context bytes for these inputs."""
    binaries, pins = binaries_record(binaries_data), profile_pins(profile_data)
    with tempfile.TemporaryDirectory(prefix="retirement-context-evidence-") as held:
        snapshot = Path(held)
        evidence = Evidence(root, "evidence root", snapshot)
        try:
            census_files = Evidence(census, "census directory")
            try:
                record, admission = assemble(copy.deepcopy(inputs), evidence, census_files, binaries, pins, snapshot)
            finally:
                census_files.close()
        finally:
            evidence.close()
    data = encode(record, admission)
    check_context(data)
    return data


def inputs_template():
    """The INPUTS shape: every leaf a path under ROOT is marked PATH."""
    path = "PATH"
    commit = "COMMIT(40 hex)"
    subject = {"source_commit": commit, "source_tree": commit, "source_snapshot": path, "binary": path,
               "build_receipt": path, "stage": "STAGE"}
    return {
        "schema": INPUTS_SCHEMA,
        "contract": {"source_commit": commit, "source_tree": commit, "source": path},
        "support": {"version": "TOKEN", "files": {role: path for role in EVIDENCE_SUPPORT_ROLES},
                    "validator": {"source_commit": commit, "source_tree": commit, "source": path},
                    "closure": {kind: {"name": "TOKEN", "artifact": path}
                                for kind in binding.REQUIRED_WORK_CLOSURE_KINDS}},
        "requested_work": {"items": [{"kind": "KIND", "name": "TOKEN", "artifact": path}]},
        "subjects": {"baseline": subject, "candidate": subject},
        "producer": {"toolchain": {"version": "TOKEN", "compiler_binary": path, "resource_directory": path},
                     "build": {"configuration": path, "flags": path}},
        "measurement": {"harness_source_commit": commit, "harness_source_tree": commit,
                        "harness_binary": path, "statistics_implementation": path},
        "execution": {"service": {"id": "TOKEN", "version": "TOKEN", "recipe": path}, "lease_receipt": path},
        "host": {"machine_id": "TOKEN (#422)", "qualification_receipt": "PATH (#422)", "profile": "PATH (#422)"},
        "provenance": {name: path for name in ("relation_receipt", "replay_receipt", "replay_bundle",
                                               "census_receipt", "strict_receipt")},
        "admission": path,
        "campaign": {"seed": "UINT64", "pairs_per_round": "EVEN 60..254", "resamples": "100000..1000000",
                     "bootstrap_members_per_scope": "DERIVED COUNT"},
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--inputs", type=Path, help="the reviewed INPUTS JSON")
    parser.add_argument("--evidence-root", type=Path, help="the directory INPUTS paths name")
    parser.add_argument("--census", type=Path, help="the installed #508 census directory")
    parser.add_argument("--binaries-record", type=Path, help="the BQ-RETIREMENT-BINARIES-V2 build import record")
    parser.add_argument("--profile", type=Path, help="the recipe profile holding the contract and census pins")
    parser.add_argument("--output", type=Path, help="the context to create (never replaced)")
    parser.add_argument("--template", action="store_true", help="print the INPUTS shape and exit")
    arguments = parser.parse_args()
    status = 0
    if arguments.template:
        print(json.dumps(inputs_template(), indent=2, sort_keys=True))
    else:
        missing = [name for name in ("inputs", "evidence_root", "census", "binaries_record", "profile", "output")
                   if getattr(arguments, name) is None]
        try:
            if missing:
                _fail("missing required arguments: " + ", ".join("--" + name.replace("_", "-") for name in missing))
            inputs = _load_json(arguments.inputs.read_bytes(), "inputs")
            data = context(inputs, arguments.evidence_root, arguments.census, arguments.binaries_record.read_bytes(),
                           arguments.profile.read_bytes())
            with open(arguments.output, "xb") as stream:
                stream.write(data)
        except (ContextError, OSError) as error:
            print(f"retirement_binding_context: {error}", file=sys.stderr)
            status = 1
    return status


if __name__ == "__main__":
    sys.exit(main())
