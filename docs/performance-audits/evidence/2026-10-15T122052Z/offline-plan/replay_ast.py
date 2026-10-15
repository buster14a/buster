#!/usr/bin/env python3
"""Replay retained final AST data with immutable trusted Python; never measure or publish."""
from __future__ import annotations

import argparse
import csv
import io
import re
import hashlib
import json
from pathlib import Path
import socket
import subprocess
import sys

ROOT = Path(__file__).resolve().parent
TRUSTED = "e51b8f63685c19d3ef51c848801c689132f5a1c4"
HEAD = "d824c25a829a307d39534d43fa14d9e2ed2f5407"
EXPECTED = {
    "mode": "pull", "repository": "buster14a/buster", "ref": "refs/pull/4239/head",
    "pull": "4239", "pull_head": HEAD, "base": TRUSTED,
    "base_tree": "d514dc9782fed3bb6db07adc75e91bf0b79039a2",
    "head": HEAD, "head_tree": "da8b6ce77ccdfe1e42a31b7dd8034e6b462e6ee0",
    "trusted_revision": TRUSTED, "request_run_id": "38728597243",
    "run_id": "38728622802", "run_attempt": "1",
}


def refuse_execution(*args, **kwargs):
    raise RuntimeError("Offline AST replay forbids subprocess and network execution")


def read_json(path):
    return json.loads(Path(path).read_text())


def digest(data):
    return hashlib.sha256(data).hexdigest()


def trusted_modules():
    manifest = read_json(ROOT / "trusted-source-manifest.json")
    if manifest["revision"] != TRUSTED:
        raise ValueError("Unexpected trusted source revision")
    for row in manifest["files"]:
        raw = (ROOT / "trusted" / row["path"]).read_bytes()
        if len(raw) != row["bytes"] or digest(raw) != row["sha256"]:
            raise ValueError("Trusted source export changed: " + row["path"])
    subprocess.Popen = refuse_execution
    socket.create_connection = refuse_execution
    socket.socket.connect = refuse_execution
    socket.socket.connect_ex = refuse_execution
    def audit(event, args):
        if event in {"subprocess.Popen", "os.system", "socket.connect", "socket.getaddrinfo"} or event.startswith(("os.exec", "os.spawn")):
            refuse_execution(event)
    sys.addaudithook(audit)
    sys.path.insert(0, str(ROOT / "trusted" / "tools" / "bench_direct"))
    import compiler_publish
    import compiler_receipt
    return compiler_publish, compiler_receipt


class LocalArtifact:
    """Only the two reads needed by trusted read_evidence, from independently acquired bytes."""
    def __init__(self, row, raw, name):
        self.row, self.raw, self.name = row, raw, name
        self.reads = []

    def request(self, path):
        wanted = "/actions/runs/38728622802/artifacts?name=" + self.name + "&per_page=10"
        if path != wanted:
            raise ValueError("Unexpected offline API lookup: " + path)
        self.reads.append(path)
        return {"artifacts": [self.row]}

    def download(self, url, **kwargs):
        if url != self.row["archive_download_url"]:
            raise ValueError("Unexpected offline download URL")
        self.reads.append(url)
        return self.raw


CENSUS_FIELDS = ("nodes", "retained_bytes", "regions", "candidates", "tree", "machine", "completed", "missing",
    "unspanned", "invalid", "appends", "busy", "bound", "bit_fields", "strings", "uncovered", "outside", "stale",
    "completion_queries", "recipes", "builtin_identities", "function_names", "type_names", "typed_type_names", "missing_type_names")


def raw_probe_census(files, bundle, receipt_tools, binaries):
    rows = []
    for cell, config in receipt_tools.AST_RETIREMENT_CELLS.items():
        probe = bundle[cell + "_probe"]
        source = bundle["source"]["unity"] if config["workload"] == "unity" else bundle["source"]["corpus"][config["workload"]]
        for arm, role, revision in (("a", "baseline", EXPECTED["base"]), ("b", "candidate", HEAD)):
            member = f"ast_retirement_acceptance/probe/{cell}/{arm}.log"
            raw = files[member]
            lines = raw.decode("utf-8", "strict").splitlines()
            facts = [line for line in lines if line.startswith("C_AST_FACTS ")]
            trees = [line for line in lines if line.startswith("C_AST ")]
            symbols = [line for line in lines if line.startswith("C_SYMBOL_BINDINGS ")]
            if len(facts) != 1 or len(trees) != 1 or len(symbols) > 1:
                raise ValueError("Raw probe counter population differs: " + member)
            pairs = re.findall(r"(?:^| )([a-z_]+)=([0-9]+)(?= |$)", facts[0])
            values = dict(pairs)
            if len(values) != len(pairs) or any(int(v) > (1 << 64) - 1 for v in values.values()):
                raise ValueError("Invalid or repeated census counter: " + member)
            absent = [key for key in CENSUS_FIELDS if key not in values]
            permitted = ["type_names", "typed_type_names", "missing_type_names"] if arm == "a" else []
            if absent != permitted:
                raise ValueError("Unexpected unavailable census counters: " + member + " " + repr(absent))
            parsed = {key: int(values[key]) if key in values else None for key in CENSUS_FIELDS}
            retained = bundle["acceptance"]["probes"][cell][role]
            if parsed["retained_bytes"] != retained["fact_retained_bytes"]:
                raise ValueError("Raw census retained bytes differ: " + member)
            rows.append({"cell": cell, "workload": config["workload"], "operation": config["operation"],
                "allocator": config["allocator"], "arm": arm, "role": role, "revision": revision,
                "binary_sha256": binaries[role]["sha256"], "binary_size_bytes": binaries[role]["size_bytes"],
                "source_identity": source, "probe_command": probe["command"], "probe_arm_record": probe[arm],
                "raw_log_member": member, "raw_log_sha256": digest(raw), "raw_log_bytes": len(raw),
                "raw_facts_line": facts[0], "raw_tree_line": trees[0], "raw_symbol_binding_lines": symbols,
                "facts": parsed, "unavailable_fields": absent, "retained_components": retained})
    if len(rows) != 40:
        raise ValueError("Full paired probe census is not forty rows")
    return {"schema": "buster-4239-final-retained-raw-fact-census-v1", "identity": EXPECTED,
        "rows": rows, "row_count": len(rows), "source": "actual retained untimed -v logs from final physical artifact",
        "limits": ["No historical producer census reused", "Counters describe their recorded source/input/operation; not every AST expression has a fact",
            "uncovered and outside retained separately from named missing-candidate gaps", "Tree/facts/symbol bindings stay separate; aliases are not added",
            "Absent baseline type-name counters are unavailable, never zero", "Untimed probe census is not a timed observation"]}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--zip", type=Path, required=True)
    parser.add_argument("--artifact-row", type=Path, required=True)
    parser.add_argument("--authority", type=Path, required=True,
                        help="Parent's independently authenticated identity and actual job results")
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    if not args.output.resolve().is_relative_to(ROOT):
        raise ValueError("Replay output must stay inside this task's offline-plan directory")
    args.output.mkdir(parents=True, exist_ok=False)
    publish, receipt_tools = trusted_modules()
    authority = read_json(args.authority)
    if authority.get("expected") != EXPECTED or authority.get("authorized") is not True or \
            authority.get("request_attempt") != "1" or authority.get("compare_result") != "success":
        raise ValueError("Independent authorization/attempt/compare facts incomplete or mismatched")
    row, raw = read_json(args.artifact_row), args.zip.read_bytes()
    name = publish.artifact_name(HEAD, "1")
    if row.get("name") != name or row.get("expired") is not False or \
            row.get("digest") != "sha256:" + digest(raw):
        raise ValueError("Downloaded ZIP differs from authenticated artifact name/digest")
    client = LocalArtifact(row, raw, name)
    receipt, summary, problem, artifact, throughput = publish.read_evidence(client, EXPECTED["run_id"], name)
    if problem or not isinstance(receipt, dict) or receipt.get("identity") != EXPECTED:
        raise ValueError("Trusted artifact parser refused exact identity: " + problem)
    files = throughput.get("sealed_files")
    if not isinstance(files, dict):
        raise ValueError("Strict sealed artifact reader refused the archive")
    bundle = throughput.get("ast_retirement_bundle")
    host = receipt.get("host", {}).get("cpu_model")
    problems = publish.validate_ast_retirement_bundle(receipt.get("ast_retirement_acceptance"), bundle,
        EXPECTED["base"], HEAD, receipt.get("binaries"), host, trusted_revision=TRUSTED)
    if not isinstance(receipt.get("ast_retirement_acceptance"), dict):
        problems.append("Requested AST retirement record is absent")
    if bundle.get("owner", {}).get("ownership", {}).get("trusted_tree") != "d514dc9782fed3bb6db07adc75e91bf0b79039a2":
        problems.append("AST scope-owner trusted tree differs from authenticated actual T")
    machine_problems, machine_facts = publish.machine_record(throughput.get("machine"), receipt,
        "pull", EXPECTED["run_id"], "1", require=True)
    problems.extend(machine_problems)
    problems.extend(receipt_tools.validate_closure(receipt, throughput.get("closure")))
    ordinary_pairs = None
    try:
        ordinary_pairs = publish.sealed_lab_replay(files)
    except Exception as error:
        problems.append("Ordinary statistical replay refused: " + str(error))
    cells = {}
    raw_rows = 0
    if isinstance(bundle, dict):
        for cell in receipt_tools.AST_RETIREMENT_CELLS:
            records = publish.sampling_json(files, f"ast_retirement_acceptance/{cell}/pairs.json", False)
            raw_rows += len(records)
            cells[cell] = {"raw_rows": len(records), "metrics": bundle["acceptance"]["cells"][cell]["metrics"],
                "retained": bundle["acceptance"]["probes"][cell]}
    if len(cells) != 20 or raw_rows != 800:
        problems.append("Fixed AST population is not twenty cells / eight hundred raw rows")
    ast_files = {name: data for name, data in files.items() if name.startswith("ast_retirement_acceptance/")}
    rss_packets = {name: data for name, data in ast_files.items() if name.endswith(".rss")}
    perf_csv = {name: data for name, data in ast_files.items() if name.endswith(".csv")}
    if len(rss_packets) != 800 or any(len(data) != 256 for data in rss_packets.values()):
        problems.append("Native RSS population differs from 800 exact 256-byte packets")
    if len(perf_csv) != 800:
        problems.append("Perf population differs from 800 raw CSV files")
    census = raw_probe_census(files, bundle, receipt_tools, receipt["binaries"])
    (args.output / "probe-census.json").write_text(json.dumps(census, sort_keys=True, indent=2) + "\n")
    table = io.StringIO(newline="")
    writer = csv.writer(table)
    writer.writerow(["cell", "arm", "revision", *CENSUS_FIELDS, "tree_retained_bytes", "fact_retained_bytes", "symbol_binding_retained_bytes", "symbol_binding_status", "raw_log_sha256"])
    for row in census["rows"]:
        retained = row["retained_components"]
        writer.writerow([row["cell"], row["arm"], row["revision"], *[row["facts"][key] for key in CENSUS_FIELDS], *[retained[key] for key in ("tree_retained_bytes", "fact_retained_bytes", "symbol_binding_retained_bytes", "symbol_binding_status")], row["raw_log_sha256"]])
    (args.output / "probe-census.csv").write_text(table.getvalue())
    inventory = [{"path": name, "bytes": len(data), "sha256": digest(data)} for name, data in sorted(files.items())]
    report = {"schema": "buster-4239-final-offline-ast-replay-v1", "trusted_revision": TRUSTED,
        "expected_identity": EXPECTED, "artifact_row": artifact, "zip_sha256": digest(raw),
        "zip_bytes": len(raw), "raw_ast_rows": raw_rows, "cells": cells,
        "raw_native_packets": len(rss_packets), "raw_perf_csv": len(perf_csv),
        "ast_artifact_members": len(ast_files),
        "measurement_domain": "wall/instructions include compiler plus RSS observer; compiler-child wait4 RSS; no subtraction",
        "target_observer_overhead": "unmeasured without separately matching physical controls; no compiler-only small-effect inference",
        "observed_cpu_model": host, "machine_facts": machine_facts, "problems": problems,
        "ordinary_lab_replayed_pairs": ordinary_pairs,
        "native_seal_verify_executed_locally": False, "ordinary_native_corpus_replay_executed_locally": False,
        "publisher_executed": False, "measurement_executed": False,
        "claim": "Pure AST and ordinary lab statistical replay only; root must join authenticated native publisher proof and physical-host evidence before acceptance",
        "result": "pass" if not problems else "refused", "offline_reads": client.reads}
    (args.output / "inventory.json").write_text(json.dumps(inventory, sort_keys=True, indent=2) + "\n")
    (args.output / "replay.json").write_text(json.dumps(report, sort_keys=True, indent=2) + "\n")
    print(json.dumps({"result": report["result"], "cells": len(cells), "raw_rows": raw_rows,
                      "problems": problems}, sort_keys=True))
    return 0 if not problems else 1


if __name__ == "__main__":
    raise SystemExit(main())
