#!/usr/bin/env python3
"""Emit a test binding context for the #881 worker-unit fixture.

Usage: retirement_binding_context_fixture.py CENSUS OUTPUT BASELINE_SHA256
       CANDIDATE_SHA256 CONTRACT_SHA256 SEED PAIRS RESAMPLES

The worker-unit producer writes the #511 binding document the composer reads
(tools/bench_service/retirement_worker_compose.c) from a pinned, reviewed
binding context: the header ``BQ-RETIREMENT-BINDING-CONTEXT-V1`` and one
``<section>=<canonical JSON>`` line per pre-campaign section of the binding
record (contract, execution, measurement, population, producer, provenance,
requested_work, rules, subjects, support) plus the workflow's admission record
descriptor. This emits such a context for the preparation fixture: the
structurally complete record of native_retirement_performance_binding_test,
with the fixture census's own support files (CENSUS is the installed census
directory), the statistical family the validator derives from its
performance rows, the fixture's subject binaries, contract pin and frozen
campaign values, and the admission-receipt sentinel the producer replaces.
The subject commits, provenance receipts, host identities and other
descriptors are the test record's, not evidence; no file it names beyond the
census is published. Nothing here is a reviewed production context.

       retirement_binding_context_fixture.py --production-inputs DIRECTORY
       CENSUS BASELINE_BINARY CANDIDATE_BINARY SEED PAIRS RESAMPLES

writes the same test record as inputs of the production generator
(retirement_binding_context.py) under DIRECTORY: ``evidence/`` (every file
the record names, with the two given binaries and build receipts that bind
them), ``inputs.json``, ``binaries.record`` (a BQ-RETIREMENT-BINARIES-V2
record naming both binaries) and ``profile`` (the contract and six census
pins). With the record's own binaries its output equals this fixture's.
"""

import copy
import hashlib
import json
from pathlib import Path
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
# The installed census file of each pinned #508 support role.
CENSUS_FILES = {
    "support_declaration": "support.tsv",
    "manifest": "manifest.txt",
    "inputs": "inputs.tsv",
    "rows": "rows.tsv",
    "performance_rows": "performance-rows.json",
    "validator_report": "validator-report.json",
}


def canonical(value):
    return json.dumps(value, sort_keys=True, separators=(",", ":"), ensure_ascii=False)


def descriptor(path, data):
    return {"path": path, "bytes": len(data), "sha256": hashlib.sha256(data).hexdigest()}


def context(census, baseline, candidate, contract, seed, pairs, resamples):
    record, _contents = binding_tests.BindingTests._build_record()
    record = copy.deepcopy(record)
    census = Path(census)
    support = record["support"]
    for item in support["files"]:
        name = CENSUS_FILES.get(item["name"])
        if name:
            item.update(descriptor(f"census/{name}", (census / name).read_bytes()))
    support["root_sha256"] = binding._support_root_digest(support["files"], support["validator"],
                                                          support["closure"])
    manifest = support["files"][binding.SUPPORT_FILE_ROLES.index("manifest")]
    record["requested_work"]["closure"]["manifest_sha256"] = manifest["sha256"]
    rows_data = (census / CENSUS_FILES["performance_rows"]).read_bytes()
    parsed, _axes, family = binding._performance_rows(rows_data)
    counts = binding._family_member_counts(family)
    population = record["population"]
    population.update({
        "required_row_count": len(parsed),
        "required_rows_sha256": hashlib.sha256(rows_data).hexdigest(),
        "statistical_family": family,
        "source_digests": binding._artifact_digest_map(support),
    })
    record["contract"]["source"]["sha256"] = contract
    record["subjects"]["baseline"]["binary"]["sha256"] = baseline
    record["subjects"]["candidate"]["binary"]["sha256"] = candidate
    record["rules"]["sampling"].update({
        "seed": seed, "pairs_per_round": pairs, "resamples": resamples,
        "bootstrap_members_per_scope": counts["bootstrap_members_per_scope"],
        "cell_members_per_scope": counts["cell_members_per_scope"],
    })
    record["rules"]["uncertainty"]["resampling"]["resamples"] = resamples
    record["execution"]["host"]["aa_admission_receipt"] = {"path": ADMISSION_PATH, "bytes": 1, "sha256": PENDING}
    lines = [HEADER]
    lines += [f"{name}={canonical(record[name])}" for name in SECTIONS]
    lines.append(f"admission={canonical(record['workflow']['records']['admission'])}")
    return ("\n".join(lines) + "\n").encode("utf-8")


def _json_bytes(value):
    return (json.dumps(value, sort_keys=True, separators=(",", ":")) + "\n").encode()


def production_inputs(directory, census, baseline_binary, candidate_binary, seed, pairs, resamples):
    """The test record as the production generator's inputs under directory."""
    record, contents = binding_tests.BindingTests._build_record()
    contents = dict(contents)
    subjects = record["subjects"]
    binaries = {"baseline": Path(baseline_binary).read_bytes(), "candidate": Path(candidate_binary).read_bytes()}
    relation_path = record["provenance"]["relation_receipt"]["path"]
    relation = json.loads(contents[relation_path])
    for side, data in binaries.items():
        subject = subjects[side]
        contents[subject["binary"]["path"]] = data
        receipt = json.loads(contents[subject["build_receipt"]["path"]])
        digest = hashlib.sha256(data).hexdigest()
        if receipt["binary_sha256"] != digest:
            receipt["binary_sha256"] = digest
            contents[subject["build_receipt"]["path"]] = _json_bytes(receipt)
            relation[side]["binary_sha256"] = digest
            relation[side]["build_receipt_sha256"] = hashlib.sha256(
                contents[subject["build_receipt"]["path"]]).hexdigest()
            contents[relation_path] = _json_bytes(relation)
    root = Path(directory)
    for path, data in contents.items():
        target = root / "evidence" / path
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(data)
    census = Path(census)
    _parsed, _axes, family = binding._performance_rows((census / CENSUS_FILES["performance_rows"]).read_bytes())
    support = record["support"]
    files = {item["name"]: item["path"] for item in support["files"]}
    path_of = lambda artifact: artifact["path"]
    side = lambda value: {"source_commit": value["source_commit"], "source_tree": value["source_tree"],
                          "source_snapshot": path_of(value["source_snapshot"]), "binary": path_of(value["binary"]),
                          "build_receipt": path_of(value["build_receipt"]), "stage": value["stage"]}
    execution = record["execution"]
    inputs = {
        "schema": "bq-retirement-binding-context-inputs-v1",
        "contract": {"source_commit": record["contract"]["source_commit"],
                     "source_tree": record["contract"]["source_tree"],
                     "source": path_of(record["contract"]["source"])},
        "support": {"version": support["version"],
                    "files": {role: files[role] for role in ("performance_declaration", "dependencies",
                                                             "environment")},
                    "validator": {"source_commit": support["validator"]["source_commit"],
                                  "source_tree": support["validator"]["source_tree"],
                                  "source": path_of(support["validator"]["source"])},
                    "closure": {kind: {"name": entry["name"], "artifact": path_of(entry["artifact"])}
                                for kind, entry in support["closure"].items()}},
        "requested_work": {"items": [{"kind": item["kind"], "name": item["name"],
                                      "artifact": path_of(item["artifact"])}
                                     for item in record["requested_work"]["items"]]},
        "subjects": {"baseline": side(subjects["baseline"]), "candidate": side(subjects["candidate"])},
        "producer": {"toolchain": {"version": record["producer"]["toolchain"]["version"],
                                   "compiler_binary": path_of(record["producer"]["toolchain"]["compiler_binary"]),
                                   "resource_directory":
                                       path_of(record["producer"]["toolchain"]["resource_directory"])},
                     "build": {"configuration": path_of(record["producer"]["build"]["configuration"]),
                               "flags": path_of(record["producer"]["build"]["flags"])}},
        "measurement": {"harness_source_commit": record["measurement"]["harness_source_commit"],
                        "harness_source_tree": record["measurement"]["harness_source_tree"],
                        "harness_binary": path_of(record["measurement"]["harness_binary"]),
                        "statistics_implementation": path_of(record["measurement"]["statistics_implementation"])},
        "execution": {"service": {"id": execution["service"]["id"], "version": execution["service"]["version"],
                                  "recipe": path_of(execution["service"]["recipe"])},
                      "lease_receipt": path_of(execution["lease"]["receipt"])},
        "host": {"machine_id": execution["host"]["machine_id"],
                 "qualification_receipt": path_of(execution["host"]["qualification_receipt"]),
                 "profile": path_of(execution["profile"]["descriptor"])},
        "provenance": {name: path_of(record["provenance"][name])
                       for name in ("relation_receipt", "replay_receipt", "replay_bundle", "census_receipt",
                                    "strict_receipt")},
        "admission": path_of(record["workflow"]["records"]["admission"]),
        "campaign": {"seed": seed, "pairs_per_round": pairs, "resamples": resamples,
                     "bootstrap_members_per_scope":
                         binding._family_member_counts(family)["bootstrap_members_per_scope"]},
    }
    (root / "inputs.json").write_text(json.dumps(inputs, indent=1, sort_keys=True) + "\n")
    digests = [hashlib.sha256(binaries[side]).hexdigest() for side in ("baseline", "candidate")]
    filler = hashlib.sha256(b"fixture binaries record").hexdigest()
    (root / "binaries.record").write_text(
        f"BQ-RETIREMENT-BINARIES-V2\njob=82\ntoken=1\nrequest={filler}\npreparation={filler}\n"
        f"directory={filler}\nbase-source={filler}\ncandidate-source={filler}\n"
        f"base-binary={digests[0]} {filler}\ncandidate-binary={digests[1]} {filler}\n")
    pins = {"contract-sha256": hashlib.sha256(contents[record["contract"]["source"]["path"]]).hexdigest()}
    keys = {"support_declaration": "support-declaration-sha256", "manifest": "census-manifest-sha256",
            "inputs": "census-inputs-sha256", "rows": "census-rows-sha256",
            "performance_rows": "performance-rows-sha256", "validator_report": "validator-report-sha256"}
    for role, key in keys.items():
        pins[key] = hashlib.sha256((census / CENSUS_FILES[role]).read_bytes()).hexdigest()
    (root / "profile").write_text("schema=1\nrecipe=native-retirement-performance-v1\n" +
                                  "".join(f"{key}={value}\n" for key, value in pins.items()))
    return inputs


def main():
    if len(sys.argv) == 9 and sys.argv[1] == "--production-inputs":
        directory, census, baseline, candidate, seed, pairs, resamples = sys.argv[2:]
        production_inputs(directory, census, baseline, candidate, int(seed), int(pairs), int(resamples))
        return 0
    if len(sys.argv) != 9:
        print(__doc__, file=sys.stderr)
        return 2
    census, output, baseline, candidate, contract, seed, pairs, resamples = sys.argv[1:]
    data = context(census, baseline, candidate, contract, int(seed), int(pairs), int(resamples))
    Path(output).write_bytes(data)
    return 0


if __name__ == "__main__":
    sys.exit(main())
