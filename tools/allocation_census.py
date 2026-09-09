#!/usr/bin/env python3
"""Offline V2 allocation-census reader and atomic JSON/CSV exporter.

Usage: allocation_census.py --input compiler.log --output NEW_DIRECTORY
       [--label LABEL] [--provenance metadata.json]

No compiler or measurement process is launched. Only V2 is accepted: every
thread epoch has unique textual site rows, independent totals for all five
kinds, and an END row count. Epochs may interleave; DONE must follow all ENDs
and identify the last contiguous epoch. V1 lacks independent OS reconciliation
and is rejected. The producer must establish worker completion before DONE;
the reader can verify the emitted protocol, not inspect the original process.

The existing calling-thread source-metrics snapshot precedes report formatting.
An exit census has a different observation boundary and need not equal it.
"""
from __future__ import annotations

import argparse
import csv
import hashlib
import json
import os
from pathlib import Path
import shutil
import sys
import tempfile

FIELDS = "calls bytes padding zero_requested zero_written empty small maximum failures failed_bytes".split()
KINDS = ("arena", "os_reserve", "os_commit", "os_decommit", "os_unreserve")
SITE_FIELDS = ("kind", "file", "line", "function")
UINT64_MAX = (1 << 64) - 1


def unsigned(text: str, *, positive: bool = False, maximum: int = UINT64_MAX) -> int:
    if not text or not text.isascii() or not text.isdigit() or len(text) > 20:
        raise ValueError("expected an unsigned decimal integer")
    value = int(text)
    if value > maximum or (positive and not value):
        raise ValueError("integer outside the protocol range")
    return value


def add(total: dict, row: dict) -> None:
    for key in FIELDS:
        total[key] = max(total.get(key, 0), row[key]) if key == "maximum" else total.get(key, 0) + row[key]


def unescape(text: str) -> str:
    output = []
    index = 0
    escapes = {"t": "\t", "n": "\n", "r": "\r", "\\": "\\"}
    while index < len(text):
        value = text[index]
        if ord(value) < 32:
            raise ValueError("unescaped control character in call site")
        if value == "\\":
            index += 1
            if index == len(text) or text[index] not in escapes:
                raise ValueError("invalid census escape")
            value = escapes[text[index]]
        output.append(value)
        index += 1
    return "".join(output)


def numbers(values: list[str], kind: str) -> dict:
    if len(values) != len(FIELDS):
        raise ValueError("wrong census counter count")
    row = dict(zip(FIELDS, map(unsigned, values)))
    nonempty = row["calls"] - row["empty"]
    if (row["empty"] + row["small"] > row["calls"] or
            row["failures"] > row["calls"] or row["failed_bytes"] > row["bytes"] or
            (not row["failures"] and row["failed_bytes"]) or
            (row["failures"] == row["calls"] and row["failed_bytes"] != row["bytes"]) or
            row["maximum"] > row["bytes"] or row["bytes"] > nonempty * row["maximum"] or
            row["bytes"] < row["small"] + (nonempty - row["small"]) * 65 or
            (nonempty and not row["maximum"]) or
            (not nonempty and row["maximum"]) or
            (row["maximum"] <= 64 and row["small"] != nonempty) or
            (row["maximum"] > 64 and row["small"] == nonempty) or
            row["zero_written"] > row["zero_requested"] or row["zero_requested"] > row["bytes"] or
            (not row["calls"] and any(row.values()))):
        raise ValueError("inconsistent census counters")
    if kind == "arena" and (row["failures"] or row["failed_bytes"]):
        raise ValueError("arena rows describe completed requests; failures are OS metrics")
    if kind != "arena" and (row["padding"] or row["zero_requested"] or row["zero_written"]):
        raise ValueError("OS rows cannot contain arena padding or zeroing metrics")
    return row


def parse_census(text: str) -> dict:
    epochs = {}
    final = None
    lines = text.split("\n")
    for line_number, line in enumerate(lines, 1):
        if "BUSTER_ALLOC_" not in line:
            continue
        if line_number == len(lines):
            raise ValueError(f"unterminated census record at line {line_number}")
        if line.endswith("\r"):
            line = line[:-1]
        parts = line.split("\t")
        try:
            if final is not None or not parts[0].startswith("BUSTER_ALLOC_"):
                raise ValueError("interleaved text or census record after DONE")
            if len(parts) < 2:
                raise ValueError("truncated census record")
            epoch = unsigned(parts[1], positive=True)
            if parts[0] == "BUSTER_ALLOC_DONE_V2" and len(parts) == 2:
                if (len(epochs) != epoch or max(epochs, default=0) != epoch or
                        any(not state["closed"] for state in epochs.values())):
                    raise ValueError("DONE requires every contiguous epoch to be complete")
                final = epoch
            else:
                if parts[0] not in ("BUSTER_ALLOC_V2", "BUSTER_ALLOC_TOTAL_V2", "BUSTER_ALLOC_END_V2"):
                    raise ValueError("unknown census record; only protocol V2 is supported")
                state = epochs.setdefault(epoch, {"rows": {}, "totals": {}, "closed": False})
                if state["closed"]:
                    raise ValueError("duplicate footer or record after its epoch footer")
                if parts[0] == "BUSTER_ALLOC_V2" and len(parts) == 16:
                    kind = parts[2]
                    if kind not in KINDS:
                        raise ValueError("unknown allocation kind")
                    site = (kind, unescape(parts[3]), unsigned(parts[4], positive=True, maximum=(1 << 32) - 1), unescape(parts[5]))
                    if not site[1] or not site[3] or site in state["rows"]:
                        raise ValueError("empty or duplicate textual call-site identity")
                    row = numbers(parts[6:], kind)
                    if not row["calls"]:
                        raise ValueError("site records must contain at least one request")
                    state["rows"][site] = row
                elif parts[0] == "BUSTER_ALLOC_TOTAL_V2" and len(parts) == 13:
                    kind = parts[2]
                    if kind not in KINDS or kind in state["totals"]:
                        raise ValueError("unknown or duplicate allocation-kind total")
                    state["totals"][kind] = numbers(parts[3:], kind)
                elif parts[0] == "BUSTER_ALLOC_END_V2" and len(parts) == 3:
                    count = unsigned(parts[2])
                    if count != len(state["rows"]) or set(state["totals"]) != set(KINDS):
                        raise ValueError("missing site records or independent kind totals")
                    actual = {kind: dict.fromkeys(FIELDS, 0) for kind in KINDS}
                    for site, row in state["rows"].items():
                        add(actual[site[0]], row)
                    if actual != state["totals"]:
                        raise ValueError("site records do not reconcile with independent kind totals")
                    state["closed"] = True
                else:
                    raise ValueError("wrong field count or truncated census record")
        except ValueError as error:
            raise ValueError(f"invalid census record at line {line_number}: {error}") from error
    if final is None:
        raise ValueError("missing V2 census completion marker")
    totals = {kind: dict.fromkeys(FIELDS, 0) for kind in KINDS}
    merged = {}
    site_records = 0
    for epoch in sorted(epochs):
        state = epochs[epoch]
        for kind, values in state["totals"].items():
            add(totals[kind], values)
        for site, row in state["rows"].items():
            dest = merged.setdefault(site, dict(zip(SITE_FIELDS, site)))
            add(dest, row)
            site_records += 1
    sites = sorted(merged.values(), key=lambda row: (-row["bytes"], *(row[key] for key in SITE_FIELDS)))
    return {"schema": 2, "protocol": "BUSTER_ALLOC_V2", "completed": True,
            "epochs": final, "site_records": site_records, "totals": totals, "sites": sites}


def export_census(input_path: Path, output_path: Path, *, label: str | None = None,
                  provenance_path: Path | None = None) -> dict:
    input_path = input_path.resolve()
    raw = input_path.read_bytes()
    report = parse_census(raw.decode("utf-8"))
    raw_hash = hashlib.sha256(raw).hexdigest()
    report["raw_log"] = {"path": str(input_path), "sha256": raw_hash, "bytes": len(raw)}
    report["provenance"] = {"label": label, "supplied_metadata": None}
    report["scope"] = "Cumulative compiler-owned arena and OS API requests; not live bytes, RSS, or libc allocation counts."
    if provenance_path is not None:
        provenance_path = provenance_path.resolve()
        metadata_raw = provenance_path.read_bytes()
        metadata = json.loads(metadata_raw)
        if not isinstance(metadata, dict):
            raise ValueError("provenance metadata must be a JSON object")
        report["provenance"]["supplied_metadata"] = {
            "path": str(provenance_path), "sha256": hashlib.sha256(metadata_raw).hexdigest(),
            "data": metadata, "authority": "user-supplied; not independently verified"}
    output_path = output_path.absolute()
    if output_path.exists() or output_path.is_symlink():
        raise ValueError("output already exists; choose a new directory")
    output_path.parent.mkdir(parents=True, exist_ok=True)
    temporary = Path(tempfile.mkdtemp(prefix=f".{output_path.name}.", dir=output_path.parent))
    try:
        with (temporary / "report.json").open("w", encoding="utf-8", newline="\n") as stream:
            json.dump(report, stream, indent=2, ensure_ascii=False, allow_nan=False)
            stream.write("\n")
            stream.flush()
            os.fsync(stream.fileno())
        with (temporary / "sites.csv").open("w", encoding="utf-8", newline="") as stream:
            writer = csv.DictWriter(stream, fieldnames=["raw_log_sha256", *SITE_FIELDS, *FIELDS])
            writer.writeheader()
            for row in report["sites"]:
                writer.writerow({"raw_log_sha256": raw_hash, **row})
            stream.flush()
            os.fsync(stream.fileno())
        if output_path.exists() or output_path.is_symlink():
            raise ValueError("output appeared during export; refusing to replace it")
        os.rename(temporary, output_path)
    finally:
        if temporary.exists():
            shutil.rmtree(temporary)
    return report


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--input", type=Path, help="saved UTF-8 compiler log containing a complete V2 census")
    parser.add_argument("--output", type=Path, help="new directory for report.json and sites.csv")
    parser.add_argument("--label", help="optional user-supplied run label")
    parser.add_argument("--provenance", type=Path, help="optional existing JSON metadata object to retain with its hash")
    parser.add_argument("--self-test", action="store_true", help="run offline reader/exporter regression tests")
    args = parser.parse_args()
    result = 0
    if args.self_test:
        import unittest
        suite = unittest.defaultTestLoader.discover(str(Path(__file__).parent), pattern="allocation_census_test.py")
        result = 0 if unittest.TextTestRunner(verbosity=2).run(suite).wasSuccessful() else 1
    else:
        if args.input is None or args.output is None:
            parser.error("--input and --output are required")
        try:
            report = export_census(args.input, args.output, label=args.label, provenance_path=args.provenance)
            print(f"ALLOCATION_CENSUS completed=true epochs={report['epochs']} sites={len(report['sites'])} output={args.output}")
        except (OSError, ValueError) as error:
            print(f"allocation census: {error}", file=sys.stderr)
            result = 2
    return result


if __name__ == "__main__":
    raise SystemExit(main())
