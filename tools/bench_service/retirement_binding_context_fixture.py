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


def main():
    if len(sys.argv) != 9:
        print(__doc__, file=sys.stderr)
        return 2
    census, output, baseline, candidate, contract, seed, pairs, resamples = sys.argv[1:]
    data = context(census, baseline, candidate, contract, int(seed), int(pairs), int(resamples))
    Path(output).write_bytes(data)
    return 0


if __name__ == "__main__":
    sys.exit(main())
