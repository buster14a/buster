#!/usr/bin/env python3
"""Verify this frozen research capture; not a future compiler benchmark/gate.

Usage: replay.py INITIAL.zip FOLLOWUP.zip [--self-test]
This never compiles or executes a captured binary, extracts archive paths,
changes the repository, or treats diagnostic timestamps as performance passes.
"""
import copy
import hashlib
import json
from pathlib import Path
import re
import sys
import zipfile

EXPECTED = (
    "c34b70ce2f82d3857e8ad66d8210728efe0bcb4014e1c47e7917d493012545ca",
    "658a4f5aa1c4e8bc928a33d754edcb6ebf2bc721ceb83925d3594536d00eb902",
)


def require(condition, message):
    if not condition:
        raise ValueError(message)


def load(path, expected):
    data = Path(path).read_bytes()
    require(hashlib.sha256(data).hexdigest() == expected, "archive identity mismatch")
    with zipfile.ZipFile(path) as archive:
        names = archive.namelist()
        require(len(names) == len(set(names)), "duplicate archive member")
        require(sum(x.file_size for x in archive.infolist()) < 100_000_000,
                "unexpected archive size")
        for line in archive.read("SHA256SUMS").decode().splitlines():
            digest, name = line.split("  ", 1)
            require(hashlib.sha256(archive.read(name)).hexdigest() == digest,
                    "member identity mismatch: " + name)
        records = {name: archive.read(name) for name in
                   ("census.json", "observations.jsonl", "runs.jsonl", "inputs.json")
                   if name in names}
    return records


def fields(line):
    pairs = [item.split("=", 1) for item in line.split()[1:]]
    require(len(pairs) == len({key for key, _ in pairs}), "duplicate counter")
    return {key: int(value) for key, value in pairs}


def verify(initial, followup):
    require(len(initial) == 112 and len(followup) == 378, "incomplete observation population")
    require(all(row["status"] == 0 for row in initial + followup), "failed compilation")
    require(len({r["case"] for r in initial}) == 56, "initial case count")
    require(len({r["case"] for r in followup}) == 93, "follow-up case count")
    for population in (initial, followup):
        groups = {}
        for row in population:
            key = (row["case"], row["binary"], row.get("observation", "census"))
            require(key not in groups, "duplicate observation")
            groups[key] = row
        for case in {r["case"] for r in population}:
            pair = [groups[(case, binary, "census")] for binary in ("base", "probe")]
            hashes = {r["output_sha256"] for r in population if r["case"] == case}
            require(len(hashes) == 1 and None not in hashes, "object identity mismatch")
            require(pair[0]["output_bytes"] == pair[1]["output_bytes"], "object size mismatch")
    counters = {}
    model_rows = 0
    for row in followup:
        lines = row["traces"]
        if row["binary"] == "base":
            require(not lines, "instrumented baseline")
            continue
        totals = [fields(t) for t in lines if t.startswith("C_GROWTH_V1 ")]
        queries = [fields(t) for t in lines if t.startswith("C_GROWTH_QUERY ")]
        require(len(totals) == 1, "missing or duplicate TU total")
        total = totals[0]
        require(total["diagnostics"] == 0, "unexpected diagnostic")
        require(len(queries) == total["qualified_calls"], "missing query trace")
        require(sum(q["visits"] for q in queries) == total["qualified_visits"], "query/total mismatch")
        last = None
        for q in queries:
            key = (q["key"], q["atomic"], q["volatile"])
            hit = bool(q["valid"] and last and last[0] == key)
            require(q["cache_hit"] == hit and q["mismatch"] == 0, "scalar replay mismatch")
            if hit:
                require(last[1] == q["result"], "scalar returned ID changed")
            if q["valid"]:
                last = (key, q["result"])
        if row["observation"] != "census":
            continue
        counters[row["case"]] = total
        match = re.fullmatch(r"(qualified|constant|one-body|atomic-runtime)-n(\d+)-q(\d+)", row["case"])
        if match:
            family, n, q = match.groups()
            n, q = int(n), int(q)
            if family == "qualified":
                require(total["qualified_visits"] == q * (n + q + 27) - 1, "many-body work model")
            elif family == "constant":
                require(total["entity_visits"] == q * (n + 1), "entity work model")
                require(total["global_visits"] == q * (n + 1), "global work model")
            elif family == "one-body":
                require(total["qualified_visits"] == q * (n + 28) - 1, "independent work model")
                require(total["ir_types"] == n + 29 and total["entities"] == 3, "Q grew the table")
            else:
                require(total["runtime_calls"] == q, "runtime route not exercised")
                require(total["symbol_visits"] == q * (n + 2) - 1, "runtime work model")
            model_rows += 1
    require(model_rows == 64, "incomplete work-model grid")
    for row in initial:
        if row["binary"] == "probe":
            require(len(row["counters"]) == 1, "initial TU total count")
            require(fields(row["counters"][0]) == counters[row["case"]], "cross-capture count mismatch")
    require(counters["constant-early"]["entity_visits"] == 256 and
            counters["constant-early"]["global_visits"] == 256, "constant early-hit control")
    require(counters["atomic-runtime-native-control"]["runtime_calls"] == 0, "native atomic control")
    require(all(counters[f"runtime-n{n}-q{q}"]["runtime_calls"] == 0
                for n in (0, 64, 256, 1024) for q in (1, 16, 64, 256)), "original route changed")
    return {"initial_compilations": len(initial), "followup_compilations": len(followup),
            "followup_cases": 93, "exact_work_model_points": model_rows,
            "all_object_identities_match": True, "scalar_mismatches": 0}


def main():
    require(len(sys.argv) in (3, 4), "usage: replay.py INITIAL.zip FOLLOWUP.zip [--self-test]")
    require(len(sys.argv) == 3 or sys.argv[3] == "--self-test", "unknown option")
    first, second = [load(path, digest) for path, digest in zip(sys.argv[1:3], EXPECTED)]
    initial = json.loads(first["census.json"])
    followup = [json.loads(line) for line in second["observations.jsonl"].splitlines()]
    result = verify(initial, followup)
    if len(sys.argv) == 4:
        failures = 0
        for kind in ("missing", "object", "counter", "scalar"):
            damaged = copy.deepcopy(followup)
            if kind == "missing":
                damaged.pop()
            elif kind == "object":
                damaged[0]["output_sha256"] = "0" * 64
            else:
                index = next(i for i, r in enumerate(damaged) if r["case"] == "one-body-n1024-q256" and r["binary"] == "probe")
                key = "qualified_visits=269311" if kind == "counter" else "mismatch=0"
                replacement = "qualified_visits=269312" if kind == "counter" else "mismatch=1"
                damaged[index]["traces"] = [t.replace(key, replacement) for t in damaged[index]["traces"]]
            try:
                verify(initial, damaged)
            except ValueError:
                failures += 1
        require(failures == 4, "negative replay test unexpectedly accepted")
        result["negative_replay_tests_rejected"] = failures
    print(json.dumps(result, indent=2, sort_keys=True))


if __name__ == "__main__":
    try:
        main()
    except (ValueError, KeyError, OSError, zipfile.BadZipFile) as error:
        raise SystemExit("FAIL: " + str(error)) from error
