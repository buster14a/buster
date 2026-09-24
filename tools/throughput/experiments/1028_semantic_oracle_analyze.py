#!/usr/bin/env python3
"""Summarize BUSTER_SEMANTIC_TRACE files written by 1028-semantic-oracle.patch.

One trace file per translation unit. Lines:
  SQ <mode> <start> <end> <scope> <flags> <entry_flags> valid=<v> type=<id> [rtype=<id>] sig=<S> [rsig=<S>|esig=<S>] ...
  LP <site> <start> <end> success=<s> sig=<IR signature>
  TU tokens=<n> types=<n> target=<arch>
  TM <ctype> <IR signature>
The report is diagnostic-only: populations and agreement, never timing.
"""

import collections
import json
import re
import sys
from pathlib import Path

KV = re.compile(r"(\w+)=(\S*)")


def parse_line(line):
    parts = line.split()
    if not parts:
        return None
    tag = parts[0]
    row = {"tag": tag}
    if tag == "SQ":
        row["mode"] = parts[1]
        row["start"], row["end"], row["scope"] = int(parts[2]), int(parts[3]), int(parts[4])
        row["flags"], row["entry_flags"] = int(parts[5], 16), int(parts[6], 16)
        rest = " ".join(parts[7:])
    elif tag == "LP":
        row["site"] = parts[1]
        row["start"], row["end"] = int(parts[2]), int(parts[3])
        rest = " ".join(parts[4:])
    elif tag == "TM":
        row["ctype"] = int(parts[1])
        row["sig"] = parts[2] if len(parts) > 2 else "-"
        return row
    elif tag == "TU":
        rest = " ".join(parts[1:])
    else:
        return None
    for key, value in KV.findall(rest):
        row[key] = value
    return row


def ir_kind(sig):
    head = sig.split(">")[0]
    return head.split(":")[0].split("[")[0].split("#")[0]


def analyze(path):
    stats = collections.Counter()
    hit_mismatch = []
    flag_miss = collections.Counter()
    flag_miss_differ = []
    nocache_keys = collections.Counter()
    nocache_consecutive = 0
    previous_nocache = None
    nocache_flags = collections.Counter()
    semantic_answers = collections.defaultdict(set)
    predictions = []
    type_map = {}
    site_counts = collections.Counter()
    with open(path, "rt", errors="replace") as source:
        for line in source:
            try:
                row = parse_line(line)
            except (ValueError, IndexError):
                stats["malformed_lines"] += 1
                continue
            if not row:
                continue
            tag = row["tag"]
            if tag == "SQ":
                mode = row["mode"]
                stats["sq_total"] += 1
                stats["sq_" + mode] += 1
                key = (row["start"], row["end"], row["scope"])
                if row.get("valid") == "1" and row.get("type", "4294967295") != "4294967295":
                    semantic_answers[(row["start"], row["end"])].add(int(row["type"]))
                if mode == "HIT":
                    ok = row.get("compatible") == "1" and row.get("rvalid") == "1" and row.get("rdiag") == "0" and row.get("rconstraint") == "0"
                    stats["hit_ok" if ok else "hit_mismatch"] += 1
                    stats["hit_same_id"] += row.get("same_id") == "1"
                    if not ok and len(hit_mismatch) < 50:
                        hit_mismatch.append(line.strip())
                elif mode in ("MISS_FLAGS", "MISS_UNCHECKED"):
                    pair = (mode, row["flags"], row["entry_flags"])
                    flag_miss[pair] += 1
                    if row.get("valid") == "1" and row.get("esig", "") not in ("", "-"):
                        differ = row.get("sig") != row.get("esig")
                        flag_miss[pair + ("differ" if differ else "same",)] += 1
                        if differ and len(flag_miss_differ) < 50:
                            flag_miss_differ.append(line.strip())
                elif mode == "NOCACHE":
                    nocache_keys[key] += 1
                    nocache_flags[row["flags"]] += 1
                    if previous_nocache == key:
                        nocache_consecutive += 1
                    previous_nocache = key
                if mode != "NOCACHE":
                    previous_nocache = None
                stats["published"] += row.get("published") == "1"
            elif tag == "LP":
                predictions.append(row)
                site_counts[row["site"]] += 1
                stats["lp_total"] += 1
                stats["lp_fallback_s32"] += row.get("success") == "0"
            elif tag == "TM":
                type_map[row["ctype"]] = row["sig"]
            elif tag == "TU":
                stats["tu"] += 1
                stats["tu_tokens"] = int(row.get("tokens", "0"))
                stats["tu_types"] = int(row.get("types", "0"))
    join = collections.Counter()
    join_kinds = collections.Counter()
    join_samples = collections.defaultdict(list)
    for row in predictions:
        answers = semantic_answers.get((row["start"], row["end"]))
        if not answers:
            join["lp_without_semantic_answer"] += 1
            join["site_unmatched:" + row["site"]] += 1
            continue
        mapped = {type_map.get(ctype, "?") for ctype in answers}
        lp_sig = row.get("sig", "-")
        if lp_sig in mapped:
            join["lp_equal_to_a_semantic_answer"] += 1
            join["site_equal:" + row["site"]] += 1
        else:
            join["lp_differs_from_all_semantic_answers"] += 1
            join["site_differs:" + row["site"]] += 1
            for sig in mapped:
                pair = (row["site"], ir_kind(lp_sig), ir_kind(sig), "fallback" if row.get("success") == "0" else "predicted")
                join_kinds[pair] += 1
                if len(join_samples[pair]) < 5:
                    join_samples[pair].append({"lp": row, "semantic_sig": sig, "ctypes": sorted(answers)})
        if len(mapped) > 1:
            join["semantic_answers_disagree_among_themselves"] += 1
    nocache_total = sum(nocache_keys.values())
    report = {
        "file": str(path),
        "stats": dict(stats),
        "hit_mismatch_samples": hit_mismatch,
        "flag_miss": {" ".join(map(str, key)): value for key, value in sorted(flag_miss.items())},
        "flag_miss_differ_samples": flag_miss_differ,
        "nocache": {
            "total": nocache_total,
            "distinct_keys": len(nocache_keys),
            "repeats": nocache_total - len(nocache_keys),
            "consecutive_identical": nocache_consecutive,
            "keys_queried_3_or_more": sum(1 for count in nocache_keys.values() if count >= 3),
            "flags": {hex(key): value for key, value in sorted(nocache_flags.items())},
        },
        "prediction_sites": dict(site_counts),
        "boundary_join": dict(join),
        "boundary_join_kind_pairs": {" ".join(map(str, key)): value for key, value in sorted(join_kinds.items(), key=lambda item: -item[1])},
        "boundary_join_samples": {" ".join(map(str, key)): value for key, value in join_samples.items()},
    }
    return report


def main(argv):
    reports = [analyze(Path(argument)) for argument in argv[1:]]
    totals = collections.Counter()
    for report in reports:
        for key, value in report["stats"].items():
            if key not in ("tu_tokens", "tu_types"):
                totals[key] += value
        for key in ("total", "distinct_keys", "repeats", "consecutive_identical", "keys_queried_3_or_more"):
            totals["nocache_" + key] += report["nocache"][key]
        for key, value in report["boundary_join"].items():
            totals["join_" + key] += value
    print(json.dumps({"totals": dict(totals), "files": reports}, indent=1))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
